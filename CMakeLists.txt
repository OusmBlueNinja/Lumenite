cmake_minimum_required(VERSION 3.16)
project(Lumenite LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ----------------- jsoncpp (STATIC) -----------------
FetchContent_Declare(
        jsoncpp
        GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
        GIT_TAG 1.9.5
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "Disable jsoncpp tests" FORCE)
FetchContent_MakeAvailable(jsoncpp)

# ----------------- curl (STATIC) -----------------
FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_7_1
)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "Don't build curl executable" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Don't build tests" FORCE)
set(HTTP_ONLY ON CACHE BOOL "HTTP only" FORCE)
set(CURL_USE_LIBIDN2 OFF CACHE INTERNAL "")
set(CURL_USE_LIBIDN OFF CACHE INTERNAL "")
set(CURL_USE_WIN32_IDN OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(curl)

# ----------------- sqlite3 (STATIC) -----------------
FetchContent_Declare(
        sqlite3
        URL https://www.sqlite.org/2024/sqlite-amalgamation-3450100.zip
)
FetchContent_MakeAvailable(sqlite3)

add_library(sqlite3 STATIC
        ${sqlite3_SOURCE_DIR}/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})

# ----------------- Lua -----------------
file(GLOB LUA_SRC CONFIGURE_DEPENDS vendor/lua/*.c)
add_library(lua_static STATIC ${LUA_SRC})
target_include_directories(lua_static PUBLIC vendor/lua)

# ----------------- OpenSSL -----------------
if (MINGW)
    set(OPENSSL_ROOT_DIR "C:/msys64/mingw64")
    set(OPENSSL_USE_STATIC_LIBS TRUE)
endif ()
find_package(OpenSSL REQUIRED)

# ----------------- Lumenite Executable -----------------
add_executable(lumenite
        src/main.cpp
        src/LumeniteApp.cpp src/LumeniteApp.h
        src/Router.cpp src/Router.h
        src/Server.cpp src/Server.h
        src/TemplateEngine.cpp src/TemplateEngine.h
        src/SessionManager.cpp src/SessionManager.h
        src/ErrorHandler.cpp src/ErrorHandler.h
        src/modules/LumeniteDb.cpp src/modules/LumeniteDb.h
        src/modules/LumeniteCrypto.cpp src/modules/LumeniteCrypto.h
        src/modules/LumeniteSafe.cpp src/modules/LumeniteSafe.h
        src/utils/ProjectScaffolder.cpp src/utils/ProjectScaffolder.h
        src/utils/Version.h
        src/utils/MimeDetector.cpp
        src/utils/MimeDetector.h
)

target_include_directories(lumenite PRIVATE
        vendor/lua
        ${OPENSSL_INCLUDE_DIR}
        ${sqlite3_SOURCE_DIR}
)

target_link_libraries(lumenite PRIVATE
        lua_static
        jsoncpp_static
        CURL::libcurl
        sqlite3
        OpenSSL::SSL
        OpenSSL::Crypto

)

if (MINGW)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++ -static -pthread")
endif ()

if (WIN32)
    target_link_libraries(lumenite PRIVATE ws2_32 iphlpapi)
endif ()
