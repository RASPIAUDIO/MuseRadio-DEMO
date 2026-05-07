if(NOT CMAKE_BINARY_DIR MATCHES "muse_radio_airplay_poc")
  idf_component_register(SRCS "airplay_dummy.c" INCLUDE_DIRS ".")
  return()
endif()

set(ALAC_DIR "${CMAKE_SOURCE_DIR}/third_party/cspot/cspot/bell/external/alac/codec")

idf_component_register(
  SRCS
    "raop.c"
    "rtp.c"
    "util.c"
    "dmap_parser.c"
    "log_util.c"
    "alac_wrapper.cpp"
    "${ALAC_DIR}/ALACDecoder.cpp"
    "${ALAC_DIR}/ALACBitUtilities.c"
    "${ALAC_DIR}/ag_dec.c"
    "${ALAC_DIR}/dp_dec.c"
    "${ALAC_DIR}/matrix_dec.c"
    "${ALAC_DIR}/EndianPortable.c"
  INCLUDE_DIRS
    "."
    "${ALAC_DIR}"
  PRIV_REQUIRES
    espressif__mdns
    mbedtls
    esp_netif
    esp_timer
    lwip
    pthread
)

target_compile_options(${COMPONENT_LIB} PRIVATE
  -Wno-multichar
  -Wno-unused-variable
  -Wno-unused-function
  -Wno-missing-field-initializers
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
  MBEDTLS_CONFIG_FILE=\"mbedtls/esp_config.h\"
)

target_include_directories(${COMPONENT_LIB} PRIVATE
  "$ENV{IDF_PATH}/components/esp_event/include"
  "$ENV{IDF_PATH}/components/esp_netif/include"
  "$ENV{IDF_PATH}/components/esp_timer/include"
  "$ENV{IDF_PATH}/components/mbedtls/port/include"
  "$ENV{IDF_PATH}/components/mbedtls/mbedtls/include"
  "$ENV{IDF_PATH}/components/pthread/include"
  "${CMAKE_SOURCE_DIR}/managed_components/espressif__mdns/include"
)

foreach(dep IN ITEMS espressif__mdns mbedtls esp_netif esp_timer lwip pthread)
  idf_component_get_property(dep_lib ${dep} COMPONENT_LIB)
  target_link_libraries(${COMPONENT_LIB} PRIVATE ${dep_lib})
endforeach()
