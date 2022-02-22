configure_file(config.h.in ${CMAKE_BINARY_DIR}/generated/config.h)

add_executable(vs2 vs2.cxx ${CMAKE_BINARY_DIR}/generated/config.h)

set_property(TARGET vs2 PROPERTY CXX_STANDARD 20)

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
 set_target_properties(vs2 PROPERTIES COMPILE_FLAGS "/wd4996")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
 set_target_properties(vs2 PROPERTIES COMPILE_FLAGS "-Wno-deprecated-declarations -Wno-logical-op-parentheses")
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BUILD_DIR} ${CMAKE_BINARY_DIR}/generated)

check_include_files(getopt.h HAVE_GETOPT_H)

if(NOT HAVE_GETOPT_H)
 target_sources(vs2 PRIVATE getopt.c getopt.h)
 if(MSVC)
  set_source_files_properties(getopt.c PROPERTIES COMPILE_FLAGS "-DHAVE_STRING_H")
 endif()
endif()

if(VAPOURSYNTH_INCLUDE_DIR)
 include_directories(${VAPOURSYNTH_INCLUDE_DIR})
else()
 message(WARNING "vapoursynth headers are not found")
endif()

if(VAPOURSYNTH_VSSCRIPT_LIB)
 target_link_libraries(vs2 ${VAPOURSYNTH_VSSCRIPT_LIB})
else()
 message(WARNING "could not find vsscript library")
endif()

if(XXHASH_INCLUDE_DIR)
 include_directories(${XXHASH_INCLUDE_DIR})
else()
 message(WARNING "xxhash headers are not found")
endif()

if(XXHASH_LIB)
 target_link_libraries(vs2 ${XXHASH_LIB})
else()
 message(WARNING "could not find xxhash library")
endif()

install(TARGETS vs2 RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
