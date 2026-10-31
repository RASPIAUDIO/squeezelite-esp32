if(NOT IDF_TARGET STREQUAL "esp32s31")
    idf_component_register()
    return()
endif()

set(ap2_sources
    "upstream/alac_magic_cookie.c"
    "upstream/audio/audio_buffer.c"
    "upstream/audio/audio_crypto.c"
    "upstream/audio/audio_decoder.c"
    "upstream/audio/audio_receiver.c"
    "upstream/audio/audio_stream.c"
    "upstream/audio/audio_stream_buffered.c"
    "upstream/audio/audio_stream_realtime.c"
    "upstream/audio/audio_timing.c"
    "upstream/dacp_client.c"
    "upstream/hap/hap.c"
    "upstream/hap/hap_crypto.c"
    "upstream/hap/hap_pair_setup.c"
    "upstream/hap/hap_pair_verify.c"
    "upstream/hap/srp.c"
    "upstream/hap/tlv8.c"
    "upstream/network/mdns_airplay.c"
    "upstream/network/ntp_clock.c"
    "upstream/network/ptp_clock.c"
    "upstream/network/socket_utils.c"
    "upstream/plist/base64.c"
    "upstream/plist/bplist_builder.c"
    "upstream/plist/bplist_parser.c"
    "upstream/plist/plist_xml.c"
    "upstream/rtsp/rtsp_conn.c"
    "upstream/rtsp/rtsp_crypto.c"
    "upstream/rtsp/rtsp_events.c"
    "upstream/rtsp/rtsp_fairplay.c"
    "upstream/rtsp/rtsp_handlers.c"
    "upstream/rtsp/rtsp_message.c"
    "upstream/rtsp/rtsp_rsa.c"
    "upstream/rtsp/rtsp_server.c"
)
idf_component_register(SRCS ${ap2_sources} "ap2_psa.c" "ap2_rsa.c" "airplay2_sink.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "." "../raop" "upstream" "upstream/audio" "upstream/rtsp"
        "upstream/hap" "upstream/plist" "upstream/network"
    PRIV_REQUIRES freertos esp_timer esp_netif esp_wifi esp_http_client
        nvs_flash mbedtls esphome__libsodium espressif__mdns
        espressif__esp_audio_codec)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable)


target_compile_definitions(${COMPONENT_LIB} PRIVATE CONFIG_ENABLE_AIRPLAY_ARTWORK=1)
