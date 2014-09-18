cmake_minimum_required(VERSION 2.6)
project(gridfs-fcgi)

SET(CMAKE_VERBOSE_MAKEFILE ON)
SET(GIRDFSFCGI_VERSION "1.0.0")
SET(CMAKE_CXX_FLAGS "-pthread -std=c++0x ${CMAKE_CXX_FLAGS} -fmessage-length=0 -fdiagnostics-show-location=every-line")

#IF(CMAKE_COMPILER_IS_GNUCC)
#  SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fmessage-length=0")
#ENDIF(CMAKE_COMPILER_IS_GNUCC)
#IF(CMAKE_COMPILER_IS_GNUCXX)
#  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmessage-length=0")
#ENDIF(CMAKE_COMPILER_IS_GNUCXX)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
option(GFSFCGI_CONFIG_FILE "The default config file location" "/etc/gridfs-fcgi/gridfs-fcgi.conf")

find_package(MongoDB REQUIRED)
find_package(Boost REQUIRED)

configure_file(
    "${PROJECT_SOURCE_DIR}/src/config.h.in"
    "${PROJECT_SOURCE_DIR}/src/config.h"
)

add_subdirectory(fastcgipp)

include_directories(fastcgipp/include ${MongoDB_INCLUDE_DIR} ${Boost_INCLUDE_DIRS})
add_executable(gridfs-fcgi src/main.cpp)
target_link_libraries(gridfs-fcgi fastcgipp ${MongoDB_LIBRARIES} ${Boost_LIBRARIES})
