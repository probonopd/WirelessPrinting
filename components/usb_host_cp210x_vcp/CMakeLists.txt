idf_component_register(SRCS "usb_host_cp210x_vcp.cpp"
                    INCLUDE_DIRS "include")


# TODO added
target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions)
