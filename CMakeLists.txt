set(ctshell_srcs
        "${CMAKE_CURRENT_SOURCE_DIR}/ctshell.c"
)

set(ctshell_incs
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

set(CTSHELL_DEFINITIONS "")

option(CONFIG_CTSHELL_ENABLE_FUZZING "Enable libFuzzer based fuzz testing" OFF)

if(ESP_PLATFORM)
    set(CONFIG_CTSHELL_PORT_ESP32 1)
endif()

if(CONFIG_CTSHELL_PORT_POSIX)
    list(APPEND ctshell_srcs "${CMAKE_CURRENT_SOURCE_DIR}/port/posix/ctshell_posix.c")
    list(APPEND ctshell_incs "${CMAKE_CURRENT_SOURCE_DIR}/port/posix")
    list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_PORT_POSIX=1")
endif()

if(CONFIG_CTSHELL_PORT_WINDOWS)
    list(APPEND ctshell_srcs "${CMAKE_CURRENT_SOURCE_DIR}/port/windows/ctshell_windows.c")
    list(APPEND ctshell_incs "${CMAKE_CURRENT_SOURCE_DIR}/port/windows")
    list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_PORT_WINDOWS=1")
endif()

if(CONFIG_CTSHELL_PORT_STM32)
    list(APPEND ctshell_srcs "${CMAKE_CURRENT_SOURCE_DIR}/port/stm32/ctshell_stm32.c")
    list(APPEND ctshell_incs "${CMAKE_CURRENT_SOURCE_DIR}/port/stm32")
    list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_PORT_STM32=1")
endif()

if(CONFIG_CTSHELL_PORT_ESP32)
    list(APPEND ctshell_srcs "${CMAKE_CURRENT_SOURCE_DIR}/port/esp32/ctshell_esp32.c")
    list(APPEND ctshell_incs "${CMAKE_CURRENT_SOURCE_DIR}/port/esp32")
    list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_PORT_ESP32=1")
endif()

if(CONFIG_CTSHELL_PORT_CHERRYUSB)
    list(APPEND ctshell_srcs "${CMAKE_CURRENT_SOURCE_DIR}/port/cherryusb/ctshell_cherryusb.c")
    list(APPEND ctshell_incs "${CMAKE_CURRENT_SOURCE_DIR}/port/cherryusb")
    list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_PORT_CHERRYUSB=1")
endif()

if(CONFIG_CTSHELL_USE_FS)
    list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_USE_FS=1")
    if(CONFIG_CTSHELL_USE_FS_FATFS)
        list(APPEND ctshell_srcs "${CMAKE_CURRENT_SOURCE_DIR}/extension/fs/ctshell_fs_fatfs.c")
        list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_USE_FS_FATFS=1")
    endif()
    if(CONFIG_CTSHELL_USE_FS_POSIX)
        list(APPEND ctshell_srcs "${CMAKE_CURRENT_SOURCE_DIR}/extension/fs/ctshell_fs_posix.c")
        list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_USE_FS_POSIX=1")
    endif()
    if(CONFIG_CTSHELL_USE_FS_LITTLEFS)
        list(APPEND ctshell_srcs "${CMAKE_CURRENT_SOURCE_DIR}/extension/fs/ctshell_fs_littlefs.c")
        list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_USE_FS_LITTLEFS=1")
    endif()
    if(CONFIG_CTSHELL_USE_FS_WINDOWS)
        list(APPEND ctshell_srcs "${CMAKE_CURRENT_SOURCE_DIR}/extension/fs/ctshell_fs_windows.c")
        list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_USE_FS_WINDOWS=1")
    endif()
    if(CONFIG_CTSHELL_USE_FS_ARDUINO_SD)
        list(APPEND ctshell_srcs "${CMAKE_CURRENT_SOURCE_DIR}/extension/fs/ctshell_fs_arduino_sd.cpp")
        list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_USE_FS_ARDUINO_SD=1")
    endif()
endif()

if(CONFIG_CTSHELL_USE_DOUBLE)
    list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_USE_DOUBLE=1")
endif()

set(ctshell_srcs ${ctshell_srcs} CACHE INTERNAL "ctshell source files")
set(ctshell_incs ${ctshell_incs} CACHE INTERNAL "ctshell include directories")

if(ESP_PLATFORM)
    message(STATUS "enable ctshell in esp-idf")

    idf_component_register(
            SRCS ${ctshell_srcs}
            INCLUDE_DIRS ${ctshell_incs}
            REQUIRES driver esp_timer freertos esp_driver_uart
            LDFRAGMENTS "${CMAKE_CURRENT_SOURCE_DIR}/port/esp32/linker.lf"
    )

    target_link_libraries(${COMPONENT_LIB} INTERFACE "-T ${CMAKE_CURRENT_SOURCE_DIR}/port/esp32/ctshell_cmd.ld")
else()
    if(CONFIG_CTSHELL_ENABLE_FUZZING)
        list(APPEND CTSHELL_DEFINITIONS "CONFIG_CTSHELL_ENABLE_FUZZING=1")
        if(CMAKE_C_COMPILER_ID MATCHES "Clang")
            set(CTSHELL_FUZZ_TARGET ctshell_fuzz)
            set(CTSHELL_FUZZ_SRC "${CMAKE_CURRENT_SOURCE_DIR}/test/fuzz/ctshell_fuzz.c")
            add_executable(${CTSHELL_FUZZ_TARGET}
                    ${CTSHELL_FUZZ_SRC}
                    ${ctshell_srcs}
            )
            target_include_directories(${CTSHELL_FUZZ_TARGET} PRIVATE ${ctshell_incs})
            target_compile_definitions(${CTSHELL_FUZZ_TARGET} PRIVATE ${CTSHELL_DEFINITIONS})
            target_compile_options(${CTSHELL_FUZZ_TARGET} PRIVATE
                    -fsanitize=fuzzer,address
                    -fprofile-instr-generate
                    -fcoverage-mapping
                    -g
                    -O1
                    -mllvm
                    -asan-globals=0
            )
            target_link_options(${CTSHELL_FUZZ_TARGET} PRIVATE
                    -fsanitize=fuzzer,address
                    -fprofile-instr-generate
            )
        else()
            message(FATAL_ERROR "Fuzzing requires Clang compiler. Run with: CC=clang cmake ..")
        endif()
    endif()

    set(CTSHELL_DEFINITIONS ${CTSHELL_DEFINITIONS} CACHE INTERNAL "ctshell definitions")
endif()

# --- Linker options for section-based command registration ---
# GCC (ld.bfd) handles __start_/__stop_ symbols for orphan sections automatically.
# LLVM lld requires -z nostart-stop-gc to prevent these symbols from being
# discarded when --gc-sections is enabled.
set(CTSHELL_LINK_OPTIONS "")
if(CMAKE_LINKER MATCHES "lld" OR CMAKE_C_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    list(APPEND CTSHELL_LINK_OPTIONS "-z" "nostart-stop-gc")
endif()
set(CTSHELL_LINK_OPTIONS ${CTSHELL_LINK_OPTIONS} CACHE INTERNAL "ctshell linker options")
