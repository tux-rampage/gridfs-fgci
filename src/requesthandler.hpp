#pragma once

#include <cstdlib>
#include <fastcgi++/request.hpp>
#include <mongo/client/gridfs.h>

namespace gfsfcgi
{
	using mongo::GridFile;
	using mongo::GridFS;

	class ChunkIterator
	{
		protected:
			struct ByteRange {
				std::size_t offset = 0;
				std::size_t size = 0;
			};

			GridFile file;
			unsigned int pos = 0;

			ByteRange* byteRange;

		public:
			ChunkIterator(GridFile& file) : file(file), byteRange(NULL) {};
			virtual inline ~ChunkIterator() {};

			bool next();
			bool valid();

			void setByteRange(const std::size_t& offset, const std::size_t& size);

			unsigned int getDataSize();
			const char* getData();
	};

	class RequestHandler : public Fastcgipp::Request<char>
	{
		protected:
			enum State {START, SENDING, COMPLETE} state;
			GridFS& gridfs;
			ChunkIterator* chunks;

		public:
			inline RequestHandler(GridFS& gridfs) : gridfs(gridfs), chunks(NULL), state(START) {};
			virtual ~RequestHandler();
			bool response();
			bool sendData();
	};
}
