cmake_minimum_required(VERSION 3.5)
project(hanaru CXX)

if (WIN32)
    include(${CMAKE_TOOLCHAIN_FILE})
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME}
    controllers/beatmap_route.cc
    controllers/beatmap_route.hh
    controllers/beatmap_set_route.cc
    controllers/beatmap_set_route.hh
    controllers/download_route.cc
    controllers/download_route.hh
    controllers/subscribe_route.cc
    controllers/subscribe_route.hh
    impl/downloader.cc
    impl/downloader.hh
    impl/globals.hh
    impl/rate_limiter.cc
    impl/rate_limiter.hh
    impl/storage_manager.cc
    impl/storage_manager.hh
    main.cc
)

# Clang doesn't supported by drogon
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++20 -fcoroutines")
elseif (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /std:c++latest")
else ()
    message(WARNING "unknown compiler, trying to use default configuration for coroutines")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -fcoroutines")
endif ()

find_package(Drogon CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)
include_directories(${JSONCPP_INCLUDE_DIRS})

aux_source_directory(controllers CTL_SRC)

target_sources(${PROJECT_NAME}
               PRIVATE
               ${SRC_DIR}
               ${CTL_SRC})
