cmake_minimum_required(VERSION 3.25.0 FATAL_ERROR)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(PROJECT AmneziaVPN)
if(CLIENT_APPLICATION_NAME STREQUAL "Hop")
    if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(HOP_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/platforms/android/VERSION")
    else()
        set(HOP_VERSION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/hop/VERSION")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${HOP_VERSION_FILE}")
    file(STRINGS "${HOP_VERSION_FILE}" HOP_VERSION LIMIT_COUNT 1)
    set(AMNEZIAVPN_VERSION "${HOP_VERSION}" CACHE STRING "Client app version" FORCE)
elseif(NOT AMNEZIAVPN_VERSION)
    set(AMNEZIAVPN_VERSION 5.0.3.1 CACHE STRING "Client app version")
endif()

set(QT_CREATOR_SKIP_PACKAGE_MANAGER_SETUP ON CACHE BOOL "" FORCE)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES
    ${CMAKE_SOURCE_DIR}/cmake/platform_settings.cmake
    ${CMAKE_SOURCE_DIR}/cmake/recipes_bootstrap.cmake
    ${CMAKE_SOURCE_DIR}/cmake/conan_provider.cmake
    CACHE STRING "" FORCE)

project(${PROJECT} VERSION ${AMNEZIAVPN_VERSION}
        DESCRIPTION "AmneziaVPN"
        HOMEPAGE_URL "https://amnezia.org/"
)

option(AMNEZIA_BUILD_TESTS "Build test executables" OFF)

# trigger conan to kick off `conan install` globally
find_package(OpenSSL REQUIRED)
if (PREBUILTS_ONLY)
    return()
endif()

string(TIMESTAMP CURRENT_DATE "%Y-%m-%d")
set(RELEASE_DATE "${CURRENT_DATE}")

set(APP_MAJOR_VERSION ${CMAKE_PROJECT_VERSION_MAJOR}.${CMAKE_PROJECT_VERSION_MINOR}.${CMAKE_PROJECT_VERSION_PATCH})

# bump by 2 on every release, because we're releasing two versions on the play store
set(APP_ANDROID_VERSION_CODE 2171)
if(CLIENT_APPLICATION_NAME STREQUAL "Hop")
    file(READ "${CMAKE_SOURCE_DIR}/platforms/android/VERSION_CODE" APP_ANDROID_VERSION_CODE)
    string(STRIP "${APP_ANDROID_VERSION_CODE}" APP_ANDROID_VERSION_CODE)
endif()
if(DEFINED APP_ANDROID_VERSION_CODE_OFFSET)
    math(EXPR APP_ANDROID_VERSION_CODE "${APP_ANDROID_VERSION_CODE} + ${APP_ANDROID_VERSION_CODE_OFFSET}")
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    set(MZ_PLATFORM_NAME "linux")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    set(MZ_PLATFORM_NAME "windows")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    set(MZ_PLATFORM_NAME "macos")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Android")
    set(MZ_PLATFORM_NAME "android")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "iOS")
    set(MZ_PLATFORM_NAME "ios")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Emscripten")
    set(MZ_PLATFORM_NAME "wasm")
endif()

set(QT_BUILD_TOOLS_WHEN_CROSS_COMPILING ON)

if(APPLE AND NOT IOS)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(AMN_PF_RULE_IDENTITY "user { root }")
    else()
        set(AMN_PF_RULE_IDENTITY "group { amnvpn }")
    endif()

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/deploy/data/pf-templates/amn.400.allowPIA.conf.in"
        "${CMAKE_CURRENT_BINARY_DIR}/amn.400.allowPIA.conf"
        @ONLY
    )

    file(COPY_FILE
        "${CMAKE_CURRENT_BINARY_DIR}/amn.400.allowPIA.conf"
        "${CMAKE_CURRENT_SOURCE_DIR}/deploy/data/macos/pf/amn.400.allowPIA.conf"
        ONLY_IF_DIFFERENT
    )
endif()


if(ANDROID)
    add_subdirectory(platforms/android)
elseif(WIN32)
    add_subdirectory(platforms/windows)
else()
    add_subdirectory(shared/client client)
    if(NOT IOS AND NOT MACOS_NE)
        add_subdirectory(shared/service service)
    endif()
endif()

if ((LINUX AND NOT ANDROID) OR (APPLE AND NOT IOS AND NOT MACOS_NE) OR (WIN32))
    include(${CMAKE_SOURCE_DIR}/cmake/CPack.cmake)
endif()
