cmake_minimum_required(VERSION 3.24)

project(PassVault
    VERSION 1.0.0
    DESCRIPTION "Windows desktop companion for PassVault Android"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
    add_compile_definitions(UNICODE _UNICODE)
endif()

find_package(Qt6 6.5 COMPONENTS Core Widgets Network Svg REQUIRED)
find_package(OpenSSL 3.0 REQUIRED)

qt_standard_project_setup()

set(PASSVAULT_APPDATA_SUBDIR "PassVault" CACHE STRING "Subdirectory under %APPDATA% for local storage")

set(PASSVAULT_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${PASSVAULT_GENERATED_DIR}")

if(NOT DEFINED GOOGLE_DESKTOP_CLIENT_ID)
    set(GOOGLE_DESKTOP_CLIENT_ID "$ENV{GOOGLE_DESKTOP_CLIENT_ID}"
        CACHE STRING "Google OAuth desktop client ID")
endif()
if(NOT DEFINED GOOGLE_DESKTOP_CLIENT_SECRET)
    set(GOOGLE_DESKTOP_CLIENT_SECRET "$ENV{GOOGLE_DESKTOP_CLIENT_SECRET}"
        CACHE STRING "Google OAuth desktop client secret")
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/google_oauth_config.h.in"
    "${PASSVAULT_GENERATED_DIR}/passvault/oauth/google_oauth_config.h"
    @ONLY
)

enable_testing()

add_subdirectory(third_party/sqlite3)
add_subdirectory(src)
add_subdirectory(resources)
add_subdirectory(tests)
