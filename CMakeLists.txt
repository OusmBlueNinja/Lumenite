cmake_minimum_required(VERSION 3.16)
project(Lumenite LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
        jsoncpp
        GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
        GIT_TAG 1.9.5
)
FetchContent_MakeAvailable(jsoncpp)

FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_7_1 # latest stable tag as of mid-2025
)
FetchContent_MakeAvailable(curl)

set(BUILD_CURL_EXE OFF CACHE INTERNAL "")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(HTTP_ONLY ON CACHE INTERNAL "")

file(GLOB LUA_SRC CONFIGURE_DEPENDS vendor/lua/*.c)
add_library(lua_static STATIC ${LUA_SRC})
target_include_directories(lua_static PUBLIC vendor/lua)

add_executable(lumenite
        src/LumeniteApp.cpp
        src/LumeniteApp.h
        src/main.cpp
        src/Router.cpp
        src/Router.h
        src/Server.cpp
        src/Server.h
        src/TemplateEngine.cpp
        src/TemplateEngine.h
        src/SessionManager.cpp
        src/SessionManager.h
)

target_include_directories(lumenite PRIVATE vendor/lua)

target_link_libraries(lumenite PRIVATE
        lua_static
        jsoncpp_lib
        CURL::libcurl
        ws2_32
)

if (WIN32)
    target_link_libraries(lumenite PRIVATE iphlpapi)
endif()
