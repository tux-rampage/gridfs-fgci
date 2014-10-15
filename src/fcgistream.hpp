/**
 * FastCGI stream implementations
 */

#pragma once;

#include <iostream>
#include <vector>

#include "fastcgi_constants.hpp"

namespace fastcgi
{
    class Client;
    class IOHandler;
    class Request;
    struct protocol::Record;

    namespace streams {
        struct chunk_t {
            size_t size = 0;
            char* data = NULL;
        };

        /**
         * Shared pointer to a client
         */
        typedef std::shared_ptr<Client> ClientPtr;


        class ClosableStreamBuffer
        {
            public:
                virtual inline ~ClosableStreamBuffer() {};

            protected:
                bool closed = false;

            public:
                virtual inline void close() {
                    this->closed = true;
                }
        };

        class InStreamBuffer : std::streambuf, ClosableStreamBuffer
        {
            friend Request;

            public:
                inline InStreamBuffer(Request& request) :
                    request(request),
                    isInitialized(false),
                    isComplete(false),
                    std::streambuf() {};

                virtual ~InStreamBuffer();

            protected:
                Request& request;
                std::list<chunk_t> chunks;
                std::list<chunk_t>::iterator current;
                bool isInitialized;
                bool isComplete;

                void addChunk(const protocol::Record& record);
                virtual int_type underflow();
                virtual pos_type seekpos(pos_type off, std::ios_base::openmode which = std::ios_base::in);

            public:
                bool ready() const;
        };

        class OutStreamBuffer : std::streambuf, ClosableStreamBuffer
        {
            using parent = std::streambuf;
            using char_type = typename parent::char_type;
            using int_type = typename parent::int_type;

            friend Request;

            public:
                const static size_t DEFAULT_CHUNKSIZE = 4086;
                enum class role_t : unsigned char { STDOUT = FCGI_STDOUT, STDERR = FCGI_STDERR, VALUES_RESULT = FCGI_GET_VALUES_RESULT };

                OutStreamBuffer(Request& request, const role_t& role, const size_t& chunksize = DEFAULT_CHUNKSIZE);
                OutStreamBuffer(ClientPtr client, uint16_t requestId, const role_t& role, const size_t& chunksize = DEFAULT_CHUNKSIZE);
                virtual ~OutStreamBuffer();

            private:
                char*  chunk;
                size_t chunkSize;

                void resetChunk();

            protected:
                ClientPtr client;
                uint16_t  requestId;

                role_t role;

                // Put Area
                virtual int_type overflow(int_type ch);
                virtual int sync();

            public:
                virtual void close();
        };

        /**
         * FCGI Input stream
         */
        class InStream : public std::istream
        {
            public:
                InStream(Request& request);
                virtual ~InStream();

            public:
                /**
                 * Check if stream is ready
                 */
                bool isReady() const;

                /**
                 * Close the stream buffer
                 */
                void close();
        };

        /**
         * FastCGI Output stream
         */
        class OutStream : public std::ostream
        {
            friend Client;
            using OutStreamBuffer::role_t;

            protected:
                /**
                 * Construct directly with client
                 *
                 * @param[in]  client     The client instance
                 * @param[in]  requestId  The FastCGI request ID
                 * @param[in]  role       The FastCGI role
                 */
                OutStream(ClientPtr client, uint16_t requestId, const role_t& role);

            public:
                /**
                 * @param[in]  request  The FastCGI request
                 * @param[in]  role     The role of this stream
                 */
                OutStream(Request& request, const role_t& role);
                ~OutStream();

                /**
                 * Close this stream
                 */
                void close();
        };
    }
}
