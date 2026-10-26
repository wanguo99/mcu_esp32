idf_component_register(SRCS "runtime.c" INCLUDE_DIRS "include" REQUIRES board app_config settings)
idf_component_optional_requires(PRIVATE network web camera storage recording monitor_web servo)
