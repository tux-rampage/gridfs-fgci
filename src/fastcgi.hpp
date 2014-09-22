
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
	class IOHandler;

    /**
     * Low level protocol
     */
    namespace protocol {
        struct Header {
            unsigned char version;
            unsigned char type;
            uint16_t requestId; // char[2]
            uint16_t contentLength; // char[2]
            unsigned char paddingLength;
            unsigned char reserved;
        };

        struct Record {
			Header header;
			char *content = NULL;
        };

        struct BeginRequestBody {
            uint16_t role; // char[2] / uint16
            unsigned char flags;
            unsigned char reserved[5];
        };


        struct BeginRequestRecord {
            Header header;
            BeginRequestBody body;
        };


        struct EndRequestBody {
            uint32_t appStatus; // char[4] / uint32
            unsigned char protocolStatus;
            unsigned char reserved[3];
        };

        struct EndRequestRecord {
            Header header;
            EndRequestBody body;
        };

        struct UnknownTypeBody {
            unsigned char type;
            unsigned char reserved[7];
        };

        struct UnknownTypeRecord {
            Header header;
            UnknownTypeBody body;
        };

        class Variable
        {
        	private:
        		std::string _name;
        		std::string _value;

        		size_t putSize(char *buffer, const size_t& size) const;

        	public:
        		inline Variable(const std::string name, const std::string value) : _name(name), _value(value) {};

        		inline ~Variable() {};

        		inline std::string name() const
        		{
        			return this->_name;
        		}

        		inline void name(const std::string name)
        		{
        			this->_name = name;
        		}

        		std::string value() const
        		{
        			return this->_value;
        		}

        		void value(const std::string& value)
        		{
        			this->_value = value;
        		}

        		inline size_t getValueSize() const
        		{
        			return this->_value.size();
        		}

        		inline size_t getNameSize() const
        		{
        			return this->_name.size();
        		}

        		size_t getSize() const;

        		/**
        		 * Put variable into the given buffer
        		 *
        		 * The caller must ensure that the memory from *buffer can
        		 * store this->getSize() bytes
        		 */
        		void putData(char* buffer) const;
        };

        /**
         * Http Request
         */
        class Request
        {
			friend ::fastcgi::IOHandler;

        	public:
        		enum HTTPMethod { GET, SET, PUT, POST, DELETE };
        		enum Role { Responder, Authenticator };

        	protected:
        		uint16_t id;
        		std::map<std::string, std::string> params;
        		Role role;

        		void processIncommingRecord(const Record& record);

        	public:
        		Request();
        		~Request();

        		/**
        		 * Returns the request id
        		 */
        		inline int getId() const
        		{
        			return this->id;
        		};

        		inline Role getRole() const
        		{
        			return this->role;
        		};
        };
    };

    class HandlerThread
    {
        protected:
            std::thread* thread;


        public:
            HandlerThread();
            ~HandlerThread();

            void handle();
    };

    /**
     * Handles FastCGI I/O
     */
    class IOHandler
    {
    	protected:
    		class Client
    		{
    			private:
    				IOHandler& io;
    				int socket;
    				protocol::Record currentRecord;
    				unsigned char* buffer;
    				size_t headerBytesRead;
    				size_t contentBytesRead;
    				size_t paddingBytesRead;

    				bool headerReady;
    				bool contentReady;
    				bool paddingReady;

    				evbuffer *outputBuffer;

    				template<class T> void prepareRecordSegment(T& segment);

    				char* extractHeader(char* buffer, size_t& size);
    				char* extractContent(char* buffer, size_t& size);
    				char* extractPadding(char* buffer, size_t& size);

    				void dispatch(bufferevent* bev);

    			public:
    				Client(IOHandler& io, int socket);
    				~Client();

    				void onRead(bufferevent *bev, void *arg);

    		};

    		std::vector<Client*> clients;
    		std::map<uint16_t, protocol::Request> requests;

        public:
            IOHandler(std::string bind);
            ~IOHandler();

            void run();
    };
};

