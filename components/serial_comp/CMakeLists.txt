idf_component_register(SRCS "src/serial_comp.c"
                        REQUIRES esp_driver_usb_serial_jtag config_comp temp_comp
                       INCLUDE_DIRS "include")