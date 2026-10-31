idf_component_register(SRCS "sendspin_sink.cpp" INCLUDE_DIRS "include"
    REQUIRES sendspin_lib nvs_flash esp_netif esp_timer esp_wifi mdns console json)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
