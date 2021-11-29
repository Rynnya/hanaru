cmake_minimum_required(VERSION 3.5)
project(hanaru CXX)

include(CheckIncludeFileCXX)

option(HANARU_CACHE "Enables cache system to reduce disk usage" ON)

check_include_file_cxx(any HAS_ANY)
check_include_file_cxx(string_view HAS_STRING_VIEW)
check_include_file_cxx(coroutine HAS_COROUTINE)
if (NOT "${CMAKE_CXX_STANDARD}" STREQUAL "")
    # Do nothing
elseif (HAS_ANY AND HAS_STRING_VIEW AND HAS_COROUTINE)
    set(CMAKE_CXX_STANDARD 20)
elseif (HAS_ANY AND HAS_STRING_VIEW)
    set(CMAKE_CXX_STANDARD 17)
else ()
    set(CMAKE_CXX_STANDARD 14)
endif ()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME}
    controllers/beatmap_route.cc
    controllers/beatmap_route.hh
    controllers/beatmap_set_route.cc
    controllers/beatmap_set_route.hh
    controllers/download_route.cc
    controllers/download_route.hh
    impl/downloader.cc
    impl/downloader.hh
    impl/globals.hh
    impl/memory_system.cc
    impl/memory_system.hh
    impl/rate_limiter.cc
    impl/rate_limiter.hh
    main.cc
)

if (HANARU_CACHE)
    add_definitions(-DHANARU_CACHE)
    message("-- compiles with cache system")
else ()
    message("-- compiles without cache system!")
endif ()

find_package(Drogon CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon)

if (CMAKE_CXX_STANDARD LESS 17)
    # With C++14, use boost to support any, string_view and filesystem
    message(STATUS "use c++14")
    find_package(Boost 1.61.0 REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC Boost::boost)
elseif (CMAKE_CXX_STANDARD LESS 20)
    message(STATUS "use c++17")
else ()
    message(STATUS "use c++20")
endif ()

aux_source_directory(controllers CTL_SRC)

target_sources(${PROJECT_NAME}
               PRIVATE
               ${SRC_DIR}
               ${CTL_SRC})
