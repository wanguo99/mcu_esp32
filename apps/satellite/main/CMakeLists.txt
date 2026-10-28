idf_component_register(SRCS "app_main.c" "satellite.c" "satellite_console.c" "satellite_web.c" INCLUDE_DIRS "." REQUIRES runtime console angle_search power_monitor esp_app_format esp_hw_support esp_psram esp_system esp_timer heap spi_flash PRIV_REQUIRES esp_driver_rmt)

idf_component_optional_requires(PRIVATE servo voltage_sensor network mpu6050 ws2812 fan relay_uart web)

if(CONFIG_MCU_ENABLE_WEB)
    target_add_binary_data(${COMPONENT_LIB} "web/satellite.html" TEXT)
    target_add_binary_data(${COMPONENT_LIB} "web/satellite.css" TEXT)
    target_add_binary_data(${COMPONENT_LIB} "web/satellite.js" TEXT)
endif()
