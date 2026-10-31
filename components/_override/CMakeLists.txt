
if(IDF_TARGET STREQUAL "esp32s31")
    # IDF 6.1 supplies these symbols. Never override its PHY/security routines.
    idf_component_register()
else()
    # Create component with ESP-IDF 5.5.1 stubs for missing symbols
    idf_component_register(
        SRCS "esp_idf_stubs.c"
        PRIV_REQUIRES bootloader_support
        )
endif()
