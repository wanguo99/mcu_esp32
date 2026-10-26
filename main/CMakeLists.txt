idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_app_format esp_hw_support esp_psram esp_system esp_timer heap nvs_flash spi_flash
)
