
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

    /////////////////////////////////////////////////////////////////////////////////////
    // Client Impl
    //

	Client::Client(IOHandler& io, int socket)
	{
	}

	Client::~Client()
	{
	}

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

	template<class T> void Client::prepareInRecordSegment(T& segment)
	{
	}


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

	template<class T> void Client::prepareOutRecordSegment(T& segment)
	{
	}


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


	/**
	 * Write chunk implementation
	 */
    void Client::write(protocol::Message &message)
    {
    	std::lock_guard guard(this->socketMutex);

    	// TODO Put to buffer
    	// evbuffer_add(buf, &message.getHeader(), message.getHeaderSize());

    	if (message.getSize()) {
    		// evbuffer_add(buf, message.getData(), message.getContentSize());

    		if (message.getPaddingSize()) {
    			char* padding = new char[message.getPaddingSize()];
    			memset(padding, 0, message.getPaddingSize());

    			// evbuffer_add(buf, padding, message.getPaddingSize());

    			delete [] padding;
    		}
    	}
    };


	///////////////////////////////////////////////////////////////////////////
	//
	// Protocol specific
	//
	///////////////////////////////////////////////////////////////////////////

	namespace protocol {

		////////////////////////
		// Variable Container

		std::vector<Variable> protocol::Variable::parseFromStream(std::istream in)
		{
			std::vector<Variable> result;
			return result;
		}

		size_t Variable::putSize(char *buffer, const size_t& size) const
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

		size_t Variable::getSize() const
		{
			size_t nameSize = this->getNameSize();
			size_t valueSize = this->getValueSize();
			size_t size = nameSize + valueSize;

			size += (nameSize > MAX_BYTE_SIZE)? 4 : 1;
			size += (valueSize > MAX_BYTE_SIZE)? 4 : 1;

			return size;
		};

		void Variable::putData(char* buffer) const
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


		//////////////////////////////////////////////////////////////////
		// Message impl
		//

		Message::Message(uint16_t id, unsigned char type) : buffer(NULL), size(0), canFreeBuffer(false)
		{
			this->header.requestId = id;
			this->header.type = type;
			this->header.version = FCGI_VERSION_1;
			this->header.reserved = 0;
			this->header.contentLength = 0;
			this->header.paddingLength = 0;
		};

		Message::~Message()
		{
			this->freeBuffer();
		};

		void Message::freeBuffer()
		{
			if (this->canFreeBuffer && (this->buffer != NULL)) {
				delete [] this->buffer;
				this->buffer = NULL;
				this->size = 0;
			}
		}

		void Message::setData(const char* data, const size_t& size)
		{
			if (size <= protocol::MAX_INT16_SIZE) {
				throw new IOException("Chuck size too large.");
			}

			this->size = size;
			this->buffer = data;
		};

		bool Message::empty()
		{
			return (this->size == 0);
		}

		size_t Message::getPaddingSize() const
		{
			size_t mod = this->size % 8;
			return (mod == 0)? 0 : 8 - mod;
		}

		size_t Message::getSize() const
		{
			return sizeof(this->header) + this->getSize() + this->getPaddingSize();
		};

		size_t Message::getContentSize() const
		{
			return this->size;
		}

		/**
		 * Return raw message data
		 */
		const char* Message::getData() const
		{
			return const_cast<const char*>(this->buffer);
		};


		///////////////////////////////////////////////////
		// Request Impl
		//

		void Request::processIncommingRecord(const Record& record)
		{
			// TODO
		}

		Request::Request(const uint16_t& id, ClientPtr client) : client(client), id(id), role(Role::RESPONDER)
		{
		}

		protocol::Request::~Request()
		{
		}

		void protocol::Request::send(protocol::Message& msg)
		{
			this->client->write(msg);
		}

	}
}
