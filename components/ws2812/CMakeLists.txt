idf_component_register(SRCS "ws2812.c" "ws2812_effects.c" INCLUDE_DIRS "include" REQUIRES peripheral_resources esp_driver_gpio esp_timer led_strip)
