cmake_minimum_required(VERSION 3.25)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
project(model-tools VERSION 0.1.0 LANGUAGES CXX DESCRIPTION "LEGO Universe 3D model viewers (NIF, HKX, LXFML)")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 COMPONENTS Widgets OpenGLWidgets REQUIRED)
find_package(OpenGL REQUIRED)

include(FetchContent)
FetchContent_Declare(lu_assets
    GIT_REPOSITORY https://github.com/LU-Rebuilt/lu-assets.git
    GIT_TAG main
)
FetchContent_MakeAvailable(lu_assets)

# ZLIB — needed by lxfml_viewer for ZIP extraction.
# lu-assets provides ZLIB::ZLIB via FetchContent fallback on Windows.
if(NOT TARGET ZLIB::ZLIB)
    find_package(ZLIB REQUIRED)
endif()

FetchContent_Declare(tool_common
    GIT_REPOSITORY https://github.com/LU-Rebuilt/tool-common.git
    GIT_TAG main
)
FetchContent_MakeAvailable(tool_common)

# NIF mesh viewer
add_executable(nif_viewer
    src/nif_viewer/main.cpp
    src/nif_viewer/main_window.cpp src/nif_viewer/main_window.h
    src/nif_viewer/nif_gl_widget.cpp src/nif_viewer/nif_gl_widget.h
)
target_link_libraries(nif_viewer PRIVATE gl_viewport lu_assets qt_common)
target_include_directories(nif_viewer PRIVATE src/nif_viewer)

# HKX collision viewer
add_executable(hkx_viewer
    src/hkx_viewer/main.cpp
    src/hkx_viewer/main_window.cpp src/hkx_viewer/main_window.h
    src/hkx_viewer/hkx_gl_widget.cpp src/hkx_viewer/hkx_gl_widget.h
)
target_link_libraries(hkx_viewer PRIVATE gl_viewport lu_assets qt_common)
target_include_directories(hkx_viewer PRIVATE src/hkx_viewer)

# LXFML brick model viewer
add_executable(lxfml_viewer
    src/lxfml_viewer/main.cpp
    src/lxfml_viewer/main_window.cpp src/lxfml_viewer/main_window.h
    src/lxfml_viewer/lxfml_gl_widget.cpp src/lxfml_viewer/lxfml_gl_widget.h
    src/lxfml_viewer/brick_db.cpp src/lxfml_viewer/brick_db.h
    src/lxfml_viewer/zip_extract.cpp src/lxfml_viewer/zip_extract.h
)
target_link_libraries(lxfml_viewer PRIVATE gl_viewport lu_assets qt_common ZLIB::ZLIB)
target_include_directories(lxfml_viewer PRIVATE src/lxfml_viewer)
