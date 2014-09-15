cmake_minimum_required(VERSION 2.6)
project(gridfs-fcgi)

SET(CMAKE_VERBOSE_MAKEFILE ON)
SET(GIRDFSFCGI_VERSION "1.0.0")

IF(CMAKE_COMPILER_IS_GNUCC)
  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fmessage-length=0")
ENDIF(CMAKE_COMPILER_IS_GNUCC)
IF(CMAKE_COMPILER_IS_GNUCXX)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmessage-length=0")
ENDIF(CMAKE_COMPILER_IS_GNUCXX)

configure_file(
    "${PROJECT_SOURCE_DIR}/include/config.h.in"
    "${PROJECT_SOURCE_DIR}/include/config.h"
)

include_directories("${PROJECT_SOURCE_DIR}/include")
add_executable(gridfs-fcgi src/main.cpp)
