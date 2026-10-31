if(IDF_TARGET STREQUAL "esp32s31")
  set(ap2_dep airplay2)
endif()

idf_component_register(SRC_DIRS .    
						INCLUDE_DIRS .   
						PRIV_REQUIRES ${ap2_dep} esp_libc freertos pthread platform_config espressif__mdns services codecs tools display wifi-manager mbedtls
						  
)
set_source_files_properties(raop.c
    PROPERTIES COMPILE_FLAGS
    -Wno-misleading-indentation
)