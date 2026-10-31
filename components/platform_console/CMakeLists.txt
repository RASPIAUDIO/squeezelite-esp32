idf_component_register( SRCS 
							cmd_i2ctools.c
							cmd_nvs.c
							cmd_ota.c
							cmd_system.c
							cmd_wifi.c
							platform_console.c
							improv_parser.c
							improv_serial.c
							cmd_config.c
						INCLUDE_DIRS .   
						REQUIRES nvs_flash bootloader_support esp_driver_uart esp_hw_support
						PRIV_REQUIRES console app_update tools services spi_flash  platform_config vfs pthread wifi-manager platform_config esp_libc  telnet display squeezelite tools)

set_source_files_properties(cmd_config.c
    PROPERTIES COMPILE_FLAGS
    -Wno-unused-function
)	

