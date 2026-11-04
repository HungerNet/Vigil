cmake_minimum_required(VERSION 3.14)

project(FrenchifyLunar)

find_package(JNI REQUIRED)
include_directories(${JNI_INCLUDE_DIRS})
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

FetchContent_Declare(
    curl
    URL https://github.com/curl/curl/releases/download/curl-8_12_1/curl-8.12.1.tar.xz
    URL_HASH SHA256=0341f1ed97a26c811abaebd37d62b833956792b7607ea3f15d001613c76de202
)

set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CURL_STATICLIB ON CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
set(CMAKE_USE_SCHANNEL ON CACHE BOOL "" FORCE)
set(CMAKE_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TELNET ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_DICT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FILE ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_TFTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_FTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_RTSP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_POP3 ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_IMAP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_SMTP ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_GOPHER ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_MQTT ON CACHE BOOL "" FORCE)
set(CURL_DISABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
set(CURL_USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(curl)

add_library(ChatReaderLunar SHARED
    src/dllmain.cpp
    src/utils.cpp
    src/jni_helpers.cpp
    src/class_finder.cpp
    src/class_finder_log.cpp
    src/anticheat.cpp
    src/colored_hitboxes.cpp
    src/tab_overlay.cpp
    src/player_api.cpp
    src/denick.cpp
    src/number_denicker.cpp
    src/session_stats.cpp
    src/party_queue_detector.cpp
    src/trade_indicator.cpp
    src/hud_edit.cpp
    src/lunar_runtime.cpp
)

target_compile_definitions(ChatReaderLunar PRIVATE CURL_STATICLIB FRENCHIFY_LUNAR)
target_link_libraries(ChatReaderLunar PRIVATE ${JNI_LIBRARIES} CURL::libcurl opengl32)
target_include_directories(ChatReaderLunar PRIVATE ${curl_SOURCE_DIR}/include)

add_custom_command(TARGET ChatReaderLunar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ChatReaderLunar>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/known-nicks.json"
        "$<TARGET_FILE_DIR:ChatReaderLunar>/config/known-nicks.json"
    COMMENT "Copy known-nicks.json beside ChatReaderLunar.dll"
)

add_executable(InjectorLunar src/injector.cpp)
target_compile_definitions(InjectorLunar PRIVATE FRENCHIFY_LUNAR_INJECTOR)
