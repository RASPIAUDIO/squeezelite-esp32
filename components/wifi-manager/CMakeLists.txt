set( WEBPACK_DIR webapp/webpack/dist )

# for the forgetful, REQUIRES cannot use CONFIG_XXX due to parsing order
if(IDF_TARGET STREQUAL "esp32" OR IDF_TARGET STREQUAL "esp32s31")
    set(target_requires "driver_bt")
endif()
if(IDF_TARGET STREQUAL "esp32s31")
    list(APPEND target_requires espressif__yt8531 sendspin_sink)
endif()

idf_component_register( SRC_DIRS . webapp UML-State-Machine-in-C/src
						INCLUDE_DIRS . webapp UML-State-Machine-in-C/src
						REQUIRES squeezelite-ota json espressif__mdns esp_wifi esp_eth esp_ringbuf esp_driver_gpio esp_driver_i2c services
						PRIV_REQUIRES display tools platform_config esp_common json esp_libc freertos spi_flash nvs_flash espressif__mdns pthread wpa_supplicant platform_console esp_http_server console esp_timer ${target_requires}
)

include(webapp/webapp.cmake)

target_add_binary_data(${COMPONENT_LIB} "webapp/src/sendspin.html" TEXT)
