set(ctshell_srcs
        "${CMAKE_CURRENT_SOURCE_DIR}/ctshell.c"
)

set(ctshell_incs
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

if(CONFIG_CTSHELL_PORT_WINDOWS)
    list(APPEND ctshell_srcs
            "${CMAKE_CURRENT_SOURCE_DIR}/port/windows/ctshell_windows.c"
    )
    list(APPEND ctshell_incs
            "${CMAKE_CURRENT_SOURCE_DIR}/port/windows"
    )
endif()

if(CONFIG_CTSHELL_PORT_STM32)
    list(APPEND ctshell_srcs
            "${CMAKE_CURRENT_SOURCE_DIR}/port/stm32/ctshell_stm32.c"
    )
    list(APPEND ctshell_incs
            "${CMAKE_CURRENT_SOURCE_DIR}/port/stm32"
    )
endif()

if(CONFIG_CTSHELL_PORT_ESP32)
    list(APPEND ctshell_srcs
            "${CMAKE_CURRENT_SOURCE_DIR}/port/esp32/ctshell_esp32.c"
    )
    list(APPEND ctshell_incs
            "${CMAKE_CURRENT_SOURCE_DIR}/port/esp32"
    )
endif()

if(CONFIG_CTSHELL_USE_FS)
    if(CONFIG_CTSHELL_USE_FS_FATFS)
        list(APPEND ctshell_srcs
                "${CMAKE_CURRENT_SOURCE_DIR}/extension/fs/ctshell_fatfs.c"
        )
    endif()
endif()

set(ctshell_srcs ${ctshell_srcs} CACHE INTERNAL "ctshell source files")
set(ctshell_incs ${ctshell_incs} CACHE INTERNAL "ctshell include directories")

if(ESP_PLATFORM)
    message(STATUS "enable ctshell in esp-idf")

    idf_component_register(
            SRCS ${ctshell_srcs}
            INCLUDE_DIRS ${ctshell_incs}
            REQUIRES driver esp_timer freertos log
            LDFRAGMENTS "${CMAKE_CURRENT_SOURCE_DIR}/port/esp32/linker.lf"
    )

    target_link_libraries(${COMPONENT_LIB} INTERFACE "-T ${CMAKE_CURRENT_SOURCE_DIR}/port/esp32/ctshell_cmd.ld")
else()
    set(CTSHELL_DEFINITIONS
            CONFIG_CTSHELL_PORT_STM32=${CONFIG_CTSHELL_PORT_STM32}
            CONFIG_CTSHELL_PORT_ESP32=${CONFIG_CTSHELL_PORT_ESP32}
            CONFIG_CTSHELL_PORT_WINDOWS=${CONFIG_CTSHELL_PORT_WINDOWS}
            CONFIG_CTSHELL_USE_FS=${CONFIG_CTSHELL_USE_FS}
            CONFIG_CTSHELL_USE_FS_FATFS=${CONFIG_CTSHELL_USE_FS_FATFS}
            CONFIG_CTSHELL_USE_DOUBLE=${CONFIG_CTSHELL_USE_DOUBLE}
    )
    set(CTSHELL_DEFINITIONS ${CTSHELL_DEFINITIONS} CACHE INTERNAL "ctshell definitions")
endif()
