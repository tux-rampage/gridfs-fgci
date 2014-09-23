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

        void InStreamBuffer::addChunk(const char* data, const size_t& size)
        {
            chunk_t chunk;
            chunk.data = new char[size];
            chunk.size = size;

            memcpy(chunk.data, data, size);
            this->chunks.push_back(chunk);
        }

        std::streambuf::int_type InStreamBuffer::underflow()
        {
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
            if (!(which & std::ios_base::in)) {
                return pos_type(off_type(-1));
            }

            this->isInitialized;
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

        InStream::InStream(protocol::Request& request) : _b(request), std::istream(&_b)
        {}
    }
}

