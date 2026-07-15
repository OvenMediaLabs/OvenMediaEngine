ome_add_static_library(webrtc_provider
    DEPS
        base_provider
        access_controller
        bitstream
        dtls_srtp
        http
        ice
        rtc_signalling
        rtp_rtcp
        sdp
        whip
        orchestrator
        ovcrypto
        ovlibrary
)

if(OME_BUILD_TESTS)
    file(GLOB _srcs "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")
    ome_add_tests(ome_test_providers_webrtc
        SRCS ${_srcs}
    )
endif()
