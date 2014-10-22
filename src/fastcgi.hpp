
#pragma once

#include <stdexcept>
#include <queue>
#include <map>
#include <list>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

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
    class Request;
    class Client;

    class IOException : public std::runtime_error {
        public:
            inline IOException(const std::string& msg) : std::runtime_error(msg)
            {}
    };

    class IOSegmentViolationException : IOException {
        public:
            inline IOSegmentViolationException(const std::string& msg) : IOException(msg)
            {};
    };

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
                inline Variable() {};
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

            public:
                /**
                 * Parse content
                 */
                static std::vector<Variable> parseFromStream(std::istream& in);
        };


        std::istream& operator>>(std::istream& in, Variable& var);
        std::ostream& operator<<(std::ostream& out, Variable& var);

        /**
         * Wraps a FastCGI Message
         */
        class Message
        {
            public:
                /**
                 * @param[in]  id    The Request ID
                 * @param[in]  type  The message type id
                 */
                Message(uint16_t id, unsigned char type);
                virtual ~Message();

            protected:
                Header header; ///< Contains the FastCGI header

                /**
                 * Calculates the padding length by the heder's contentLength
                 */
                uint16_t calculatePaddingLength();

            public:
                /**
                 * Check whether the message content is empty or not
                 */
                virtual bool empty() const;

                /**
                 * The raw data representation of this message
                 *
                 * You can get the size of this data via getSize()
                 *
                 * @return Raw message data or NULL, if this must be assembled
                 */
                virtual const char* raw() const;

                /**
                 * Returns the header data
                 */
                const Header& getHeader() const;

                /**
                 * Returns the total size of this message
                 */
                size_t getSize() const;

                /**
                 * Returns the padding size
                 */
                virtual size_t getPaddingSize() const;

                /**
                 * Short accessor for header.contentLength
                 */
                virtual size_t getContentSize() const;

                /**
                 * Returns the data to send
                 */
                virtual const char* getData() const = 0;
        };

        /**
         * Wraps a FastCGI Record
         */
        class GenericMessage : public Message
        {
            public:
                /**
                 * Creates a generic message.
                 *
                 * This will copy the data from the provided data buffer.
                 * If the data pointer is NULL, an empty record will bea generated
                 * (i.e. Indicating eof for fcgi streams)
                 *
                 * @param[in]  id    The request ID for this message
                 * @param[in]  type  The message type
                 * @param[in]  data  Pointer to data
                 * @param[in]  size  Size the data chunk. This must not exceed MAX_INT16_SIZE
                 */
                GenericMessage(uint16_t id, unsigned char type, const char* data, size_t size);

                virtual ~GenericMessage();

            protected:
                size_t size; ///< The size of data buffer
                const char* data; ///< Pointer to data buffer

            public:
                /**
                 * Returns the data to send
                 */
                virtual const char* getData() const;
        };

        /**
         * Wraps EndRequestRecord
         */
        class EndRequestMessage : public Message
        {
            public:
                EndRequestMessage(uint16_t id, uint32_t status, unsigned char fcgiStatus = 0);
                ~EndRequestMessage();

            protected:
                EndRequestRecord record;
                char* _raw;

            public:
                const char* raw() const;
                const char* getData() const;
        };
    };

    /**
     * Abstract request handler
     */
    class RequestHandler
    {
        public:
            RequestHandler(Request& request);
            virtual ~RequestHandler();

        private:
            Request* request; ///< Pointer to the assigned request

        protected:
            /**
             * Gets the associated request
             *
             * @throws ::std::runtime_error When no request instance is assigned (nullptr).
             * @return The associated request
             */
            Request& getRequest() throw (std::runtime_error);

            /**
             * Savely finish the request and invalidate the request reference
             *
             * @param[in]  status  The application exit code
             */
            void finish(uint16_t status);

        public:
            /**
             * Called when a data fragment (STDIN, DATA) is received.
             *
             * By default this is a NOOP dummy
             *
             * @param[in]  record  The received record
             */
            virtual void onReceiveData(const protocol::Record& record);

            /**
             * Called when the server sends FCGI_ABORT_REQUEST
             *
             * By default this will call finish() with status code 1
             */
            virtual void onAbort();

            /**
             * Handle the assigned request
             *
             * @return Returns true when processing the request is complete,
             * @return false if there are more actions to do.
             * @return Returning false allows processing chunk wise allowing other requests
             * @return in the same thread to complete.
             */
            virtual bool handle() = 0;
    };

    typedef ::std::shared_ptr<RequestHandler> RequestHandlerPtr;

    /**
     * Interface for Handler implementations
     */
    class HandlerFactory
    {
        public:
            virtual inline ~HandlerFactory() {};

        /**
         * Interface definition
         */
        public:
            /**
             * Check if the given role is accepted by this handler
             */
            virtual bool acceptRole(uint16_t role);

            /**
             * Create a request handler for the given request
             *
             * @return A smart pointer to the created request handler
             */
            virtual RequestHandlerPtr factory(Request& request) = 0;
    };

    /**
     * Shared pointer to a handler
     */
    typedef std::shared_ptr<HandlerFactory> HandlerFactoryPtr;

    /**
     * Worker callback
     */
    typedef std::function<bool(void)> WorkerCallback;

    /**
     * Shared pointer to worker callback
     */
    typedef std::shared_ptr<WorkerCallback> WorkerCallbackPtr;

    /**
     * Worker queue
     */
    class WorkerQueue : protected std::queue<WorkerCallbackPtr>
    {
        using parent = std::queue<WorkerCallbackPtr>;

        public:
            WorkerQueue();
            ~WorkerQueue();

        protected:
            std::mutex protector;
            std::condition_variable readyCondition;
            bool terminated;
            std::list<std::thread*> threadPool;

        public:
            /**
             * Push an elment to the queue and notify a worker threads
             *
             * @param[in] WorkerCallbackPtr
             */
            void push(WorkerCallbackPtr& ptr);

            /**
             * Remove and return the first element in queue
             *
             * This method must be called in a worker thread, since this call blocks
             * until 1) a handler is pushed to the queue or b) the queue is terminated.
             *
             * The callback pointer might be a null pointer (e.g. on queue termination)
             */
            WorkerCallbackPtr pop();

            /**
             * Terminate the worker queue and tell all handlers to exit
             */
            void terminate();

            /**
             * check if the queue is terminated
             */
            bool isTerminated() const;

            /**
             * Run the worker queue with the given amount of threads.
             *
             * This method will not block and can savely be called from the main thread.
             *
             * @param[in] threadCount  The number of worker threads to create.
             *                         If this param is < 1, the number of threads will be
             *                         guessed std::thread::hardware_concurrency but at minimum 1
             *                         thread is created
             */
            void run(unsigned int threadCount);
    };

    /**
     * Worker Thread
     */
    class Worker
    {
        public:
            Worker(WorkerQueue& queue);
            ~Worker();

        protected:
            WorkerQueue& queue;

        public:
            void operator()();
    };


    /**
     * Http Request
     */
    class Request
    {
        friend streams::OutStreamBuffer;
        friend Client;

        public:
            enum class Role : uint16_t { RESPONDER = FCGI_RESPONDER, AUTHORIZER = FCGI_AUTHORIZER, FILTER = FCGI_FILTER };

        public:
            Request(const uint16_t& id, Role role, ClientPtr client);
            ~Request();

        private:
            streams::InStream paramStream;

        protected:
            uint16_t id;
            std::map<std::string, std::string> params;
            Role role;

            bool valid;
            bool ready;

            // Streams:

            streams::InStream _stdin;
            streams::InStream _datain;
            streams::OutStream _stdout;
            streams::OutStream _stderr;

            // Client ref
            ClientPtr client;
            RequestHandlerPtr handler;

            void processIncommingRecord(const protocol::Record& record);

        public:
            /**
             * Send a message to the fastcgi client
             *
             * @param[in]  msg  The message to send
             */
            void send(protocol::Message& msg);

            /**
             * Send the end request record to the client and mark this request invalid
             *
             * Note: When calling from a handler, the request should be considered as destroyed after the call.
             *
             * @param[in]  status  The application status code
             */
            void finish(uint32_t status);

            /**
             * Set the request handler
             */
            void setHandler(RequestHandlerPtr handler);

            /**
             * get the std stream
             */
            inline streams::InStream& getStdIn()
            {
                return this->_stdin;
            }

            /**
             * get the data stream
             */
            inline streams::InStream& getDataStream()
            {
                return this->_datain;
            }

            /**
             * get the stdout stream
             */
            inline streams::OutStream& getStdOut()
            {
                return this->_stdout;
            }

            /**
             * get the stderr stream
             */
            inline streams::OutStream& getStdErr()
            {
                return this->_stderr;
            }

            /**
             * Get the request id
             */
            inline uint16_t getId() const
            {
                return this->id;
            };

            /**
             * get the request role
             */
            inline Role getRole() const
            {
                return this->role;
            };

            /**
             * Check if this request is valid
             */
            bool isValid();
    };

    /**
     * FastCGI Client connection
     */
    class Client
    {
        public:
            Client(IOHandler& io, int socket);
            ~Client();

        public:
            //! Typedef: map of requestId to request object
            typedef std::map<uint16_t, std::shared_ptr<Request> > RequestMap;

//            enum StreamType { STDOUT, STDERR };

        public:
            /**
             * Helper method for preparing incoming records (performing int endian conversion)
             *
             * @param[in|out] segment
             */
            template<class T> static void prepareInRecordSegment(T& segment);

            /**
             * Helper method for preparing outgoing records (performing int endian conversion)
             *
             * @param[in|out] segment
             */
            template<class T> static void prepareOutRecordSegment(T& segment);

            /**
             * Helper function for libevent callbacks
             *
             * @param[in] event
             * @param[in] ptr   Pointer to the client instance
             */
            static void eventReadCallback(bufferevent* event, void* ptr);

            /**
             * Timer callback to trigger gc
             *
             * @param[in]  fd         Ignored
             * @param[in]  eventType  The event type that was triggered
             * @param[in]  ptr        Pointer to the client instance
             */
            static void eventGcCallback(evutil_socket_t fd, short eventType, void* ptr);


        protected:
            int socket; ///< Socket descriptor

            IOHandler& io; ///< I/O handler instance
            std::mutex socketMutex; ///< Socet protection mutex (for writing)
            protocol::Record currentRecord; ///< the current record being read

            RequestMap requests; ///< current requests
            bool isValid;
            bool keepConnection; ///< Keep the connection alive for further requests

            /**
             * Dispatch events
             */
            void dispatch();

            /**
             * Called when a read event occours
             */
            void onRead(bufferevent*);

            /**
             * Check if there is an active request with the given id
             */
            bool hasRequest(uint16_t id);

            /**
             * Perform garbage collection
             */
            void gc();

        private:
            size_t headerBytesRead;
            size_t contentBytesRead;
            size_t paddingBytesRead;

            bool headerReady;
            bool contentReady;
            bool paddingReady;

            bufferevent *event;

            /**
             * @param[in]      buffer  Pointer to buffer data
             * @param[in|out]  size    total buffer size
             * @return Returns the modified pointer after extracting the header
             */
            char* extractHeader(char* buffer, size_t& size);

            /**
             * @param[in]      buffer  Pointer to buffer data
             * @param[in|out]  size    total buffer size
             * @return Returns the modified pointer after extracting the content
             */
            char* extractContent(char* buffer, size_t& size);

            /**
             * @param[in]      buffer  Pointer to buffer data
             * @param[in|out]  size    total buffer size
             * @return Returns the modified pointer after extracting the padding
             */
            char* extractPadding(char* buffer, size_t& size);

            /**
             * Reset the current record state
             */
            void resetRecordState();

            /**
             * Close the client connection and mark this client invalid
             */
            void destroy();

        public:
            /**
             * Send a message to the client
             *
             * @param[in]  message  The message to send
             */
            void write(protocol::Message& message);

            /**
             * Check validity flag
             */
            inline bool valid() const
            {
                return this->isValid;
            };
    };

    /**
     * Shared Pointer to a client
     */
    typedef std::shared_ptr<Client> ClientPtr;

    /**
     * Handles FastCGI I/O via libevent
     */
    class IOHandler
    {
        friend Client;

        public:
            /**
             * Creates a socket internally and binds it to the given bind specification
             *
             * @param[in]  bind  The Bind specification
             */
            IOHandler(const std::string& bind);

            /**
             * Uses the given file descriptor for listening
             * @param[in]  socket  The file descriptor of the socket to listen on
             */
            IOHandler(int socket);

            /**
             * Performs a shutdown and closes all filedescriptors
             */
            virtual ~IOHandler();

        public:
            /**
             * libevent helper - accept callback
             */
            static void eventAcceptCallback(evconnlistener* event, int fd, sockaddr* clientAddress, int len, void* arg);

            /**
             * Libevent helper method - error callback
             */
            static void eventErrorCallback(evconnlistener* event, void* arg);

            /**
             * Libevent helper - Callback for triggering gc
             *
             * @param[in]  fd  Ignored
             */
            static void eventGcCallback(int fd, short type, void* ptr);

            /**
             * Callback for signals
             */
            static void eventSignalCallback(int signal, short type, void* ptr);


        protected:
            typedef std::vector<ClientPtr> ClientList;

            int fd; ///< Filedescriptor for listening socket

            timeval gcInterval;
            event_base* eventBase; ///< Event base instance
            std::list<event*> eventListeners; ///< Generic events event

            std::vector<HandlerFactoryPtr> handlers; ///< Registered handlers
            ClientList clients; ///< active clients
            WorkerQueue workerQueue;

            std::mutex clientListMutex;

        /**
         * Handler methods
         */
        protected:
            /**
             * Called to accept a new client connection
             */
            virtual void accept(int fd, sockaddr* address, int socketlen);

            /**
             * handle socket errors
             */
            virtual void onError(evconnlistener*);

            /**
             * Run garbage collection
             */
            void gc();

            /**
             * Clear all event listeners
             */
            void clearListeners();

        public:
            /**
             * Add a handler
             *
             * @param[in] factory  A shared pointer to a handler instance
             */
            void addHandlerFactory(HandlerFactoryPtr factory);

            /**
             * Check if any handler factory accepts the given role
             *
             * @param[in] role The FCGI role to check
             */
            bool isRoleAccepted(uint16_t role);

            /**
             * Returns the first handler factory that can handle the given role
             *
             * @param[in] role  The role to find a handler for
             * @return The handler factory implementation
             */
            HandlerFactoryPtr getHandlerFactory(uint16_t role);

            /**
             * Run the I/O event loop
             */
            void run(unsigned int workerCount);

            /**
             * Make a file descriptor non blocking
             * @param[in]  fd  The file descriptor
             * @return true on success, false on failure
             */
            static bool setNonBlocking(int fd);
    };
}
