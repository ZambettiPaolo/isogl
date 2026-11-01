cmake_minimum_required(VERSION 3.16)
project(isogl_qt6_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets OpenGLWidgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ISOGL REQUIRED IMPORTED_TARGET isogl)

add_executable(isogl-qt6-host main.cpp)
target_link_libraries(isogl-qt6-host PRIVATE
    PkgConfig::ISOGL
    Qt6::Widgets
    Qt6::OpenGLWidgets)
