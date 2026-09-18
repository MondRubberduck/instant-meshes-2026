cmake_minimum_required(VERSION 3.15)
project(InstantMeshes C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(INSTANT_MESHES_BUILD_GUI "Build GUI application" ON)
option(INSTANT_MESHES_BUILD_PYTHON "Build Python bindings" ON)
option(INSTANT_MESHES_DEV_MODE "Developer mode" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()
string(TOUPPER "${CMAKE_BUILD_TYPE}" U_CMAKE_BUILD_TYPE)

if (CMAKE_GENERATOR MATCHES "Visual Studio")
  set_property(GLOBAL PROPERTY USE_FOLDERS ON)
endif()

# Compiler options
if(MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS -D__TBB_NO_IMPLICIT_LINKAGE)
  add_compile_options(/MP /W4)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    add_compile_options(/arch:SSE2 /DEIGEN_DONT_ALIGN)
  endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(-Wno-gnu-anonymous-struct -Wno-c99-extensions -Wno-nested-anon-types)
  endif()
endif()

# Build TBB
set(TBB_BUILD_STATIC ON CACHE BOOL " " FORCE)
set(TBB_BUILD_SHARED OFF CACHE BOOL " " FORCE)
set(TBB_BUILD_TBBMALLOC OFF CACHE BOOL " " FORCE)
set(TBB_BUILD_TBBMALLOC_PROXY OFF CACHE BOOL " " FORCE)
set(TBB_BUILD_TESTS OFF CACHE BOOL " " FORCE)
add_subdirectory(ext/tbb ext_build/tbb)
set_property(TARGET tbb_static tbb_def_files PROPERTY FOLDER "dependencies")

# Locate Eigen
set(EIGEN_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/ext/eigen")
if(NOT EXISTS "${EIGEN_INCLUDE_DIRS}/Eigen/Core")
  set(EIGEN_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/ext/eigen")
endif()

# Common include directories for retopo core
set(CORE_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${EIGEN_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/tbb/include
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/dset
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/pss
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/pcg32
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/rply
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/ufbx
  ${CMAKE_CURRENT_SOURCE_DIR}/ext/half
)

# --- Target: retopo_core (Headless C++17 library) ---

set(CORE_SOURCES
  src/retopo_engine.h src/retopo_engine.cpp
  src/adaptive_scale.h src/adaptive_scale.cpp
  src/contour_guide.h src/contour_guide.cpp
  src/singularity_filter.h src/singularity_filter.cpp
  src/quality_report.h src/quality_report.cpp
  src/extraction_sanitizer.h src/extraction_sanitizer.cpp
  src/aabb.h
  src/common.h
  src/solver_interface.h
  src/meshio.h src/meshio.cpp
  src/normal.h src/normal.cpp
  src/adjacency.h src/adjacency.cpp
  src/meshstats.h src/meshstats.cpp
  src/hierarchy.h src/hierarchy.cpp
  src/extract.h src/extract.cpp
  src/field.h src/field.cpp
  src/bvh.h src/bvh.cpp
  src/subdivide.h src/subdivide.cpp
  src/reorder.h src/reorder.cpp
  src/serializer.h src/serializer.cpp
  src/smoothcurve.h src/smoothcurve.cpp
  src/cleanup.h src/cleanup.cpp
  src/dedge.h src/dedge.cpp
  ext/rply/rply.c
  ext/ufbx/ufbx.c
)

add_library(retopo_core STATIC ${CORE_SOURCES})
target_include_directories(retopo_core PUBLIC ${CORE_INCLUDES})
target_link_libraries(retopo_core PUBLIC tbb_static)

if(MSVC)
  set_source_files_properties(ext/rply/rply.c ext/ufbx/ufbx.c PROPERTIES COMPILE_FLAGS "/wd4127 /wd4244 /wd4701")
endif()

# --- Target: InstantMeshesCLI (Command-line tool) ---

add_executable(InstantMeshesCLI
  src/main.cpp
  src/batch.h src/batch.cpp
)
target_link_libraries(InstantMeshesCLI PRIVATE retopo_core)
target_include_directories(InstantMeshesCLI PRIVATE ${CORE_INCLUDES})
set_target_properties(InstantMeshesCLI PROPERTIES OUTPUT_NAME "InstantMeshesCLI")

# --- Target: GUI Application (NanoGUI) ---

if(INSTANT_MESHES_BUILD_GUI AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/CMakeLists.txt")
  set(NANOGUI_BUILD_EXAMPLE OFF CACHE BOOL " " FORCE)
  set(NANOGUI_BUILD_SHARED  OFF CACHE BOOL " " FORCE)
  set(NANOGUI_BUILD_PYTHON  OFF CACHE BOOL " " FORCE)
  add_subdirectory(ext/nanogui ext_build/nanogui)
  set_property(TARGET nanogui glfw glfw_objects PROPERTY FOLDER "dependencies")

  # Resources compilation
  set(PNG_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/comb.png
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/delete_stroke.png
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/loadmesh.png
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/scare.png
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/sing_dir.png
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/sing_pos.png
    ${CMAKE_CURRENT_SOURCE_DIR}/resources/instantmeshes.png)
  file(GLOB FRAG_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.frag")
  file(GLOB VERT_FILES "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.vert")
  file(GLOB GEO_FILES  "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.geo")
  set(IM_RESOURCES ${PNG_FILES} ${FRAG_FILES} ${VERT_FILES} ${GEO_FILES})

  string(REPLACE ";" "," resources_string "${IM_RESOURCES}")
  set(im_bin2c_cmdline
    -DOUTPUT_C=resources.cpp
    -DOUTPUT_H=resources.h
    "-DINPUT_FILES=${resources_string}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/resources/bin2c.cmake"
  )

  add_custom_command(
    OUTPUT resources.cpp resources.h
    COMMAND ${CMAKE_COMMAND} ARGS ${im_bin2c_cmdline}
    DEPENDS ${IM_RESOURCES}
    COMMENT "Running bin2c on resources"
    PRE_BUILD VERBATIM
  )

  set(EXTRA_GUI_SOURCE "")
  if (APPLE)
    set(EXTRA_GUI_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/im.icns")
  elseif(WIN32)
    set(EXTRA_GUI_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/resources/im.rc")
  endif()

  add_executable(InstantMeshes MACOSX_BUNDLE
    resources.cpp
    src/main.cpp
    src/batch.h src/batch.cpp
    src/glutil.h src/glutil.cpp
    src/widgets.h src/widgets.cpp
    src/tool.h
    src/viewer.h src/viewer.cpp
    ${EXTRA_GUI_SOURCE}
  )

  target_compile_definitions(InstantMeshes PRIVATE INSTANT_MESHES_GUI)

  target_include_directories(InstantMeshes PRIVATE
    ${CORE_INCLUDES}
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/nanogui/include
    ${NANOGUI_EXTRA_INCS}
    ${CMAKE_CURRENT_BINARY_DIR}
  )
  add_definitions(${NANOGUI_EXTRA_DEFS})
  target_link_libraries(InstantMeshes PRIVATE retopo_core nanogui ${NANOGUI_EXTRA_LIBS})
  set_target_properties(InstantMeshes PROPERTIES OUTPUT_NAME "Instant Meshes")
endif()

# --- Target: Python Module (pybind11) ---

if(INSTANT_MESHES_BUILD_PYTHON AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/python/CMakeLists.txt")
  find_package(Python 3.8 COMPONENTS Interpreter Development.Module QUIET)
  if(Python_FOUND)
    message(STATUS "Found Python ${Python_VERSION}, enabling pyretopo module.")
    add_subdirectory(python)
  endif()
endif()
