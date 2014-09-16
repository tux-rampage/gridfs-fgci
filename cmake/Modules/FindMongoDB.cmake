# - Find MongoDB
# Find the MongoDB includes and client library
# This module defines
#  MongoDB_INCLUDE_DIR, where to find mongo/client/dbclient.h
#  MongoDB_LIBRARIES, the libraries needed to use MongoDB.
#  MongoDB_FOUND, If false, do not try to use MongoDB.

if(MongoDB_INCLUDE_DIR AND MongoDB_LIBRARIES)
   set(MongoDB_FOUND TRUE)

else(MongoDB_INCLUDE_DIR AND MongoDB_LIBRARIES)

  find_path(MongoDB_INCLUDE_DIR mongo/client/dbclientinterface.h
      PATHS
      /opt/mongo/include
      $ENV{ProgramFiles}/Mongo/*/include
      $ENV{SystemDrive}/Mongo/*/include
  )

  find_library(MongoDB_LIBRARIES NAMES mongoclient
      PATHS
      /usr/lib/mongo
      /usr/local/lib
      /usr/local/lib/mongo
	  /opt/mongo/lib
  )

  if(MongoDB_INCLUDE_DIR AND MongoDB_LIBRARIES)
    set(MongoDB_FOUND TRUE)
    message(STATUS "Found MongoDB: ${MongoDB_INCLUDE_DIR}, ${MongoDB_LIBRARIES}")
  else(MongoDB_INCLUDE_DIR AND MongoDB_LIBRARIES)
    set(MongoDB_FOUND FALSE)
    if (MongoDB_FIND_REQUIRED)
		message(FATAL_ERROR "MongoDB not found.")
	else (MongoDB_FIND_REQUIRED)
		message(STATUS "MongoDB not found.")
	endif (MongoDB_FIND_REQUIRED)
  endif(MongoDB_INCLUDE_DIR AND MongoDB_LIBRARIES)

  mark_as_advanced(MongoDB_INCLUDE_DIR MongoDB_LIBRARIES)

endif(MongoDB_INCLUDE_DIR AND MongoDB_LIBRARIES)
