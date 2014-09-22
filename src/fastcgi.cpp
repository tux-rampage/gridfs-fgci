
#include "fastcgi.hpp";

namespace fastcgi
{
	template<> uint16_t convertBigEndian<uint16_t> (uint16_t value);
	template<> uint32_t convertBigEndian<uint32_t> (uint32_t value);
	template<typename T> T convertBigEndian(T value)
	{
		#if __BYTE_ORDER == __LITTLE_ENDIAN
			T result;
			char* pValue = (char*)&value - 1;
			char* pValueEnd = pValue + sizeof(T);
			char* pResult = (char*)&result + sizeof(T);

			while(pValue != pValueEnd) {
				*--pResult = *++pValue;
			}

			return result;
		#elif __BYTE_ORDER == __BIG_ENDIAN
			return value;
		#endif
	}

	template<> void IOHandler::Client::prepareRecordSegment<protocol::Header>(protocol::Header& segment) {
		segment.contentLength = convertBigEndian(segment.contentLength);
		segment.requestId = convertBigEndian(segment.contentLength);
	}

	template<> void IOHandler::Client::prepareRecordSegment<protocol::BeginRequestBody>(protocol::BeginRequestBody& body)
	{
		body.role = convertBigEndian(body.role);
	}

	template<> void IOHandler::Client::prepareRecordSegment<protocol::EndRequestBody>(protocol::EndRequestBody& segment)
	{
		segment.appStatus = convertBigEndian(segment.appStatus);
	}

	char* IOHandler::Client::extractHeader(char *ptr, size_t &size)
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
	}

	char* IOHandler::Client::extractContent(char* buffer, size_t& size)
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
	}

	char* IOHandler::Client::extractPadding(char* buffer, size_t& size)
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
	}

	void IOHandler::Client::dispatch(bufferevent* bev)
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
	}

	/**
	 * Read data from socket
	 */
	void IOHandler::Client::onRead(bufferevent* bev, void* arg)
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

	}

	size_t protocol::Variable::putSize(char *buffer, const size_t& size) const
	{
		if (size > 127) {
			uint32_t s = convertBigEndian(dynamic_cast<uint32_t>(size));
			memcpy(buffer, &s, sizeof(uint32_t));

			return sizeof(uint32_t);
		}

		unsigned char v = dynamic_cast<unsigned char>(size);
		memcpy(buffer, &v, sizeof(unsigned char));

		return sizeof(unsigned char);
	}

	size_t protocol::Variable::getSize() const
	{
		size_t nameSize = this->getNameSize();
		size_t valueSize = this->getValueSize();
		size_t size = nameSize + valueSize;

		size += (nameSize > 127)? 4 : 1;
		size += (valueSize > 127)? 4 : 1;

		return size;
	}

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
	}
}
