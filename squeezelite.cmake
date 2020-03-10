

if(NOT SDKCONFIG OR NOT IDF_PATH  OR NOT IDF_TARGET )
    message(FATAL_ERROR "squeezelite should not be made outside of the main project !")
endif()

function(___register_flash target_name sub_type)
	partition_table_get_partition_info(otaapp_offset "--partition-type app --partition-subtype ${sub_type}" "offset")
	esptool_py_flash_project_args(${target_name} ${otaapp_offset} ${build_dir}/${target_name}.bin FLASH_IN_PROJECT)
	esptool_py_custom_target(${target_name}-flash ${target_name} "${target_name}")
endfunction()

function(___create_new_target target_name)
	idf_build_get_property(build_dir BUILD_DIR)
	set(target_elf ${target_name}.elf)
	
	# Create a dummy file to work around CMake requirement of having a source
	# file while adding an executable
	set(target_elf_src ${CMAKE_BINARY_DIR}/${target_name}_src.c)
	add_custom_command(OUTPUT ${target_elf_src}
		COMMAND ${CMAKE_COMMAND} -E touch ${target_elf_src}
	    VERBATIM)
	
	
	add_custom_target(_${target_name}_elf DEPENDS "${target_elf_src}"  )
	add_executable(${target_elf} "${target_elf_src}")
	add_dependencies(${target_elf} _${target_name}_elf)
	add_dependencies(${target_elf} "recovery.elf")
	set_property(TARGET ${target_elf} PROPERTY RECOVERY_BUILD 0 )
	set_property(TARGET ${target_elf} PROPERTY RECOVERY_PREFIX app_${target_name})
	idf_build_get_property(bca BUILD_COMPONENT_ALIASES)            
	target_link_libraries(${target_elf} ${bca})
	set(target_name_mapfile "${target_name}.map")
	target_link_libraries(${target_elf} "-Wl,--cref -Wl,--Map=${CMAKE_BINARY_DIR}/${target_name_mapfile}")
	add_custom_command(
			TARGET ${target_elf}
			POST_BUILD 
	        COMMAND ${CMAKE_COMMAND} -E echo "Generated ${build_dir}/${target_name}.bin"
		    #COMMAND echo ${ESPTOOLPY} elf2image ${ESPTOOLPY_FLASH_OPTIONS} ${esptool_elf2image_args}
		    #COMMAND echo ${ESPTOOLPY} elf2image ${ESPTOOLPY_FLASH_OPTIONS} ${ESPTOOLPY_ELF2IMAGE_OPTIONS}
	        COMMAND ${ESPTOOLPY} elf2image ${ESPTOOLPY_FLASH_OPTIONS} ${ESPTOOLPY_ELF2IMAGE_OPTIONS}
	            -o "${build_dir}/${target_name}.bin" "${target_name}.elf"
	        DEPENDS "${target_name}.elf" 
	        WORKING_DIRECTORY ${build_dir}
	        COMMENT "Generating binary image from built executable"
	        VERBATIM
	)
	set_property(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" APPEND PROPERTY
        ADDITIONAL_MAKE_CLEAN_FILES
        "${build_dir}/${target_name_mapfile}" "${build_dir}/${target_elf_src}" )
        

endfunction()

___create_new_target(squeezelite )
___register_flash(squeezelite ota_0)