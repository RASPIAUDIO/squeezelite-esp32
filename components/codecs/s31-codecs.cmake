# Rebuild codecs for RISC-V; the upstream archives in lib/ are Xtensa-only.
set(v "${CMAKE_CURRENT_LIST_DIR}/../../vendor")
idf_component_register(INCLUDE_DIRS . portable
    "${v}/mad" "${v}/helix-aac" "${v}/addons"
    "${v}/tremor" "${v}/ogg/include" "${v}/flac/include"
    "${v}/opus/include" "${v}/soxr/src"
    inc inc/alac inc/resample16
    REQUIRES esp_libc)
target_compile_definitions(${COMPONENT_LIB} INTERFACE FPM_64BIT SIZEOF_INT=4 SIZEOF_LONG=4 SIZEOF_LONG_LONG=8)

# Keep the full libmad PCM/frame ABI expected by Squeezelite.
# Compile portable 64-bit fixed point C; measure stack use before hardware tests.
set(mad_sources version fixed bit timer stream frame synth decoder layer12 layer3 huffman)
list(TRANSFORM mad_sources PREPEND "${v}/mad/")
list(TRANSFORM mad_sources APPEND ".c")
add_library(s31_mad STATIC ${mad_sources})
target_include_directories(s31_mad PRIVATE "${v}/mad" "${CMAKE_CURRENT_LIST_DIR}/portable")
target_compile_definitions(s31_mad PRIVATE FPM_64BIT SIZEOF_INT=4 SIZEOF_LONG=4 SIZEOF_LONG_LONG=8
    HAVE_ASSERT_H HAVE_ERRNO_H HAVE_LIMITS_H HAVE_STDLIB_H HAVE_STRING_H HAVE_UNISTD_H)
target_compile_options(s31_mad PRIVATE -fstack-usage)

file(GLOB aac_sources "${v}/helix-aac/*.c")
add_library(s31_aac STATIC ${aac_sources})
target_include_directories(s31_aac PRIVATE "${v}/helix-aac" "${CMAKE_CURRENT_LIST_DIR}/portable")
target_compile_definitions(s31_aac PRIVATE AAC_ENABLE_SBR USE_DEFAULT_STDLIB)

# Native CMake builds are pinned by vendor/SOURCES.json; programs/tests and
# host SIMD are intentionally omitted for this embedded target.
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_PACKAGE_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(INSTALL_PKGCONFIG_MODULES OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory("${v}/ogg" "${CMAKE_CURRENT_BINARY_DIR}/ogg")
set(BUILD_CXXLIBS OFF CACHE BOOL "" FORCE)
set(WITH_OGG ON CACHE BOOL "" FORCE)
set(WITH_ASM OFF CACHE BOOL "" FORCE)
set(ENABLE_MULTITHREADING OFF CACHE BOOL "" FORCE)
add_subdirectory("${v}/flac" "${CMAKE_CURRENT_BINARY_DIR}/flac" EXCLUDE_FROM_ALL)
# IDF appends -Os after the upstream release -O3. Keep the decoder optimized
# for throughput on S31, especially for the experimental 24/384 path.
target_compile_options(FLAC PRIVATE -Wno-error=inline -O3)

set(tremor_sources mdct block window synthesis info floor1 floor0 vorbisfile
    res012 mapping0 registry codebook sharedbook)
list(TRANSFORM tremor_sources PREPEND "${v}/tremor/")
list(TRANSFORM tremor_sources APPEND ".c")
add_library(s31_tremor STATIC ${tremor_sources})
target_compile_definitions(s31_tremor PRIVATE VAR_ARRAYS)
target_include_directories(s31_tremor PUBLIC "${v}/tremor")
target_link_libraries(s31_tremor PUBLIC ogg)

set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(OPUS_FIXED_POINT ON CACHE BOOL "" FORCE)
set(OPUS_FLOAT_API OFF CACHE BOOL "" FORCE)
add_subdirectory("${v}/opus" "${CMAKE_CURRENT_BINARY_DIR}/opus")

file(GLOB alac_sources "${v}/alac/codec/*.c" "${v}/alac/codec/*.cpp")
add_library(s31_alac STATIC ${alac_sources} "${v}/addons/alac_wrapper.cpp")
target_compile_options(s31_alac PRIVATE -Wno-multichar)
target_include_directories(s31_alac PRIVATE "${v}/alac/codec" "${v}/addons")

set(WITH_OPENMP OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WITH_DEV_TRACE OFF CACHE BOOL "" FORCE)
set(WITH_LSR_BINDINGS OFF CACHE BOOL "" FORCE)
set(WITH_CR32 ON CACHE BOOL "" FORCE)
set(WITH_CR64 OFF CACHE BOOL "" FORCE)
set(WITH_CR32S OFF CACHE BOOL "" FORCE)
set(WITH_CR64S OFF CACHE BOOL "" FORCE)
set(WITH_VR32 ON CACHE BOOL "" FORCE)
set(WITH_SIMD OFF CACHE BOOL "" FORCE)
add_subdirectory("${v}/soxr" "${CMAKE_CURRENT_BINARY_DIR}/soxr")
# The IDF libc headers intentionally use GCC include_next and optional macros.
target_compile_options(soxr PRIVATE -Wno-error=pedantic -Wno-error=undef
    -Wno-error=unknown-pragmas -O3)
# Retain the HQ/CR32 algorithm; speed optimization leaves decode headroom at 192 kHz.
target_link_libraries(${COMPONENT_LIB} INTERFACE
    s31_mad s31_aac FLAC s31_tremor ogg opus s31_alac soxr)
