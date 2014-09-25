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
    class protocol::Request;
    struct protocol::Record;


    namespace streams {
        struct chunk_t {
            size_t size = 0;
            char* data = NULL;
        };

        class InStreamBuffer : std::streambuf
        {
            friend protocol::Request;

            public:
                inline InStreamBuffer(protocol::Request& request) :
                	request(request),
                	isInitialized(false),
                	isComplete(false),
                	std::streambuf() {};

                virtual ~InStreamBuffer();

            protected:
                protocol::Request& request;
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

        class OutStreamBuffer : std::streambuf
		{
        	using parent = std::streambuf;
        	using char_type = typename parent::char_type;
        	using int_type = typename parent::int_type;

			friend protocol::Request;

			public:
				const static size_t DEFAULT_CHUNKSIZE = 4086;
				enum class role_t : unsigned char { STDOUT = FCGI_STDOUT, STDERR = FCGI_STDERR };

				OutStreamBuffer(protocol::Request& request, const role_t& role, const size_t& chunksize = DEFAULT_CHUNKSIZE);
				virtual ~OutStreamBuffer();

			private:
				char*  chunk;
				size_t chunkSize;

				void resetChunk();

			protected:
				protocol::Request& request;
				role_t role;

				// Put Area
				virtual int_type overflow(int_type ch);
				virtual int sync();
		};

        class InStream : std::istream
        {
            protected:
                InStreamBuffer _b;

            public:
                InStream(protocol::Request& request);
                bool isReady() const;
        };
    }
}
