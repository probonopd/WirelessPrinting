## 1.0.0

- Initial version

## 1.0.4

- C++ methods are now virtual to allow derived classes to override them.

## 2.0.0

- New function `cdc_acm_host_register_new_dev_callback`. This allows you to get New Device notifications even if you use the default driver.
- Receive buffer has configurable size. This is useful if you expect data transfers larger then Maximum Packet Size.
- Receive buffer has 'append' function. In the Data Received callback you can signal that you wait for more data and the current data were not yet processed. In this case, the CDC driver appends new data to the already received data. This is especially useful if the upper layer messages consist of multiple USB transfers and you don't want to waste more RAM and CPU copying the data around.

