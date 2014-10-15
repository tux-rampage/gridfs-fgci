/**
 * FastCGI stream implementation
 */

#include "fastcgi.hpp"

namespace fastcgi
{
    namespace streams
    {
        InStreamBuffer::~InStreamBuffer()
        {
            for (auto& chunk : this->chunks) {
                delete [] chunk.data;
            }

            this->chunks.clear();
        }

        void InStreamBuffer::addChunk(const protocol::Record& record)
        {
            // Stream is closed
            if (this->closed) {
                return;
            }

            if (record.header.contentLength == 0) {
                // Received EOF
                this->isComplete = true;
                return;
            }

            size_t size = record.header.contentLength;
            chunk_t chunk;
            chunk.data = new char[size];
            chunk.size = size;

            memcpy(chunk.data, record.content, size);
            this->chunks.push_back(chunk);
        }

        std::streambuf::int_type InStreamBuffer::underflow()
        {
            if (this->closed) {
                return traits_type::eof();
            }

            if (!this->isInitialized) {
                this->current = this->chunks.begin();
            } else if (this->current != this->chunks.end()) {
                this->current++;
            }

            if (this->current == this->chunks.end()) {
                return traits_type::eof();
            }

            this->setg(this->current->data, this->current->data, this->current->data + this->current->size);
            return *(this->current->data);
        }

        std::streambuf::pos_type InStreamBuffer::seekpos(pos_type off, std::ios_base::openmode which)
        {
            if (this->closed || !(which & std::ios_base::in)) {
                return pos_type(off_type(-1));
            }

            this->current = this->chunks.begin();
            size_t current = 0;

            while (this->current != this->chunks.end()) {
                current += this->current->size;

                if (current <= off) {
                    this->current++;
                    continue;
                }

                size_t offset = off - (current - this->current->size);
                char* pOffset = this->current->data + offset;
                this->setg(this->current->data, pOffset, this->current->data + this->current->size);

                return *pOffset;
            }

            return pos_type(off_type(-1));
        }

        bool InStreamBuffer::ready() const
        {
            return this->isComplete;
        }


        // Output Buffer

        OutStreamBuffer::OutStreamBuffer(ClientPtr client, uint16_t requestId, const role_t& role, const size_t& chunksize) :
            role(role),
            chunkSize(chunksize),
            requestId(requestId),
            client(client)
        {
            if (role == role_t::VALUES_RESULT) {
                this->requestId = 0;
            }

            this->chunk = new char[this->chunkSize];
            this->resetChunk();
        }

        OutStreamBuffer::OutStreamBuffer(Request& request, const role_t& role, const size_t& chunksize) : OutStreamBuffer(request.client, request.getId(), role, chunksize)
        {
        }

        OutStreamBuffer::~OutStreamBuffer()
        {
            this->setp(NULL, NULL);
            delete [] this->chunk;

            this->chunk = NULL;
        }

        void OutStreamBuffer::resetChunk()
        {
            memset(this->chunk, 0, this->chunkSize);
            this->setp(this->chunk, this->chunk + this->chunkSize);
        }

        std::streambuf::int_type OutStreamBuffer::overflow(int_type ch)
        {
            if (this->sync() != 0) {
                this->setp(NULL, NULL);
                return traits_type::eof();
            }

            if (!traits_type::eq_int_type(ch, traits_type::eof())) {
                *this->chunk = (char)ch;
            }

            return 1;
        }

        int OutStreamBuffer::sync()
        {
            if (this->closed) {
                return -1;
            }

            protocol::GenericMessage msg(this->requestId, this->role, this->chunk, this->chunkSize);
            this->client->write(msg);
            this->resetChunk();

            return 0;
        }

        /**
         * Close the output stream buffer
         */
        void OutStreamBuffer::close()
        {
            this->sync();

            // Send EOF
            protocol::GenericMessage msg(this->requestId, this->role, NULL, 0);
            this->client->write(msg);

            ClosableStreamBuffer::close();
        }


        // Stream Impl

        InStream::InStream(Request& request)
        {
            std::shared_ptr<InStreamBuffer> b(new InStreamBuffer(request));
            std::istream(b);
        }

        InStream::~InStream()
        {}

        bool InStream::isReady() const
        {
            InStreamBuffer* buf = dynamic_cast<InStreamBuffer*>(this->rdbuf());
            if (buf == NULL) {
                return false;
            }

            return buf->ready();
        }

        void InStream::close()
        {
            InStreamBuffer* buf = dynamic_cast<InStreamBuffer*>(this->rdbuf());

            if (buf != NULL) {
                buf->close();
            }
        }


        OutStream::OutStream(ClientPtr client, uint16_t requestId, const role_t& role)
        {
            std::shared_ptr<OutStreamBuffer> b(new OutStreamBuffer(client, requestId, role));
            std::ostream(b);
        }

        OutStream::OutStream(Request& request, const role_t& role)
        {
            std::shared_ptr<OutStreamBuffer> b(new OutStreamBuffer(request, role));
            std::ostream(b);
        }

        OutStream::~OutStream()
        {}

        void OutStream::close()
        {
            OutStreamBuffer* buf = dynamic_cast<OutStreamBuffer*>(this->rdbuf());

            if (buf != NULL) {
                buf->close();
            }
        }
    }
}
