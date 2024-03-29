cmake_minimum_required(VERSION 3.22)

project(vs2 CXX C)

include(CheckIncludeFiles)
if(UNIX)
 include(GNUInstallDirs)
endif()

find_package(Git QUIET)

if(GIT_EXECUTABLE AND EXISTS ${CMAKE_SOURCE_DIR}/.git)
 execute_process(
  COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE LATEST_TAG
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
 )
 execute_process(
  COMMAND ${GIT_EXECUTABLE} rev-list ${LATEST_TAG}.. --count --first-parent
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE TAG_DISTANCE
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
 )
 execute_process(
  COMMAND ${GIT_EXECUTABLE} update-index -q --refresh
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_QUIET
  ERROR_QUIET
 )
 execute_process(
  COMMAND ${GIT_EXECUTABLE} diff-index --name-only HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE GIT_DIFF_INDEX
  ERROR_QUIET
 )
 execute_process(
  COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE BRANCH
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
 )
 execute_process(
  COMMAND ${GIT_EXECUTABLE} log -1 --format=g%h
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  OUTPUT_VARIABLE REVISION_ID
  ERROR_QUIET
  OUTPUT_STRIP_TRAILING_WHITESPACE
 )
endif()

if(DEFINED LATEST_TAG AND NOT LATEST_TAG STREQUAL "")
 set(PROJECT_VERSION "${LATEST_TAG}")
 if(DEFINED TAG_DISTANCE)
  string(APPEND PROJECT_VERSION ".${TAG_DISTANCE}")
 endif()
else()
 set(PROJECT_VERSION "unknown")
endif()
string(APPEND PROJECT_VERSION "-${REVISION_ID}")
if(NOT GIT_DIFF_INDEX STREQUAL "")
 string(APPEND PROJECT_VERSION "-dirty")
endif()

list(APPEND CMAKE_LIBRARY_PATH ${LIBRARY_PATH} $ENV{LIBRARY_PATH})
list(APPEND CMAKE_INCLUDE_PATH ${INCLUDE_PATH} $ENV{INCLUDE_PATH})

find_path(VAPOURSYNTH_INCLUDE_DIR NAMES vapoursynth.h VapourSynth.h PATH_SUFFIXES vapoursynth)
find_library(VAPOURSYNTH_VSSCRIPT_LIB NAMES vapoursynth-script vsscript)

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

add_subdirectory(sources)

message(STATUS "branch ${BRANCH}")
message(STATUS "vs2 version ${PROJECT_VERSION}")
