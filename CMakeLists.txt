cmake_minimum_required(VERSION 3.16)
project(leveldb_viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LEVELDB_DIR "" CACHE PATH "Path to the LevelDB source directory")
set(ZSTD_DIR "" CACHE PATH "Path to the Zstd package directory")
set(JSON_INCLUDE_DIR "" CACHE PATH "Directory containing nlohmann/json.hpp")
set(CPPHTTPLIB_DIR "" CACHE PATH "Path to the cpp-httplib source directory")

if(NOT IS_DIRECTORY "${LEVELDB_DIR}")
  message(FATAL_ERROR "LEVELDB_DIR must point to the LevelDB source directory")
endif()
if(NOT EXISTS "${LEVELDB_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "LEVELDB_DIR does not contain CMakeLists.txt: ${LEVELDB_DIR}")
endif()
if(NOT IS_DIRECTORY "${ZSTD_DIR}")
  message(FATAL_ERROR "ZSTD_DIR must point to the Zstd package directory")
endif()
if(NOT EXISTS "${ZSTD_DIR}/include/zstd.h")
  message(FATAL_ERROR "ZSTD_DIR does not contain include/zstd.h: ${ZSTD_DIR}")
endif()
if(NOT IS_DIRECTORY "${CPPHTTPLIB_DIR}")
  message(FATAL_ERROR "CPPHTTPLIB_DIR must point to the cpp-httplib source directory")
endif()
if(NOT EXISTS "${CPPHTTPLIB_DIR}/httplib.cpp" OR NOT EXISTS "${CPPHTTPLIB_DIR}/httplib.h")
  message(FATAL_ERROR "CPPHTTPLIB_DIR must contain httplib.cpp and httplib.h: ${CPPHTTPLIB_DIR}")
endif()
if(NOT IS_DIRECTORY "${JSON_INCLUDE_DIR}")
  message(FATAL_ERROR "JSON_INCLUDE_DIR must contain json.hpp")
endif()
if(NOT EXISTS "${JSON_INCLUDE_DIR}/json.hpp")
  message(FATAL_ERROR "JSON_INCLUDE_DIR does not contain json.hpp: ${JSON_INCLUDE_DIR}")
endif()

set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LEVELDB_INSTALL OFF CACHE BOOL "" FORCE)
set(LEVELDB_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

add_subdirectory("${LEVELDB_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/leveldb-build" EXCLUDE_FROM_ALL)

find_library(ZSTD_LIBRARY NAMES zstd_static zstd PATHS "${ZSTD_DIR}/static" "${ZSTD_DIR}/lib" NO_DEFAULT_PATH)
if(NOT ZSTD_LIBRARY)
  message(FATAL_ERROR "Could not find the Zstd library under ${ZSTD_DIR}/static or ${ZSTD_DIR}/lib")
endif()
if(NOT TARGET leveldb)
  message(FATAL_ERROR "The LevelDB directory does not define the expected 'leveldb' target")
endif()
target_include_directories(leveldb PRIVATE "${ZSTD_DIR}/include")
target_compile_definitions(leveldb PRIVATE LEVELDB_VIEWER_ZSTD_AVAILABLE=1)
target_link_libraries(leveldb "${ZSTD_LIBRARY}")

set(JSON_HEADER "${JSON_INCLUDE_DIR}/json.hpp")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/ViewerJson.hpp.in" "${CMAKE_CURRENT_BINARY_DIR}/generated/ViewerJson.hpp" @ONLY)

add_executable(leveldb_viewer
  ${CPPHTTPLIB_DIR}/httplib.cpp
  src/main.cpp
  src/DatabaseManager.cpp
  src/DatabaseTree.cpp
  src/Server.cpp
)
target_include_directories(leveldb_viewer PRIVATE
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  "${CPPHTTPLIB_DIR}"
  "${LEVELDB_DIR}/include"
  "${LEVELDB_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/leveldb-build/include"
  src
  "${JSON_INCLUDE_DIR}"
)
target_link_libraries(leveldb_viewer PRIVATE leveldb)

if(WIN32)
  target_link_libraries(leveldb_viewer PRIVATE ws2_32 shlwapi)
  target_compile_definitions(leveldb_viewer PRIVATE _WIN32_WINNT=0x0A00 NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
