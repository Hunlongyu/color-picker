cmake_minimum_required(VERSION 3.28)
project(ColorPicker VERSION 0.3.0 LANGUAGES C RC)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR
   CMAKE_SYSTEM_PROCESSOR MATCHES "ARM|arm|aarch" OR CMAKE_GENERATOR_PLATFORM MATCHES "ARM|arm")
    message(FATAL_ERROR "Build Color Picker for Windows x64.")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(FetchContent)
set(CUI_WITH_APP OFF CACHE BOOL "" FORCE)
set(CUI_DEFAULT_FONTS OFF CACHE BOOL "" FORCE)
set(CUI_STB_IMAGE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(cui EXCLUDE_FROM_ALL
    URL https://codeload.github.com/yappologistic/CUI/zip/629b6fcb991d71527f2d67b25c51429c3dcdb498)
FetchContent_Declare(updater EXCLUDE_FROM_ALL
    URL https://codeload.github.com/Hunlongyu/updater/zip/b81aac54fe750247320ef82afc1d0ee4b3ce3162
    URL_HASH SHA256=82c52137f6b995057ed6d7c1f964d037ae215f93b09cf186c54676c006cb40d8)
FetchContent_MakeAvailable(cui updater)

configure_file(assets/app.rc.in resources.rc @ONLY)
file(GLOB LUCIDE_SVG CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/lucide/*.svg")
set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/resources.rc" PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.manifest;${CMAKE_CURRENT_SOURCE_DIR}/assets/app.ico;${LUCIDE_SVG}")
add_executable(color-picker WIN32 src/main.c src/state.c src/panel.c src/picker.c src/update.c src/icons.c
    "${CMAKE_CURRENT_BINARY_DIR}/resources.rc")
target_compile_definitions(color-picker PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00
    APP_VERSION="${PROJECT_VERSION}")
target_include_directories(color-picker PRIVATE "${cui_SOURCE_DIR}/src")
target_link_libraries(color-picker PRIVATE cui::cui winautoupdate comctl32 shell32 gdi32 user32 imm32 advapi32)
set_target_properties(color-picker PROPERTIES OUTPUT_NAME color-picker-x64)
if(MSVC)
    target_compile_definitions(color-picker PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(color-picker PRIVATE $<$<COMPILE_LANGUAGE:C>:/utf-8> /W4)
    target_link_options(color-picker PRIVATE /MANIFEST:NO)
else()
    target_compile_options(color-picker PRIVATE -Wall -Wextra)
    target_link_options(color-picker PRIVATE -static -municode -Wl,--gc-sections $<$<CONFIG:Release>:-s> $<$<CONFIG:Release>:-flto>)
endif()

option(COLOR_PICKER_BUILD_TESTS "Build the picker and offline updater/UI integration tests (MinGW)" OFF)
if(COLOR_PICKER_BUILD_TESTS)
    if(NOT MINGW)
        message(FATAL_ERROR "The offline test's HTTP wrappers require MinGW-w64.")
    endif()
    add_executable(update-flow tests/update-flow.c src/icons.c src/picker.c "${CMAKE_CURRENT_BINARY_DIR}/resources.rc")
    target_include_directories(update-flow PRIVATE src "${updater_SOURCE_DIR}/src" "${cui_SOURCE_DIR}/src")
    target_compile_definitions(update-flow PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00 APP_VERSION="${PROJECT_VERSION}")
    target_link_libraries(update-flow PRIVATE cui::cui winautoupdate comctl32 shell32 gdi32 user32 imm32 advapi32)
    target_link_options(update-flow PRIVATE -static -municode -Wl,--wrap=au_http_get_mem,--wrap=au_http_get_file)
    add_executable(update-payload tests/update-payload.c)
    target_link_libraries(update-payload PRIVATE winautoupdate)
    target_link_options(update-payload PRIVATE -static -municode)
    enable_testing()
    add_executable(picker-flow tests/picker-flow.c)
    target_compile_definitions(picker-flow PRIVATE UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    target_link_libraries(picker-flow PRIVATE cui::cui gdi32 user32 imm32)
    target_link_options(picker-flow PRIVATE -static -municode)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/picker-qa")
    add_test(NAME picker-flow COMMAND picker-flow "${CMAKE_CURRENT_BINARY_DIR}/picker-qa")
    set_tests_properties(picker-flow PROPERTIES TIMEOUT 60)
    add_test(NAME update-flow COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run-update-flow.ps1"
        -TestExe $<TARGET_FILE:update-flow> -PayloadExe $<TARGET_FILE:update-payload>
        -OutputDirectory "${CMAKE_CURRENT_BINARY_DIR}")
endif()
