if(IDF_TARGET STREQUAL "esp32" OR IDF_TARGET STREQUAL "esp32s31")
    idf_component_register(	SRC_DIRS .
                            INCLUDE_DIRS .
                            PRIV_REQUIRES services bt display console tools platform_config

)
endif()

if(IDF_TARGET STREQUAL "esp32s31")
    # Native LDAC decoder and bounded A2DP packet receiver.
    target_sources(${COMPONENT_LIB} PRIVATE
        ldac/libldacdec.c ldac/bit_reader.c ldac/bit_allocation.c
        ldac/huffCodes.c ldac/utility.c ldac/imdct.c ldac/spectrum.c ldac/ldac_rx.c)
    idf_component_get_property(audio_codec espressif__esp_audio_codec COMPONENT_LIB)
    target_link_libraries(${COMPONENT_LIB} PRIVATE ${audio_codec})
endif()

