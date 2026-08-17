ome_add_static_library(rtc_signalling
    SOURCES_DIRS p2p
    DEPS
        http
        ice
        sdp
        address_utilities
        ovlibrary
)
