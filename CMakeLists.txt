cmake_minimum_required(VERSION 3.16)
project(flower_opengl C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(OpenMP REQUIRED)

# GLFW: try pkg-config first (Linux), then find_package (Windows)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GLFW3 QUIET glfw3)
endif()

if(GLFW3_FOUND)
    set(GLFW_INCLUDE ${GLFW3_INCLUDE_DIRS})
    set(GLFW_LIB ${GLFW3_LIBRARIES})
else()
    find_package(glfw3 QUIET)
    if(glfw3_FOUND)
        set(GLFW_INCLUDE "")
        set(GLFW_LIB glfw)
    else()
        message(STATUS "glfw3 not found via pkg-config or find_package.")
        message(STATUS "Set GLFW_INCLUDE_DIR and GLFW_LIBRARY manually if needed.")
        set(GLFW_INCLUDE "" CACHE PATH "GLFW include directory")
        set(GLFW_LIB "" CACHE STRING "GLFW library name/path")
    endif()
endif()

add_executable(flower src/main.c)

if(GLFW_INCLUDE)
    target_include_directories(flower PRIVATE ${GLFW_INCLUDE})
endif()

target_link_libraries(flower
    ${GLFW_LIB}
    OpenGL::GL
    OpenMP::OpenMP_C
)

# Linux needs -lm, Windows does not
if(UNIX AND NOT APPLE)
    target_link_libraries(flower m)
endif()

# Compiler options
if(MSVC)
    target_compile_options(flower PRIVATE /O2)
else()
    target_compile_options(flower PRIVATE -O3 -march=native -ffast-math)
endif()

# Copy shaders to build directory
file(COPY ${CMAKE_SOURCE_DIR}/shaders DESTINATION ${CMAKE_BINARY_DIR})