idf_component_register(SRCS "monitor_web.c" INCLUDE_DIRS "include"
    EMBED_TXTFILES "web/index.html" "web/styles.css" "web/api.js" "web/monitor.js"
    REQUIRES web camera network app_config esp_app_format esp_timer)
idf_component_optional_requires(PRIVATE storage recording)
