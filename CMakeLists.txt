cmake_minimum_required(VERSION 3.13)

include(CheckIncludeFiles)
if(UNIX)
 include(GNUInstallDirs)
endif()

project(vs2 CXX C)

find_package(Git QUIET)
if(GIT_EXECUTABLE AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/.git)
 execute_process(
  COMMAND
  ${GIT_EXECUTABLE} describe --tags --abbrev=0
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE LATEST_TAG
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
 )
 execute_process(
  COMMAND
  ${GIT_EXECUTABLE} rev-list ${LATEST_TAG}.. --count --first-parent
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE TAG_DISTANCE
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
 )
 execute_process(COMMAND ${GIT_EXECUTABLE} update-index -q --refresh
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  OUTPUT_QUIET
  ERROR_QUIET
 )
 execute_process(
  COMMAND
  ${GIT_EXECUTABLE} diff-index --name-only HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_DIFF_INDEX
  ERROR_QUIET
 )
 string(COMPARE NOTEQUAL "${GIT_DIFF_INDEX}" "" GIT_DIRTY)
 set(PROJECT_VERSION ${LATEST_TAG}.${TAG_DISTANCE})
 if(GIT_DIRTY)
  set(PROJECT_VERSION "${PROJECT_VERSION}-dirty")
 endif()
else()
 set(PROJECT_VERSION "unknown")
endif()

find_path(VAPOURSYNTH_INCLUDE_DIR NAMES vapoursynth.h VapourSynth.h
 PATHS ${INCLUDE_PATH} $ENV{INCLUDE_PATH} PATH_SUFFIXES vapoursynth)
find_library(VAPOURSYNTH_VSSCRIPT_LIB NAMES vapoursynth-script vsscript
 PATHS ${LIB_PATH} $ENV{LIB_PATH})

find_path(XXHASH_INCLUDE_DIR NAMES xxhash.h
 PATHS ${INCLUDE_PATH} $ENV{INCLUDE_PATH})
find_library(XXHASH_LIB NAMES xxhash libxxhash
 PATHS ${LIB_PATH} $ENV{LIB_PATH})

if(UNIX)
 set(CMAKE_INSTALL_PREFIX /usr/local CACHE PATH "" FORCE)
else()
 set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/install CACHE PATH "" FORCE)
endif()

set(PROJECT_BUILD_DIR ${CMAKE_BINARY_DIR})
configure_file(cmake/config.h.in ${CMAKE_BINARY_DIR}/config.h)

add_subdirectory(sources)

message(STATUS "vs2 version ${PROJECT_VERSION}")
