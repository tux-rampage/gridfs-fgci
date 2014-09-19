
#pragma once

#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

/*
 * Listening socket file number
 */
#define FCGI_LISTENSOCK_FILENO 0

/*
 * Number of bytes in a FCGI_Header.  Future versions of the protocol
 * will not reduce this number.
 */
#define FCGI_HEADER_LEN  8

/*
 * Value for version component of FCGI_Header
 */
#define FCGI_VERSION_1           1

/*
 * Values for type component of FCGI_Header
 */
#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE       11
#define FCGI_MAXTYPE (FCGI_UNKNOWN_TYPE)

/*
 * Value for requestId component of FCGI_Header
 */
#define FCGI_NULL_REQUEST_ID     0

/*
 * Mask for flags component of FCGI_BeginRequestBody
 */
#define FCGI_KEEP_CONN  1

/*
 * Values for role component of FCGI_BeginRequestBody
 */
#define FCGI_RESPONDER  1
#define FCGI_AUTHORIZER 2
#define FCGI_FILTER     3

/*
 * Values for protocolStatus component of FCGI_EndRequestBody
 */
#define FCGI_REQUEST_COMPLETE 0
#define FCGI_CANT_MPX_CONN    1
#define FCGI_OVERLOADED       2
#define FCGI_UNKNOWN_ROLE     3

/*
 * Variable names for FCGI_GET_VALUES / FCGI_GET_VALUES_RESULT records
 */
#define FCGI_MAX_CONNS  "FCGI_MAX_CONNS"
#define FCGI_MAX_REQS   "FCGI_MAX_REQS"
#define FCGI_MPXS_CONNS "FCGI_MPXS_CONNS"


/**
 * Fast CGI
 */
namespace fastcgi
{
    /**
     * Low level protocol
     */
    namespace protocol {
        typedef struct {
            unsigned char version;
            unsigned char type;
            unsigned char requestId[2]; // uint16
            unsigned char contentLength[2];// uint16
            unsigned char paddingLength;
            unsigned char reserved;
        } Header;

        /**
         * Request Body
         */
        typedef struct {
            unsigned char role[2];// uint16
            unsigned char flags;
            unsigned char reserved[5];
        } BeginRequestBody;


        typedef struct {
            Header header;
            BeginRequestBody body;
        } BeginRequestRecord;


        typedef struct {
            unsigned char appStatus[4]; // uint32
            unsigned char protocolStatus;
            unsigned char reserved[3];
        } EndRequestBody;

        typedef struct {
            Header header;
            EndRequestBody body;
        } EndRequestRecord;

        typedef struct {
            unsigned char type;
            unsigned char reserved[7];
        } UnknownTypeBody;

        typedef struct {
            Header header;
            UnknownTypeBody body;
        } UnknownTypeRecord;
    };

    class HandlerThread
    {
        protected:
            std::thread* thread;


        public:
            HandlerThread();
            ~HandlerThread();

            void handle();
    }

    class IOHandler
    {
        public:
            IOHandler(std::string bind);
            ~IOHandler();

            void run();
    };
};
