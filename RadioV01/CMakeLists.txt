# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/RadioV01/*.*)

idf_component_register(
  SRCS ${app_sources}
  INCLUDE_DIRS "."
  REQUIRES cspot_component muse_airplay_raop espressif__usb_device_uac
)

if(TARGET generate_proto_sources)
  add_dependencies(${COMPONENT_LIB} generate_proto_sources)
endif()

target_compile_options(${COMPONENT_LIB} PRIVATE -fexceptions)
