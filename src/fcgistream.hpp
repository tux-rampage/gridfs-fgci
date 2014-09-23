/**
 * FastCGI stream implementations
 */

#pragma once;

#include <iostream>
#include <vector>

namespace fastcgi
{
    class Client;
    class IOHandler;
    class protocol::Request;

    namespace streams {
        struct chunk_t {
            size_t size = 0;
            char* data = NULL;
        };

        class InStreamBuffer : std::streambuf
        {
            friend protocol::Request;

            public:
                inline InStreamBuffer(protocol::Request& request) : request(request), isInitialized(false), std::streambuf() {};
                virtual ~InStreamBuffer();

            protected:
                protocol::Request& request;
                std::vector<chunk_t> chunks;
                std::vector<chunk_t>::iterator current;
                bool isInitialized;

                void addChunk(const char* data, const size_t& size);
                virtual int_type underflow();
                virtual pos_type seekpos(pos_type off, std::ios_base::openmode which = std::ios_base::in);
        };

        class InStream : std::istream
        {
            protected:
                InStreamBuffer _b;

            public:
                InStream(protocol::Request& request);
        };
    }
}
