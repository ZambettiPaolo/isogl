cmake_minimum_required(VERSION 3.16)
project(isogl_gtk4_host C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ISOGL REQUIRED IMPORTED_TARGET isogl)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
pkg_check_modules(EGL REQUIRED IMPORTED_TARGET egl)

add_executable(isogl-gtk4-host main.c)
target_link_libraries(isogl-gtk4-host PRIVATE
    PkgConfig::ISOGL
    PkgConfig::GTK4
    PkgConfig::EGL
    ${CMAKE_DL_LIBS})
