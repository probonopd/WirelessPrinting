/*
 * SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "USBSerial.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "usb/cdc_acm_host.h"
#include "usb/vcp_ch34x.hpp"
#include "usb/vcp_cp210x.hpp"
#include "usb/vcp_ftdi.hpp"
#include "usb/vcp.hpp"
#include "usb/usb_host.h"


namespace USBHost {

using namespace esp_usb;
using std::size_t;

// Change these values to match your needs
#define EXAMPLE_BAUDRATE     (115200)
#define EXAMPLE_STOP_BITS    (0)      // 0: 1 stopbit, 1: 1.5 stopbits, 2: 2 stopbits
#define EXAMPLE_PARITY       (0)      // 0: None, 1: Odd, 2: Even, 3: Mark, 4: Space
#define EXAMPLE_DATA_BITS    (8)

namespace {

/**
 * @brief Data received callback
 *
 * Just pass received data to stdout
 */

/*
 * @return true        Received data was processed     -> Flush RX buffer
 * @return false       Received data was NOT processed -> Append new data to the buffer
 */
cdc_acm_data_callback_t handle_rx; //)(const unsigned char* data, size_t data_len, void *arg);

/**
 * @brief Device event callback
 *
 * Apart from handling device disconnection it doesn't do anything useful
 */
bool conn_open = false;
bool connected = false;
void handle_event(const cdc_acm_host_dev_event_data_t *event, void *user_ctx)
{
    switch (event->type) {
        case CDC_ACM_HOST_ERROR:
            log_e("CDC-ACM error has occurred, err_no = %d", event->data.error);
            break;
        case CDC_ACM_HOST_DEVICE_DISCONNECTED:
            log_i("Device suddenly disconnected");
            connected = false;
            conn_open = false;
            break;
        case CDC_ACM_HOST_SERIAL_STATE:
            log_i("Serial state notif 0x%04X", event->data.serial_state.val);
            break;
        case CDC_ACM_HOST_NETWORK_CONNECTION:
        default: 
            log_e("Unhandled USB event %d", event->type);
            break;
    }
}

/**
 * @brief USB Host library handling task
 *
 * @param arg Unused
 */
void usb_lib_task(void *arg)
{
    while (1) {
        // Start handling system events
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_ERROR_CHECK(usb_host_device_free_all());
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            log_i("USB: All devices freed");
            // Continue handling USB events to allow device reconnection
        }
    }
}

} // namespace (anonymous)

void setup(cdc_acm_data_callback_t cb)
{
    assert(!connected && !conn_open);
    handle_rx = cb;

    //Install USB Host driver. Should only be called once in entire application
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    // Create a task that will handle USB library events
    BaseType_t task_created = xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 10, NULL);
    assert(task_created == pdTRUE);

    ESP_ERROR_CHECK(cdc_acm_host_install(NULL));

    // Register VCP drivers to VCP service.
    VCP::register_driver<FT23x>();
    VCP::register_driver<CP210x>();
    VCP::register_driver<CH34x>();
}

static std::unique_ptr<CdcAcmDevice> vcp;
void send(uint8_t* data, size_t len) {
  if (!connected || !conn_open) {
    log_e("Failed attempt to write to USB (open=%d, connected=%d)", conn_open, connected);
    return;
  }
  // Send some dummy data
  log_i("USB tx: %.*s", len, data);
  ESP_ERROR_CHECK(vcp->tx_blocking(data, len));
  ESP_ERROR_CHECK(vcp->set_control_line_state(true, true));
}

bool is_connected() {
  return connected;
}

void loop() {
  if (conn_open) {
    return;
  }

  const cdc_acm_host_device_config_t dev_config = {
      .connection_timeout_ms = 5000, // 5 seconds, enough time to plug the device in or experiment with timeout
      .out_buffer_size = 64,
      .in_buffer_size = 64,
      .event_cb = handle_event,
      .data_cb = handle_rx,
      .user_arg = NULL,
  };

  // You don't need to know the device's VID and PID. Just plug in any device and the VCP service will pick correct (already registered) driver for the device
  //log_i("Opening any VCP device...");
  conn_open = true;
  vcp = std::unique_ptr<CdcAcmDevice>(VCP::open(&dev_config));

  if (vcp == nullptr) {
      //log_i("Failed to open VCP device");
      conn_open = false;
      return;
  }
  vTaskDelay(10);

  cdc_acm_line_coding_t line_coding = {
      .dwDTERate = EXAMPLE_BAUDRATE,
      .bCharFormat = EXAMPLE_STOP_BITS,
      .bParityType = EXAMPLE_PARITY,
      .bDataBits = EXAMPLE_DATA_BITS,
  };
  ESP_ERROR_CHECK(vcp->line_coding_set(&line_coding));

  log_i("connected");
  connected = true;
}

} // namespace USBHost
