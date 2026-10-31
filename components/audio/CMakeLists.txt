if(IDF_TARGET STREQUAL "esp32s31")
    idf_component_register(SRCS equalizer_s31.c INCLUDE_DIRS . inc
        REQUIRES espressif__esp_audio_effects)
    # Ten stereo bands at 192 kHz need speed optimization; keep IEEE math.
    target_compile_options(${COMPONENT_LIB} PRIVATE -O3)
    return()
endif()
idf_component_register(	SRCS link_helper.c
						INCLUDE_DIRS .  inc 
						)
add_prebuilt_library(esp_processing lib/libesp_processing.a)
target_link_libraries(${COMPONENT_LIB} PRIVATE esp_processing)
#target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u pow")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u cos")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u sin")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u sqrt")
