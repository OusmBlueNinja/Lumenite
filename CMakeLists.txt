cmake_minimum_required(VERSION 3.16)
project(Lumenite LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# --- Fetch JsonCpp ---
FetchContent_Declare(
        jsoncpp
        GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
        GIT_TAG 1.9.5
)
FetchContent_MakeAvailable(jsoncpp)

# --- Lua ---
file(GLOB LUA_SRC CONFIGURE_DEPENDS vendor/lua/*.c)
add_library(lua_static STATIC ${LUA_SRC})
target_include_directories(lua_static PUBLIC vendor/lua)

# --- Lumenite App ---
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
target_link_libraries(lumenite PRIVATE lua_static jsoncpp_lib ws2_32)
if (WIN32)
    target_link_libraries(lumenite PRIVATE iphlpapi)
endif ()
