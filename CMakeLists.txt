cmake_minimum_required(VERSION 3.16)

project(ClassBoard
    VERSION 1.0.0
    DESCRIPTION "Digital classroom whiteboard for interactive displays"
    HOMEPAGE_URL "https://github.com/An0nym0us-glitch/Smart-board22"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(CLASSBOARD_BUILD_TESTS "Build unit tests" ON)
option(CLASSBOARD_DEPLOY_QT "Run windeployqt after building (Windows)" OFF)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(Qt5 5.15 QUIET OPTIONAL_COMPONENTS Pdf)

add_subdirectory(src)

if(CLASSBOARD_BUILD_TESTS)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Test)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Packaging.cmake)
