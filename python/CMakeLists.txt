cmake_minimum_required(VERSION 3.15)
project(pyretopo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Python 3.8 COMPONENTS Interpreter Development.Module REQUIRED)

# pybind11 modern location
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ext/pybind11_modern/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ext/pybind11_modern" pybind11_build)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../ext/nanogui/ext/pybind11/CMakeLists.txt")
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ext/nanogui/ext/pybind11" pybind11_build)
else()
  find_package(pybind11 REQUIRED)
endif()

# Ensure TBB is built
if(NOT TARGET tbb_static)
  set(TBB_BUILD_STATIC ON CACHE BOOL " " FORCE)
  set(TBB_BUILD_SHARED OFF CACHE BOOL " " FORCE)
  set(TBB_BUILD_TBBMALLOC OFF CACHE BOOL " " FORCE)
  set(TBB_BUILD_TBBMALLOC_PROXY OFF CACHE BOOL " " FORCE)
  set(TBB_BUILD_TESTS OFF CACHE BOOL " " FORCE)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../ext/tbb" ext_build/tbb)
endif()

# Core sources
set(CORE_SOURCES
  ../src/retopo_engine.cpp
  ../src/adaptive_scale.cpp
  ../src/contour_guide.cpp
  ../src/singularity_filter.cpp
  ../src/quality_report.cpp
  ../src/extraction_sanitizer.cpp
  ../src/meshio.cpp
  ../src/normal.cpp
  ../src/adjacency.cpp
  ../src/meshstats.cpp
  ../src/hierarchy.cpp
  ../src/extract.cpp
  ../src/field.cpp
  ../src/bvh.cpp
  ../src/subdivide.cpp
  ../src/reorder.cpp
  ../src/serializer.cpp
  ../src/cleanup.cpp
  ../src/dedge.cpp
  ../ext/rply/rply.c
)

pybind11_add_module(pyretopo bindings.cpp ${CORE_SOURCES})

target_include_directories(pyretopo PRIVATE
  ../src
  ../ext/eigen
  ../ext/nanogui/ext/eigen
  ../ext/dset
  ../ext/pss
  ../ext/pcg32
  ../ext/rply
  ../ext/half
  ../ext/tbb/include
)

target_link_libraries(pyretopo PRIVATE tbb_static)

if(MSVC)
  target_compile_definitions(pyretopo PRIVATE _CRT_SECURE_NO_WARNINGS __TBB_NO_IMPLICIT_LINKAGE)
  target_compile_options(pyretopo PRIVATE /MP)
endif()
