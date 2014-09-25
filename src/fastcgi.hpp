
#pragma once

#include <stdexcept>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "fcgistream.hpp"
#include "fastcgi_constants.hpp"


/**
 * Fast CGI
 */
namespace fastcgi
{
	class IOHandler;
	class IOException : public std::runtime_error {};
	class IOSegmentViolationException : public std::runtime_error {};

	/**
     * Low level protocol
     */
    namespace protocol {
        const uint32_t MAX_INT32_SIZE = 0x7fffffff;
        const uint32_t MAX_INT16_SIZE = 0x7fff;
        const uint32_t MAX_BYTE_SIZE = 0x7f;

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

        /**
         * FastCGI protocol variable parsing
         */
        class Variable
        {
        	private:
        		std::string _name;
        		std::string _value;

        		size_t putSize(char *buffer, const size_t& size) const;
        		uint32_t readSize(char *buffer, size_t& readSize) const;

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

        		/**
        		 * Returns the total binary size in the protocol data
        		 */
        		size_t getSize() const;

        		/**
        		 * Put variable into the given buffer
        		 *
        		 * The caller must ensure that the memory from *buffer can
        		 * store this->getSize() bytes
        		 */
        		void putData(char* buffer) const;

        		/**
        		 * Parse content
        		 */
        		static std::vector<Variable> parseFromStream(std::istream in);
        };

        class Message
        {
            public:
                Message(uint16_t id, unsigned char type);
                ~Message();

            private:
                bool canFreeBuffer;
                void freeBuffer();

            protected:
                Header header;

                char* buffer;
                size_t size;

            public:
                /**
                 * Check whether this message is empty
                 */
                bool empty();

                /**
                 * Set the data to send
                 *
                 * Note: Size must not exceed protocol::MAX_INT16_SIZE
                 */
                void setData(const char* data, const size_t& size);

                /**
                 * Returns the total size of this message
                 */
                size_t getSize() const;

                /**
                 * Returns the padding size
                 */
                size_t getPaddingSize() const;

                /**
                 * Short accessor for header.contentLength
                 */
                size_t getContentSize() const;

                /**
                 * Returns the data to send
                 */
                const char* getData() const;
        };

        /**
         * Http Request
         */
        class Request
        {
        	public:
        		typedef std::shared_ptr<Client> ClientPtr;
        		enum class HTTPMethod { GET, SET, PUT, POST, DELETE };
        		enum class Role : uint16_t { RESPONDER = FCGI_RESPONDER, AUTHORIZER = FCGI_AUTHORIZER };

        	protected:
        		uint16_t id;
        		std::map<std::string, std::string> params;
        		std::map<unsigned char, bool> streamStates;
        		Role role;

        		ClientPtr client;

        		void processIncommingRecord(const Record& record);

        	public:
        		Request(const uint16_t& id, ClientPtr client);
        		~Request();

        		void send(protocol::Message& msg);

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

    /**
     * Interface for Handler implementations
     */
    class HandlerInterface
    {

    };

    // Shared pointer to handler interface
    typedef std::shared_ptr<HandlerInterface> HandlerInterfacePtr;

    /**
     * Worker queue
     */
    class WorkerQueue : protected std::queue<HandlerInterfacePtr>
    {
        protected:
            std::mutex protector;

        public:
            void push(HandlerInterfacePtr& ptr);
            HandlerInterfacePtr pop();
    };



    class Client
    {
        public:
            enum StreamType { STDOUT, STDERR, DATA };

        private:
            int socket;

            IOHandler& io;
            std::mutex socketMutex;
            protocol::Record currentRecord;

            size_t headerBytesRead;
            size_t contentBytesRead;
            size_t paddingBytesRead;

            bool headerReady;
            bool contentReady;
            bool paddingReady;

            evbuffer *outputBuffer;

            template<class T> void prepareInRecordSegment(T& segment);
            template<class T> void prepareOutRecordSegment(T& segment);

            char* extractHeader(char* buffer, size_t& size);
            char* extractContent(char* buffer, size_t& size);
            char* extractPadding(char* buffer, size_t& size);

            void dispatch(bufferevent* bev);

        public:
            Client(IOHandler& io, int socket);
            ~Client();

            void onRead(bufferevent *bev, void *arg);
            void write(protocol::Message& message);

    };

    /**
     * Handles FastCGI I/O
     */
    class IOHandler
    {
    	protected:
    		std::vector<std::shared_ptr<Client> > clients;
    		std::map<uint16_t, protocol::Request> requests;

        public:
            IOHandler(std::string bind);
            ~IOHandler();

            void run();
    };
}
