/* include/fastcgi++/config.h.in - CMake Template  */

#undef HAVE_MYSQL_H

/* @todo: Detect this header via CMake */
#define HAVE_ENDIAN_H

/* UTF8 decoder is broken */
#define FASTCGIPP_DISABLE_UTF8 1; 

/* Name of package */
#define PACKAGE "gridfs-fcgi"

/* Define to the full name of this package. */
#define PACKAGE_NAME "gridfs-fcgi"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "gridfs-fcgi "

/* Define to the version of this package. */
#define PACKAGE_VERSION ""

/* Version number of package */
#define VERSION ""
