cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED ENV{COMMONLIB_SSE_FOLDER})
    message(FATAL_ERROR "Missing COMMONLIB_SSE_FOLDER environment variable")
endif()

set(AUTHOR_NAME "Ivy")
set(PRODUCT_NAME "ShowPlayerInInventory")
set(PLUGIN_OUTPUT_NAME "ShowPlayerInInventory")
project(${PRODUCT_NAME} VERSION 1.0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)

set(ENABLE_SKYRIM_SE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_AE ON CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

set(COMMONLIB_SOURCE_DIR "$ENV{COMMONLIB_SSE_FOLDER}")
add_subdirectory("${COMMONLIB_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/clib-build")
get_target_property(COMMONLIB_SRC_DIR CommonLibSSE SOURCE_DIR)
include("${COMMONLIB_SRC_DIR}/cmake/CommonLibSSE.cmake")

find_path(CLIB_UTIL_INCLUDE_DIRS "ClibUtil/utils.hpp")
find_path(CLIB_UTILS_QTR_INCLUDE_DIRS "ClibUtilsQTR/utils.hpp")
find_path(SKSE_MCP_INCLUDE_DIRS "SKSEMCP/utils.hpp")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    @ONLY
)

set(IVY_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/EventProcessor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/MenuCamera.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Settings.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/SettingsUI.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/API/APIManager.cpp"
    "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
)

set(IVY_HEADERS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/EventProcessor.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/MenuCamera.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/PCH.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/Settings.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SettingsUI.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/logger.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/SKSEMenuFramework.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/API/APIManager.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/API/SmoothCamAPI.h"
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${IVY_HEADERS} ${IVY_SOURCES})

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR AUTHOR_NAME
    SOURCES ${IVY_HEADERS} ${IVY_SOURCES}
)

get_target_property(IVY_TARGET_SOURCES ${PROJECT_NAME} SOURCES)
if(IVY_TARGET_SOURCES)
    list(FILTER IVY_TARGET_SOURCES EXCLUDE REGEX "SkyrimWaterNeedsSKSE|Keyhandler\\.cpp|WaterNeeds|TearsOfKyne|Tears")
    set_property(TARGET ${PROJECT_NAME} PROPERTY SOURCES "${IVY_TARGET_SOURCES}")
endif()

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include/PCH.h")

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
        ${CLIB_UTIL_INCLUDE_DIRS}
        ${CLIB_UTILS_QTR_INCLUDE_DIRS}
        ${SKSE_MCP_INCLUDE_DIRS}
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/API"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    IS_HOST_PLUGIN
    _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    _CRT_SECURE_NO_WARNINGS
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PLUGIN_OUTPUT_NAME}")

function(copy_plugin_output target_folder)
    set(DLL_FOLDER "${target_folder}/SKSE/Plugins")
    message(STATUS "SKSE plugin output folder: ${DLL_FOLDER}")

    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DLL_FOLDER}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/package/Data/SKSE/Plugins/ShowPlayerInInventory.ini" "${DLL_FOLDER}/ShowPlayerInInventory.ini"
        VERBATIM
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        add_custom_command(
            TARGET ${PROJECT_NAME}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${DLL_FOLDER}/$<TARGET_PDB_FILE_NAME:${PROJECT_NAME}>"
            VERBATIM
        )
    endif()
endfunction()

if(DEFINED ENV{SKYRIM_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_FOLDER}/Data")
    copy_plugin_output("$ENV{SKYRIM_FOLDER}/Data")
endif()

if(DEFINED ENV{SKYRIM_MODS_FOLDER} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER}")
    copy_plugin_output("$ENV{SKYRIM_MODS_FOLDER}/${PROJECT_NAME}")
endif()

if(DEFINED ENV{SKYRIM_MODS_FOLDER2} AND IS_DIRECTORY "$ENV{SKYRIM_MODS_FOLDER2}")
    copy_plugin_output("$ENV{SKYRIM_MODS_FOLDER2}/${PROJECT_NAME}")
endif()
