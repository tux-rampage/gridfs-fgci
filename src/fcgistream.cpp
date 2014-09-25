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
        	if (record.header.contentLength == 0) {
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

        bool InStreamBuffer::ready() const
		{
			return this->isComplete;
		}


        // Output Buffer

        OutStreamBuffer::OutStreamBuffer(protocol::Request& request, const role_t& role, const size_t& chunksize) :
			request(request),
			role(role),
			chunkSize(chunksize)
        {
        	this->chunk = new char[this->chunkSize];
        	this->resetChunk();
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

        	if (ch != traits_type::eof()) {
        		*this->chunk = (char)ch;
        	}

        	return 1;
        }

        int OutStreamBuffer::sync()
        {
        	protocol::Message msg(this->request.getId(), this->role);

        	msg.setData(this->chunk, this->chunkSize);
        	this->request.send(msg);
        	this->resetChunk();

        	return 0;
        }


        // Stream Impl

        InStream::InStream(protocol::Request& request) : _b(request), std::istream(&_b)
        {}

        bool InStream::isReady() const
        {
        	InStreamBuffer* buf = dynamic_cast<InStreamBuffer*>(this->rdbuf());
        	if (buf == NULL) {
        		return false;
        	}

        	return buf->ready();
        }
    }
}

