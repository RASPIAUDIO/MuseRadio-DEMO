set(priv_requires esp_timer)

if(${IDF_VERSION_MAJOR} LESS 6)
    list(APPEND priv_requires usb)       # USB PHY is part of usb component in IDF < 6.0
endif()

set(uac_sources usb_device_uac.c)

if(NOT CONFIG_USB_DEVICE_UAC_AS_PART)
    list(APPEND uac_sources tusb/usb_descriptors.c)
endif()

idf_component_register(SRCS ${uac_sources}
                    INCLUDE_DIRS "include" "tusb" "tusb_uac"
                    PRIV_REQUIRES ${priv_requires})

if(NOT CONFIG_USB_DEVICE_UAC_AS_PART)
        # Determine whether tinyusb is fetched from component registry or from local path
    idf_build_get_property(build_components BUILD_COMPONENTS)
    if(tinyusb IN_LIST build_components)
        set(tinyusb_name tinyusb) # Local component
    else()
        set(tinyusb_name espressif__tinyusb) # Managed component
    endif()

    idf_component_get_property(tusb_lib ${tinyusb_name} COMPONENT_LIB)
    target_include_directories(${tusb_lib} PUBLIC "${COMPONENT_DIR}/tusb" "${COMPONENT_DIR}/tusb_uac")
endif()

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
