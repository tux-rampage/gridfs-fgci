
#include "fastcgi.hpp";

namespace fastcgi
{
    template<typename T> T convertToBigEndian(T value);
    template<typename T> T convertFromBigEndian(T value);

    void swapBytes(char* from, char* to, size_t size)
    {
        char* pValue = from - 1;
        char* pValueEnd = pValue + size;
        char* pResult = to + size;

        while(pValue != pValueEnd) {
            *--pResult = *++pValue;
        }
    };

	template<> uint16_t convertFromBigEndian<uint16_t> (uint16_t value);
	template<> uint32_t convertFromBigEndian<uint32_t> (uint32_t value);
	template<typename T> T convertFromBigEndian(T value)
	{
		#if __BYTE_ORDER == __LITTLE_ENDIAN
			T result;
			char *ptr = (char*)&result;
			swapBytes((char*)&value, ptr, sizeof(T));
			*ptr = *ptr & 0x7f;
			return result;
		#elif __BYTE_ORDER == __BIG_ENDIAN
			return value;
		#endif
	};

    template<> uint16_t convertToBigEndian<uint16_t> (uint16_t value);
    template<> uint32_t convertToBigEndian<uint32_t> (uint32_t value);
    template<typename T> T convertToBigEndian(T value)
    {
        #if __BYTE_ORDER == __LITTLE_ENDIAN
            T result;
            char *ptr = (char*)&result;
            swapBytes((char*)&value, ptr, sizeof(T));
            *ptr = *ptr | 0x80;

            return result;
        #elif __BYTE_ORDER == __BIG_ENDIAN
            return value;
        #endif
    };

    /////////////////////////////////////////////////////////////////
    // Incomming record preparation (possible big endian conversion)
    //
	template<> void Client::prepareInRecordSegment<protocol::Header>(protocol::Header& segment) {
		segment.contentLength = convertFromBigEndian(segment.contentLength);
		segment.requestId = convertFromBigEndian(segment.contentLength);
	};

	template<> void Client::prepareInRecordSegment<protocol::BeginRequestBody>(protocol::BeginRequestBody& body)
	{
		body.role = convertFromBigEndian(body.role);
	};

	template<> void Client::prepareInRecordSegment<protocol::EndRequestBody>(protocol::EndRequestBody& segment)
	{
		segment.appStatus = convertFromBigEndian(segment.appStatus);
	};


    /////////////////////////////////////////////////////////////////
    // Outgoing record preparation (possible big endian conversion)
    //

    template<> void Client::prepareOutRecordSegment<protocol::Header>(protocol::Header& segment) {
        segment.contentLength = convertToBigEndian(segment.contentLength);
        segment.requestId = convertToBigEndian(segment.contentLength);
    };

    template<> void Client::prepareOutRecordSegment<protocol::BeginRequestBody>(protocol::BeginRequestBody& body)
    {
        body.role = convertToBigEndian(body.role);
    };

    template<> void Client::prepareOutRecordSegment<protocol::EndRequestBody>(protocol::EndRequestBody& segment)
    {
        segment.appStatus = convertToBigEndian(segment.appStatus);
    };

    /**
     * Header extraction from data pointer
     */
	char* Client::extractHeader(char *ptr, size_t &size)
	{
		if (this->headerReady) {
			return ptr;
		}

		size_t reqBytes = sizeof(this->currentRecord.header) - this->headerBytesRead;
		char* pTo = (char*)&this->currentRecord.header;

		if (this->headerBytesRead > 0) {
			pTo += this->headerBytesRead;
		}

		if (reqBytes < size) {
			memcpy(pTo, ptr, reqBytes);
			ptr += reqBytes;
			size -= reqBytes;
			this->headerBytesRead += reqBytes;
		} else {
			memcpy(pTo, ptr, size);
			this->headerBytesRead += size;
			size = 0;
		}

		if (this->headerBytesRead >= sizeof(this->currentRecord.header)) {
			this->headerReady = true;
		}

		return ptr;
	};

	/**
	 * Content extraction from data pointer
	 */
	char* Client::extractContent(char* buffer, size_t& size)
	{
		if (!this->headerReady || (size <= 0)) {
			return buffer;
		}

		if (this->currentRecord.header.contentLength == 0) {
			this->contentReady = true;
			return buffer;
		}

		if (this->currentRecord.content == NULL) {
			this->currentRecord.content = new char[this->currentRecord.header.contentLength];
		}

		char* pTo = this->currentRecord.content;
		size_t reqSize = this->currentRecord.header.contentLength - this->contentBytesRead;

		if (this->contentBytesRead) {
			pTo += this->contentBytesRead;
		}

		if (size > reqSize) {
			memcpy(pTo, buffer, reqSize);
			buffer += reqSize;
			size -= reqSize;
			this->contentBytesRead += reqSize;
		} else {
			memcpy(pTo, buffer, size);
			size = 0;
			this->contentBytesRead += size;
		}

		if (this->contentBytesRead >= this->currentRecord.header.contentLength) {
			this->contentReady = true;
		}

		return buffer;
	};

	/**
	 * Padding extraction from header
	 */
	char* Client::extractPadding(char* buffer, size_t& size)
	{
		if (!this->headerReady || !this->contentReady || (size <= 0)) {
			return buffer;
		}

		size_t padSize = this->currentRecord.header.paddingLength - this->paddingBytesRead;

		if (size >= padSize) {
			if (padSize && (size > padSize)) {
				buffer += padSize;
			}

			size -= padSize;
			this->paddingReady = true;
		} else {
			this->paddingBytesRead += size;
			size = 0;
		}

		return buffer;
	};

	void Client::dispatch(bufferevent* bev)
	{
		if (this->currentRecord.header.type == FCGI_GET_VALUES) {
			protocol::Header header;
			protocol::Variable _var(FCGI_MPXS_CONNS, "1");

			header.requestId = FCGI_NULL_REQUEST_ID;
			header.contentLength = _var.getSize();
			header.paddingLength = (header.contentLength % 8);

			size_t size = sizeof(header) + header.contentLength + header.paddingLength;
			char *data = new char[size];
			char *content = data + sizeof(header);

			memcpy(data, &header, sizeof(header));

			_var.putData(content);
			evbuffer_add(this->outputBuffer, data, size);

			delete[] data;
			return;
		}

		// TODO: Implement request dispatch
	};

	/**
	 * Read data from socket
	 */
	void Client::onRead(bufferevent* bev, void* arg)
	{
		auto input = bufferevent_get_input(bev);
		char buffer[1024];
		size_t size = 0;

		while (size = (size_t)evbuffer_remove(input, (void*)&buffer, 1024)) {
			char *pFrom = &buffer;

			while (size) {
				pFrom = this->extractHeader(pFrom, size);
				pFrom = this->extractContent(pFrom, size);
				pFrom = this->extractPadding(pFrom, size);

				if (this->paddingReady) {
					this->dispatch(bev);

					delete[] this->currentRecord.content;

					this->currentRecord.content = NULL;
					this->headerReady = false;
					this->contentReady = false;
					this->paddingReady = false;
					this->headerBytesRead = 0;
					this->contentBytesRead = 0;
					this->paddingBytesRead = 0;
				}
			}
		}

	};

	size_t protocol::Variable::putSize(char *buffer, const size_t& size) const
	{
		if (size > MAX_BYTE_SIZE) {
			uint32_t s = convertFromBigEndian(dynamic_cast<uint32_t>(size));
			memcpy(buffer, &s, sizeof(int32_t));

			return sizeof(uint32_t);
		}

		unsigned char v = dynamic_cast<unsigned char>(size);
		memcpy(buffer, &v, sizeof(unsigned char));

		return sizeof(unsigned char);
	};

	size_t protocol::Variable::getSize() const
	{
		size_t nameSize = this->getNameSize();
		size_t valueSize = this->getValueSize();
		size_t size = nameSize + valueSize;

		size += (nameSize > MAX_BYTE_SIZE)? 4 : 1;
		size += (valueSize > MAX_BYTE_SIZE)? 4 : 1;

		return size;
	};

	void protocol::Variable::putData(char* buffer) const
	{
		size_t nameSize = this->getNameSize();
		size_t valueSize = this->getValueSize();
		size_t size = this->getSize();

		buffer += this->putSize(buffer, nameSize);
		buffer += this->putSize(buffer, valueSize);

		memcpy(buffer, this->_name.c_str(), nameSize);
		buffer += nameSize;

		memcpy(buffer, this->_value.c_str(), valueSize);
	};

	protocol::Message::Message(uint16_t id, unsigned char type) : buffer(NULL), size(0), pos(0), pData(NULL)
	{
	    this->header.requestId = id;
	    this->header.type = type;
	    this->header.version = FCGI_VERSION_1;
	    this->header.reserved = 0;
	    this->header.contentLength = 0;
	    this->header.paddingLength = 0;
	};

	protocol::Message::~Message()
    {
    };

    void protocol::Message::setData(char* data, size_t size)
    {
        this->size = (size <= protocol::MAX_INT16_SIZE)? size : protocol::MAX_INT16_SIZE;
        this->buffer = data;
    };

    bool protocol::Message::empty()
    {
        return (this->pos >= this->size);
    };

    size_t protocol::Message::getSize() const
    {
        if (this->header.type == FCGI_GET_VALUES_RESULT) {
            // TODO
        } else {
            this->header.contentLength = this->size;
            this->header.paddingLength = this->header.contentLength % 8;
        }

        return sizeof(this->header) + this->header.contentLength + this->header.paddingLength;
    };

    /**
     * Put message data into protocol
     */
    void protocol::Message::putData(char* buffer) const
    {
        if (this->header.type == FCGI_GET_VALUES_RESULT) {
            // TODO
        }

        if (this->empty()) {
            throw gfsfcgi::IOException("End of message data");
        }

        memcpy(buffer, &this->header, sizeof(this->header));
        buffer += sizeof(this->header);

        memcpy(buffer, this->pData, dynamic_cast<size_t>(this->header.contentLength));
        buffer += this->header.contentLength;

        memset(buffer, 0, dynamic_cast<size_t>(this->header.paddingLength));
    };

	/**
	 * Write chunk implementation
	 */
    void Client::write(protocol::Request request, StreamType type, const char* data, const size_t& size)
    {
        while (size) {
            protocol::Header header;
            header.requestId = request.getId();
            header.reserved = 0;
            header.version = FCGI_VERSION_1;

            if (size <= protocol::MAX_INT16_SIZE) {
                header.contentLength = size;
                size = 0;
            } else {
                header.contentLength = protocol::MAX_INT16_SIZE;
                size -= header.contentLength;
            }

            header.paddingLength = header.contentLength % 8;

            switch (type) {
                case StreamType::STDOUT:
                    header.type = FCGI_STDOUT;
                    break;

                case StreamType::STDERR:
                    header.type = FCGI_STDERR;
                    break;

                default:
                    header.type = FCGI_DATA;
                    break;
            }
        }
    };
}
