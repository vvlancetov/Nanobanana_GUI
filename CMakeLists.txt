cmake_minimum_required(VERSION 3.20)

project(Nanobanana_GUI VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# SFML 3.x is required. Pass -DSFML_DIR=<SFML>/lib/cmake/SFML, or set the
# SFML_DIR environment variable, or install SFML to a location CMake finds.
if(NOT SFML_DIR AND DEFINED ENV{SFML_DIR})
    set(SFML_DIR "$ENV{SFML_DIR}")
endif()

find_package(SFML 3 REQUIRED COMPONENTS Graphics Window System Network)

# Runtime DLLs are copied next to the executable after the build.
# Derived from the SFML package location unless overridden explicitly.
if(NOT SFML_BIN_DIR)
    get_filename_component(SFML_BIN_DIR "${SFML_DIR}/../../../bin" ABSOLUTE)
endif()
set(SFML_BIN_DIR "${SFML_BIN_DIR}" CACHE PATH "SFML runtime directory")

if(NOT EXISTS "${SFML_BIN_DIR}")
    message(WARNING "SFML_BIN_DIR does not exist: ${SFML_BIN_DIR}. "
                    "SFML DLLs will not be copied; pass -DSFML_BIN_DIR=<SFML>/bin")
endif()

add_executable(Nanobanana_GUI WIN32
    src/main.cpp
    src/config.cpp
    src/json.cpp
    src/base64.cpp
    src/net.cpp
    src/generator.cpp
    src/preview.cpp
    src/util.cpp
    rc/app.rc
)

target_include_directories(Nanobanana_GUI PRIVATE src)

target_link_libraries(Nanobanana_GUI PRIVATE
    SFML::Graphics
    SFML::Window
    SFML::System
    SFML::Network
    shell32
    ole32
    comdlg32
)

target_compile_definitions(Nanobanana_GUI PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(Nanobanana_GUI PRIVATE /utf-8 /W4 /permissive-)
    target_link_options(Nanobanana_GUI PRIVATE
        "/MANIFESTDEPENDENCY:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'")
endif()

foreach(lib graphics window system network)
    add_custom_command(TARGET Nanobanana_GUI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SFML_BIN_DIR}/sfml-${lib}$<IF:$<CONFIG:Debug>,-d,>-3.dll"
            "$<TARGET_FILE_DIR:Nanobanana_GUI>"
        VERBATIM)
endforeach()

source_group("src" FILES src/main.cpp src/config.cpp src/json.cpp src/base64.cpp src/net.cpp src/generator.cpp src/preview.cpp src/util.cpp)
source_group("rc" FILES rc/app.rc rc/resource.h)
