cmake_minimum_required(VERSION 2.6)
project(gridfs-fcgi)

SET(CMAKE_VERBOSE_MAKEFILE ON)
SET(GIRDFSFCGI_VERSION "1.0.0")
SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fmessage-length=0 -fdiagnostics-show-location=every-line")
SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fmessage-length=0 -fdiagnostics-show-location=every-line")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
set(GFSFCGI_CONFIG_FILE CACHE STRING "The default config file location" "/etc/gridfs-fcgi/gridfs-fcgi.conf")
option(Boost_USE_STATIC_LIBS "Use static boost libraries" OFF)

find_package(MongoDB REQUIRED)
find_package(Boost REQUIRED COMPONENTS date_time thread system iostreams)

configure_file(
    "${PROJECT_SOURCE_DIR}/include/fastcgi++/config.h.in"
    "${PROJECT_SOURCE_DIR}/include/fastcgi++/config.h"
)

include_directories("${CMAKE_SOURCE_DIR}/fastcgipp/include" ${Boost_INCLUDE_DIRS})

add_library(fastcgipp STATIC src/http.cpp
	src/protocol.cpp
	src/request.cpp
	src/manager.cpp
	src/transceiver.cpp
	src/fcgistream.cpp)

target_link_libraries(fastcgipp ${Boost_LIBRARIES})
