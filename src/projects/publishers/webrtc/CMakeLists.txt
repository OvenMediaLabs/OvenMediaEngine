ome_add_static_library(webrtc_publisher
    DEPS
        base_publisher
        dtls_srtp
        rtp_rtcp
        sdp
        ice
        http
        rtc_signalling
        json_serdes
        monitoring
        orchestrator
        ovlibrary
        ovcrypto
        config
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_publishers_webrtc
        SRCS ${_srcs}
    )
endif()
