ome_add_static_library(access_controller
    SOURCES_DIRS
        admission_webhooks
        signed_policy
    DEPS
        http
        ovlibrary
)
