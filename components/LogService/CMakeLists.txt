# Header-only component (all appenders + Logger are header-only)
idf_component_register(
    SRCS "log_dummy.c"
    INCLUDE_DIRS "include"
    REQUIRES ArcanaTs freertos log
)
