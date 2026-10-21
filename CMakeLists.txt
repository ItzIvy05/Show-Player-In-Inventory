cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED ENV{COMMONLIB_SSE_FOLDER})
    message(FATAL_ERROR "Missing COMMONLIB_SSE_FOLDER environment variable")
endif()

set(AUTHOR_NAME "Ivy")
set(PRODUCT_NAME "ShowPlayerInInventory")
set(PLUGIN_OUTPUT_NAME "ShowPlayerInInventory")
project(${PRODUCT_NAME} VERSION 2.2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(REX_OPTION_INI ON CACHE BOOL "" FORCE)

set(COMMONLIB_SOURCE_DIR "$ENV{COMMONLIB_SSE_FOLDER}")
add_subdirectory("${COMMONLIB_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/clib-build")
get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)
include("${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake")

find_path(FLICK_API_INCLUDE_DIRS "FUCK_API.h")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)

set(IVY_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EventProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/MenuCamera.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Settings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SettingsUI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ViewSwitchFix.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
)

set(IVY_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/EventProcessor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/MenuCamera.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PCH.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/Settings.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SettingsUI.h"
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${IVY_HEADERS} ${IVY_SOURCES})

add_commonlibsse_plugin(${PROJECT_NAME} AUTHOR ${AUTHOR_NAME} SOURCES ${IVY_HEADERS} ${IVY_SOURCES})

target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/PCH.h")

target_include_directories(${PROJECT_NAME} PRIVATE ${FLICK_API_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_SOURCE_DIR}/API")

set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PLUGIN_OUTPUT_NAME}")

function(copy_plugin_output target_folder)
    set(DLL_FOLDER "${target_folder}/SKSE/Plugins")
    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")
    
    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>" VERBATIM)
    endif()
endfunction()

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    copy_plugin_output("$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()
