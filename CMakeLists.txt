cmake_minimum_required(VERSION 3.16)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Compatibility floor for vendored codec CMake projects")
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON CACHE BOOL "Windows response files")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PROJECT_VER $ENV{PROJECT_VER})
add_definitions(-DMODEL_NAME=SqueezeESP32)

# Add Xtensa-specific compiler flags for better code placement (ESP32 rev3)
# -mtext-section-literals helps keep code and literals closer together
if(NOT IDF_TARGET STREQUAL "esp32s31")
  add_compile_options(-mtext-section-literals)
  add_link_options(-mtext-section-literals)
endif()

if(NOT DEFINED DEPTH)
	set(DEPTH "16")
endif()	

# State machine hierarchy enabled and logging enabled
add_definitions(-DSTATE_MACHINE_LOGGER=1)
add_definitions(-DHIERARCHICAL_STATES=1)

# Uncomment line below to get memory usage trace details
#add_definitions(-DENABLE_MEMTRACE=1)
#uncomment line below to get network ethernet debug logs 
#add_definitions(-DNETWORK_ETHERNET_LOG_LEVEL=ESP_LOG_DEBUG)
#uncomment line below to get network status debug logs
#add_definitions(-DNETWORK_STATUS_LOG_LEVEL=ESP_LOG_DEBUG)
#add_definitions(-DNETWORK_HANDLERS_LOG_LEVEL=ESP_LOG_DEBUG)
#add_definitions(-DNETWORK_WIFI_LOG_LEVEL=ESP_LOG_DEBUG)
#add_definitions(-DNETWORK_MANAGER_LOG_LEVEL=ESP_LOG_DEBUG)
#add_definitions(-DNETWORK_HTTP_SERVER_LOG_LEVEL=ESP_LOG_DEBUG)

# utility to build sizes
function(build_size target_name) 
    set(target_elf ${target_name}.elf)
    set(target_map ${target_name}.map)
    set(idf_size ${python} ${IDF_PATH}/tools/idf_size.py)
    
    if(DEFINED OUTPUT_JSON AND OUTPUT_JSON)
        list(APPEND idf_size "--json")
    endif()
 
    add_custom_target(size-${target_name} ALL
        DEPENDS ${target_elf}
        COMMAND ${idf_size} ${target_map} -o "size-${target_name}"
    )

    add_custom_target(size-files-${target_name} ALL
        DEPENDS ${target_elf}
        COMMAND ${idf_size} --files ${target_map}
    )

    add_custom_target(size-components-${target_name} ALL
        DEPENDS ${target_elf}
        COMMAND ${idf_size} --archives ${target_map}
    )
endfunction()  

# manually add the 2 versions for application: recovery and squeezelite
set(EXTRA_COMPONENT_DIRS components/platform_console/app_recovery components/platform_console/app_squeezelite  )

project(recovery)

if(IDF_TARGET STREQUAL "esp32s31")
  include(cmake/airplay2_sodium.cmake)
endif()

if(IDF_TARGET STREQUAL "esp32s31" AND CONFIG_BT_A2DP_USE_EXTERNAL_CODEC)
  set(ldac_overlay "${CMAKE_BINARY_DIR}/mncast-ldac")
  execute_process(COMMAND ${PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/prepare_ldac_idf.py"
    --idf "${IDF_PATH}" --out "${ldac_overlay}" RESULT_VARIABLE ldac_overlay_result)
  if(NOT ldac_overlay_result EQUAL 0)
    message(FATAL_ERROR "Cannot prepare reviewed LDAC ESP-IDF overlay")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/prepare_ldac_idf.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ldac_idf_helper.h")
  # The expanded codec CIE must have the same ABI in every compiled component.
  idf_build_get_property(ldac_components BUILD_COMPONENTS)
  foreach(component IN LISTS ldac_components)
    idf_component_get_property(component_lib ${component} COMPONENT_LIB)
    get_target_property(component_type ${component_lib} TYPE)
    if(NOT component_type STREQUAL "INTERFACE_LIBRARY")
      target_include_directories(${component_lib} BEFORE PRIVATE "${ldac_overlay}/include")
      # LDAC needs 11 octets including LOSC. bt_target.h permits this override;
      # all users of AVDT/BTA structs must agree, not just the patched sources.
      target_compile_definitions(${component_lib} PRIVATE AVDT_CODEC_SIZE=16)
    endif()
  endforeach()
  idf_component_get_property(ldac_bt bt COMPONENT_LIB)
  get_target_property(ldac_bt_sources ${ldac_bt} SOURCES)
  foreach(file btc_av.c bta_av_co.c btc_a2dp_sink_ext_coedc.c)
    list(FILTER ldac_bt_sources EXCLUDE REGEX "(^|/)${file}$")
    list(APPEND ldac_bt_sources "${ldac_overlay}/${file}")
  endforeach()
  set_property(TARGET ${ldac_bt} PROPERTY SOURCES "${ldac_bt_sources}")
endif()

if(IDF_TARGET STREQUAL "esp32s31")
  # The prebuilt archive exports private FLAC/Vorbis/AAC implementation names.
  # Isolate those names from Squeezelite's existing codec ABIs, independent of
  # CMake's transitive link ordering. Keep public esp_* and weak platform hooks.
  get_target_property(bt_codec_original esp_acodec IMPORTED_LOCATION)
  set(bt_codec_isolated "${BUILD_DIR}/bt-codec/libesp_audio_codec.a")
  add_custom_command(OUTPUT "${bt_codec_isolated}"
    COMMAND ${PYTHON} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/isolate_audio_codec.py"
      --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}"
      --input "${bt_codec_original}" --output "${bt_codec_isolated}"
    DEPENDS "${bt_codec_original}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/isolate_audio_codec.py"
    VERBATIM)
  add_custom_target(bt_codec_isolated DEPENDS "${bt_codec_isolated}")
  set_property(TARGET esp_acodec PROPERTY IMPORTED_LOCATION "${bt_codec_isolated}")
  add_dependencies(esp_acodec bt_codec_isolated)
endif()

# we need own "esp_app_desc" to take precedence
add_custom_command(
			TARGET recovery.elf
			PRE_LINK
			COMMAND ${CMAKE_OBJCOPY} --weaken-symbol esp_app_desc  ${BUILD_DIR}/esp-idf/app_update/libapp_update.a
	        VERBATIM
)

# when building recovery, add app_recovery to the link
get_target_property(BCA recovery.elf LINK_LIBRARIES)
list(REMOVE_ITEM BCA "idf::app_squeezelite" "idf::app_recovery" "-Wl,--Map=${BUILD_DIR}/recovery.map")
set_target_properties(recovery.elf PROPERTIES LINK_LIBRARIES "${BCA};idf::app_recovery;-Wl,--Map=${BUILD_DIR}/recovery.map")

# create files with size for recovery 
# build_size(recovery)

# build squeezelite, add app_squeezelite to the link
add_executable(squeezelite.elf "CMakeLists.txt")
add_dependencies(squeezelite.elf recovery.elf)
# This second image is not created by project(), so it must receive the SDK's
# executable link flags explicitly (notably --gc-sections and -fno-lto).
# Without them unused radio/bootloader objects create spurious unresolved APIs.
idf_build_get_property(squeezelite_link_options LINK_OPTIONS)
target_link_options(squeezelite.elf PRIVATE ${squeezelite_link_options})
idf_build_get_property(squeezelite_link_depends __LINK_DEPENDS)
set_property(TARGET squeezelite.elf APPEND PROPERTY LINK_DEPENDS "${squeezelite_link_depends}")
if(IDF_TARGET STREQUAL "esp32s31")
  target_link_options(squeezelite.elf PRIVATE "-Wl,--defsym=IDF_TARGET_ESP32S31=0")
endif()
set_target_properties(squeezelite.elf PROPERTIES LINK_LIBRARIES "${BCA};idf::app_squeezelite;-Wl,--Map=${BUILD_DIR}/squeezelite.map,--wrap=esp_panic_handler")
if(IDF_TARGET STREQUAL "esp32s31")
    idf_component_get_property(squeezelite_esptool esptool_py ESPTOOLPY_CMD)
    idf_component_get_property(squeezelite_image_args esptool_py ESPTOOL_PY_ELF2IMAGE_ARGS)
else()
    set(squeezelite_esptool ${ESPTOOLPY})
    set(squeezelite_image_args ${ESPTOOLPY_FLASH_OPTIONS} ${ESPTOOLPY_ELF2IMAGE_OPTIONS})
endif()
add_custom_command(
			TARGET squeezelite.elf
			POST_BUILD 
			COMMAND ${CMAKE_COMMAND} -E echo "Generating ${BUILD_DIR}/squeezelite.bin"
			COMMAND ${squeezelite_esptool} elf2image ${squeezelite_image_args} -o "squeezelite.bin" "squeezelite.elf"
	        VERBATIM
)

# create files with size for squeezelite
# build_size(squeezelite)
    
# make it part of cleanup    
set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" 
             APPEND PROPERTY 
             ADDITIONAL_MAKE_CLEAN_FILES "${BUILD_DIR}/squeezelite.elf" "${BUILD_DIR}/squeezelite.map"
)

# adding OTA_0 partition
partition_table_get_partition_info(otaapp_offset "--partition-type app --partition-subtype ota_0" "offset")
idf_component_get_property(main_args esptool_py FLASH_ARGS)
idf_component_get_property(sub_args esptool_py FLASH_SUB_ARGS)
esptool_py_flash_target(squeezelite-flash "${main_args}" "${sub_args}")
esptool_py_flash_target_image(squeezelite-flash squeezelite "${otaapp_offset}" "${BUILD_DIR}/squeezelite.bin")
esptool_py_flash_target_image(flash squeezelite "${otaapp_offset}" "${BUILD_DIR}/squeezelite.bin")

# and JTAG scripts
add_custom_target(_jtag_scripts  ALL
					BYPRODUCTS "flash_dbg_project_args"  
					POST_BUILD
					COMMAND ${CMAKE_COMMAND} -P "${CMAKE_CURRENT_SOURCE_DIR}/generate_debug_scripts.cmake"
)

if(CMAKE_HOST_UNIX)
  # Add custom target to set executable permissions before build for cspot component
    add_custom_target(set_cspot_permissions
        COMMAND ${CMAKE_COMMAND} -E echo "************************************************************************************************"
        COMMAND ${CMAKE_COMMAND} -E echo "**** Setting permissions for required files"
        COMMAND ${CMAKE_COMMAND} -E echo "**** ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/protoc-gen-nanopb"
        COMMAND ${CMAKE_COMMAND} -E echo "**** ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/*.py"
        COMMAND ${CMAKE_COMMAND} -E echo "**** ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/*.py2"
        COMMAND ${CMAKE_COMMAND} -E echo "**** ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/proto/*.py"
        COMMAND ${CMAKE_COMMAND} -E echo "**** ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/protoc"
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/protoc-gen-nanopb
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/*.py
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/*.py2
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/proto/*.py
        COMMAND chmod +x ${CMAKE_SOURCE_DIR}/components/spotify/cspot/bell/external/nanopb/generator/protoc
        COMMAND ${CMAKE_COMMAND} -E echo "************************************************************************************************"    
    )

  # Add a dependency to ensure permissions are set before building cspot component
  add_dependencies(__idf_spotify set_cspot_permissions)
endif()

# ======================= DEBUG FLAGS ============================

#target_compile_definitions(__idf_esp_eth PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_INFO)

#target_compile_definitions(__idf_services PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

#target_compile_definitions(__idf_driver PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
#target_compile_definitions(__idf_wifi-manager PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
#target_compile_definitions(__idf_esp_wifi PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
#target_compile_definitions(__idf_platform_console PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

#target_compile_definitions(__idf_app_recovery PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_INFO)
# target_compile_definitions(__idf_esp_eth PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_INFO)
# target_compile_definitions(__idf_esp_event PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_INFO)
# target_compile_definitions(__idf_esp_netif PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

# target_compile_definitions(__idf_freertos PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
#target_compile_definitions(__idf_bt PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_ERROR)
# target_compile_definitions(__idf_mdns PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_tcpip_adapter PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_tcp_transport PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

#target_compile_definitions(__idf_app_squeezelite PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_app_trace PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_app_update PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_asio PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_audio PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_bootloader_support PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

# target_compile_definitions(__idf_cbor PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_cmock PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_coap PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_console PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_cxx PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_display PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_driver PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_driver_bt PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_efuse PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp-dsp PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp-tls PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp32 PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_espcoredump PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_adc_cal PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_common PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_gdbstub PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_hid PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_https_ota PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_http_client PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_http_server PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_hw_support PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_ipc PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_local_ctrl PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_pm PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_ringbuf PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_rom PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_serial_slave_link PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_system PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_timer PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_websocket_client PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_expat PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_fatfs PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_freemodbus PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_hal PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_heap PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_jsmn PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_json PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_libsodium PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_log PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_lwip PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_main PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_mbedtls PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(mbedcrypto PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(mbedtls PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(mbedx509 PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_mqtt PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_esp_libc PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_nghttp PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_nvs_flash PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_openssl PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_perfmon PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_platform_config PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_protobuf-c PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_protocomm PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_pthread PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_raop PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_sdmmc PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)

# target_compile_definitions(__idf_soc PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_spiffs PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_spi_flash PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_squeezelite PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_squeezelite-ota PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_telnet PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_tools PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_ulp PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_unity PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_vfs PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_wear_levelling PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_wifi_provisioning PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_wpa_supplicant PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
# target_compile_definitions(__idf_xtensa PRIVATE -DLOG_LOCAL_LEVEL=ESP_LOG_DEBUG)
