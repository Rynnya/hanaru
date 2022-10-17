cmake_minimum_required(VERSION 3.5)
project(hanaru CXX)

if (WIN32)
    include(${CMAKE_TOOLCHAIN_FILE})
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    add_compile_definitions(NOMINMAX)
endif()

add_executable(${PROJECT_NAME}
    src/controllers/beatmap_route.cc
    src/controllers/beatmap_route.hh
    src/controllers/beatmap_set_route.cc
    src/controllers/beatmap_set_route.hh
    src/controllers/download_route.cc
    src/controllers/download_route.hh
    src/impl/downloader.cc
    src/impl/downloader.hh
    src/impl/storage_manager.cc
    src/impl/storage_manager.hh
    src/impl/utils.cc
    src/impl/utils.hh
    src/thirdparty/curler.cc
    src/thirdparty/curler.hh
    src/main.cc
)

find_package(Drogon CONFIG REQUIRED)
find_package(CURL REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE Drogon::Drogon CURL::libcurl)
include_directories(${JSONCPP_INCLUDE_DIRS} ${CURL_INCLUDE_DIR})

aux_source_directory(controllers CTL_SRC)
target_sources(${PROJECT_NAME} PRIVATE ${SRC_DIR} ${CTL_SRC})

if (MSVC)
	file(GLOB_RECURSE _source_list *.hpp *.c* *.h*)
	foreach(_source IN ITEMS ${_source_list})
		get_filename_component(_source_path "${_source}" PATH)
		string(REPLACE "${CMAKE_SOURCE_DIR}" "" _group_path "${_source_path}")
		string(REPLACE "/" "\\" _group_path "${_group_path}")
		source_group("${_group_path}" FILES "${_source}")
	endforeach()
endif()