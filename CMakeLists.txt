cmake_minimum_required(VERSION 3.14)
project(clodexport LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(BUILD_SHARED_LIBS OFF)

# Assimp
option(CLODEXPORT_BUILD_ASSIMP_LOADER "Build with Assimp-based model loader" ON)
set(CLODEXPORT_ASSIMP_SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/assimp)
if(CLODEXPORT_BUILD_ASSIMP_LOADER AND EXISTS ${CLODEXPORT_ASSIMP_SOURCE_DIR}/CMakeLists.txt)
  set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(ASSIMP_INSTALL_PDB OFF CACHE BOOL "" FORCE)
  set(ASSIMP_USE_HUNTER ON CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
  set(ASSIMP_USE_SYSTEM_ZLIB OFF CACHE BOOL "" FORCE)
  set(ASSIMP_BUILD_NO_OWN_ZLIB OFF CACHE BOOL "" FORCE)
  add_subdirectory(${CLODEXPORT_ASSIMP_SOURCE_DIR} ${CMAKE_BINARY_DIR}/deps/assimp)
endif()

# Meshoptimizer
set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_CLODEXPORT OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)

add_subdirectory(deps/meshoptimizer)

# Trichi
option(CLODEXPORT_BUILD_TRICHI "Build with trichi generator" ON)
set(CLODEXPORT_TRICHI_SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/trichi)
if(CLODEXPORT_BUILD_TRICHI AND EXISTS ${CLODEXPORT_TRICHI_SOURCE_DIR}/CMakeLists.txt)
  set(TRICHI_BUILD_CLI OFF CACHE BOOL "" FORCE)
  add_subdirectory(${CLODEXPORT_TRICHI_SOURCE_DIR} ${CMAKE_BINARY_DIR}/deps/trichi)
endif()

add_executable(clodexport
  src/main.cpp
  src/model_loader.cpp
  src/json_writer.cpp
  src/generators/clusterlod.cpp
)

if(CLODEXPORT_BUILD_TRICHI)
  target_sources(clodexport PRIVATE src/generators/trichi.cpp)
  target_compile_definitions(clodexport PRIVATE CLODEXPORT_HAS_TRICHI=1)
endif()

target_include_directories(clodexport PRIVATE
  ${CMAKE_SOURCE_DIR}/src
  ${CMAKE_SOURCE_DIR}/deps/meshoptimizer
  ${CMAKE_SOURCE_DIR}/deps/meshoptimizer/demo
  ${CMAKE_SOURCE_DIR}/deps/meshoptimizer/extern
)

target_link_libraries(clodexport PRIVATE meshoptimizer)
if(CLODEXPORT_BUILD_ASSIMP_LOADER AND TARGET assimp)
  target_link_libraries(clodexport PRIVATE assimp)
endif()
if(CLODEXPORT_BUILD_TRICHI AND EXISTS ${CLODEXPORT_TRICHI_SOURCE_DIR}/CMakeLists.txt)
  target_link_libraries(clodexport PRIVATE trichi)
  target_include_directories(clodexport PRIVATE ${CLODEXPORT_TRICHI_SOURCE_DIR}/include)
endif()

# nv_cluster_lod_builder: optional third generator (C++20, archived repo)
option(CLODEXPORT_BUILD_NVCLUSTERLOD "Build with nv_cluster_lod_builder generator" ON)
set(CLODEXPORT_NVCLUSTERLOD_SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/nv_cluster_lod_builder)
if(CLODEXPORT_BUILD_NVCLUSTERLOD AND EXISTS ${CLODEXPORT_NVCLUSTERLOD_SOURCE_DIR}/CMakeLists.txt)
  set(NVCLUSTERLOD_FETCH_MESHOPTIMIZER OFF CACHE BOOL "Use existing meshoptimizer" FORCE)
  add_subdirectory(${CLODEXPORT_NVCLUSTERLOD_SOURCE_DIR} ${CMAKE_BINARY_DIR}/deps/nv_cluster_lod_builder)
endif()

if(CLODEXPORT_BUILD_NVCLUSTERLOD)
  target_sources(clodexport PRIVATE src/generators/nv_cluster_lod_builder.cpp)
  target_compile_definitions(clodexport PRIVATE CLODEXPORT_HAS_NVCLUSTERLOD=1)
  target_link_libraries(clodexport PRIVATE nv_cluster_lod_builder)
  target_include_directories(clodexport PRIVATE
    ${CLODEXPORT_NVCLUSTERLOD_SOURCE_DIR}/include
    ${CLODEXPORT_NVCLUSTERLOD_SOURCE_DIR}/nv_cluster_builder/include
  )
endif()

# C++ standard: C++20 required for nv_cluster_lod_builder, else C++17 for trichi, else C++11
if(CLODEXPORT_BUILD_NVCLUSTERLOD)
  set_target_properties(clodexport PROPERTIES CXX_STANDARD 20)
elseif(CLODEXPORT_BUILD_TRICHI)
  set_target_properties(clodexport PROPERTIES CXX_STANDARD 17)
else()
  set_target_properties(clodexport PROPERTIES CXX_STANDARD 11)
endif()
