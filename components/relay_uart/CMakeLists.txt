idf_component_register(SRCS "relay_uart.c" "relay_uart_codec.c" INCLUDE_DIRS "include" REQUIRES peripheral_resources esp_driver_uart esp_driver_gpio esp_timer)
