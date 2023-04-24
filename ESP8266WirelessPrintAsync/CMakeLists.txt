# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/ESP8266WirelessPrintAsync/*.*)

idf_component_register(
  SRCS ${app_sources}
  INCLUDE_DIRS "." "../components/usb_host_ch34x_vcp/include/usb"
  REQUIRES usb_host_ch34x_vcp usb_host_cdc_acm usb_host_cp210x_vcp usb_host_vcp usb_host_ftdi_vcp
)

# Use of usb_host_vcp requires c++14, -fconcepts
# as we are importing a header using the `auto` symbol
# Additionally -fexceptions was enabled in components/usb_host_vcp/CMakeLists.txt to support exception handling which platformio apparently disables by default?
set_target_properties(${COMPONENT_LIB} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)
target_compile_options(${COMPONENT_LIB} PRIVATE -fconcepts)

