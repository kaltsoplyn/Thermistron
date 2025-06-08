idf_component_register(SRCS "thermistron.c"
                    PRIV_REQUIRES spi_flash config_comp temp_comp serial_comp
                    INCLUDE_DIRS "")
