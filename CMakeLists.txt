cmake_minimum_required(VERSION 3.14)
project(clodexport LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)

# Meshoptimizer: build only the library, not its clodexport target
set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_CLODEXPORT OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
add_subdirectory(deps/meshoptimizer)

# Trichi: optional second generator (git submodule at deps/trichi)
option(CLODEXPORT_BUILD_TRICHI "Build with trichi generator" ON)
set(CLODEXPORT_TRICHI_SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/trichi)
if(CLODEXPORT_BUILD_TRICHI AND EXISTS ${CLODEXPORT_TRICHI_SOURCE_DIR}/CMakeLists.txt)
  # Patch trichi to use our meshoptimizer when the target already exists (avoids duplicate symbols)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} apply -p1 --ignore-whitespace --check ${CMAKE_SOURCE_DIR}/patches/trichi-use-existing-meshoptimizer.patch
    WORKING_DIRECTORY ${CLODEXPORT_TRICHI_SOURCE_DIR}
    RESULT_VARIABLE TRICHI_PATCH_CHECK
    OUTPUT_QUIET ERROR_QUIET
  )
  if(TRICHI_PATCH_CHECK EQUAL 0)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} apply -p1 --ignore-whitespace ${CMAKE_SOURCE_DIR}/patches/trichi-use-existing-meshoptimizer.patch
      WORKING_DIRECTORY ${CLODEXPORT_TRICHI_SOURCE_DIR}
      RESULT_VARIABLE TRICHI_PATCH_APPLY
    )
    if(NOT TRICHI_PATCH_APPLY EQUAL 0)
      message(WARNING "Failed to apply trichi patch; build may fail with duplicate meshoptimizer symbols")
    endif()
  endif()
  set(TRICHI_BUILD_CLI OFF CACHE BOOL "" FORCE)
  add_subdirectory(${CLODEXPORT_TRICHI_SOURCE_DIR} ${CMAKE_BINARY_DIR}/deps/trichi)
endif()

add_executable(clodexport
  src/main.cpp
  src/obj_loader.cpp
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
