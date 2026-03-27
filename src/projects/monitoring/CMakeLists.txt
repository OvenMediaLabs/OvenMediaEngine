ome_add_static_library(monitoring
    SOURCES_DIRS alert
    DEPS
        base_info
        http
        json_serdes
        config
        ovlibrary
)
