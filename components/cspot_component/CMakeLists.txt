cmake_minimum_required(VERSION 3.16)

idf_component_register(
  SRCS "cspot_component.c"
  INCLUDE_DIRS "." "generated"
)

option(BUILD_SHARED_LIBS OFF)
option(BUILD_TESTING OFF)
option(BELL_DISABLE_TESTS "Disable bell tests" ON)
option(BELL_DISABLE_MQTT "Disable bell MQTT wrapper" ON)
option(BELL_DISABLE_WEBSERVER "Disable bell web server" OFF)
option(BELL_DISABLE_CODECS "Disable bell codecs" ON)
option(BELL_CODEC_AAC "Support AAC codec" OFF)
option(BELL_CODEC_MP3 "Support MP3 codec" OFF)
option(BELL_CODEC_VORBIS "Support Vorbis codec" ON)
option(BELL_CODEC_ALAC "Support ALAC codec" OFF)
option(BELL_CODEC_OPUS "Support Opus codec" OFF)
option(BELL_DISABLE_SINKS "Disable built-in sinks" ON)
option(BELL_ONLY_CJSON "Use cJSON instead of Nlohmann JSON" ON)
option(BELL_DISABLE_FMT "Disable fmt" ON)
option(BELL_DISABLE_REGEX "Disable regex" ON)

set(BELL_DISABLE_CODECS ON CACHE BOOL "Disable bell codecs" FORCE)
set(BELL_CODEC_AAC OFF CACHE BOOL "Support AAC codec" FORCE)
set(BELL_CODEC_MP3 OFF CACHE BOOL "Support MP3 codec" FORCE)
set(BELL_CODEC_ALAC OFF CACHE BOOL "Support ALAC codec" FORCE)
set(BELL_CODEC_OPUS OFF CACHE BOOL "Support Opus codec" FORCE)
set(BELL_CODEC_VORBIS ON CACHE BOOL "Support Vorbis codec" FORCE)

set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cspot/protobuf")
file(
  GLOB CSPOT_GENERATED_PROTO_FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/generated/protobuf/*.pb.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/generated/protobuf/*.pb.h"
)
foreach(proto_file ${CSPOT_GENERATED_PROTO_FILES})
  get_filename_component(proto_name "${proto_file}" NAME)
  configure_file(
    "${proto_file}"
    "${CMAKE_CURRENT_BINARY_DIR}/cspot/protobuf/${proto_name}"
    COPYONLY
  )
endforeach()

add_subdirectory(
  "${CMAKE_SOURCE_DIR}/third_party/cspot/cspot"
  "${CMAKE_CURRENT_BINARY_DIR}/cspot"
)

target_link_libraries(${COMPONENT_LIB} PUBLIC cspot)
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++20)
add_dependencies(${COMPONENT_LIB} generate_proto_sources)

if(TARGET cspot)
  target_include_directories(cspot PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/generated")
  add_dependencies(cspot generate_proto_sources)
  target_compile_options(cspot PRIVATE -fexceptions)
endif()

if(TARGET bell)
  target_compile_options(bell PRIVATE -fexceptions)
  target_compile_options(bell PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-include>" "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/bell_compat.hpp>")
endif()
