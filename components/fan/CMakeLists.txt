idf_component_register(SRCS "fan.c" INCLUDE_DIRS "include" REQUIRES peripheral_resources esp_driver_gpio esp_driver_ledc esp_driver_pcnt esp_timer)
