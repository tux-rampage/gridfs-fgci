
#include <functional>
#include <regex>
#include <sstream>

#include "fastcgi.hpp"

/**
 * FastCGI implementation
 */
namespace fastcgi
{
    ////////////////////////////////////////////////////////////////////////////77
    //
    // Convert Helper functions
    //

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
    //
    // libEvent - Helper functions
    //

    /**
     * Typedef to std::function for libevent buffer callbacks
     */
    typedef std::function<void(bufferevent*)> BufferEventCallback;

    /**
     * Typedef to std::function for libevent event callbacks
     */
    typedef std::function<void(bufferevent*, short)> GenericEventCallback;

    /**
     * callback function for libevent where arg should be a pointer to a BufferEventCallback
     *
     * @param[in]  event  Pointer to the libevent struct
     * @param[in]  arg    This should be a pointer to an BufferEventCallback
     */
    void libevent_bind_rw_callback(bufferevent* event, void* arg)
    {
        BufferEventCallback* cb = dynamic_cast<BufferEventCallback*>(arg);

        if (cb != NULL) {
            (*cb)(event);
        } else {
            std::cerr << "Bad buffer r/w event callback in arg " << arg << "!" << std::endl;
        }
    }

    /**
     * Called by libevent when an event on the underlying socket occours.
     *
     * @param[in]  event  Pointer to the libevent struct
     * @param[in]  events Event codes
     * @param[in]  arg    This should be a pointer to an ErrorEventCallback
     */
    void libevent_bind_error_callback(bufferevent* event, short events, void *arg)
    {
        GenericEventCallback* cb = dynamic_cast<GenericEventCallback*>(arg);

        if (cb != NULL) {
            (*cb)(event, events);
        } else {
            std::cerr << "Bad generic event callback in arg " << arg << "!" << std::endl;
        }
    }



    /////////////////////////////////////////////////////////////////////////////////////
    // Client Impl
    //

    Client::Client(IOHandler& io, int socket) : io(io),
            socket(socket),
            paddingBytesRead(0),
            headerBytesRead(0),
            contentBytesRead(0),
            contentReady(false),
            headerReady(false),
            paddingReady(false),
            isValid(true)
    {
        // bind placeholders like _1, _2, _3 ...
        using namespace std::placeholders;

        if (!IOHandler::setNonBlocking(socket)) {
            throw IOException("Failed to make socket fd non-blocking.");
        }

        this->event = bufferevent_socket_new(io.eventBase, socket, BEV_OPT_DEFER_CALLBACKS | BEV_OPT_CLOSE_ON_FREE);

        if (this->event == NULL) {
            throw IOException("Failed to allocate event buffer for client");
        }

        this->readcb = std::function(std::bind(Client::onRead, this, _1));
        bufferevent_setcb(this->event, this->readcb, NULL, NULL, this);
        bufferevent_enable(this->event, EV_READ|EV_WRITE);
    }

    Client::~Client()
    {
        if (this->event != NULL) {
            bufferevent_free(this->event);
        }
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

    template<> void Client::prepareOutRecordSegment<protocol::EndRequestRecord>(protocol::EndRequestRecord& segment)
    {
        this->prepareOutRecordSegment(segment.header);
        this->prepareOutRecordSegment(segment.body);
    }

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

    void Client::dispatch()
    {
        if (this->currentRecord.header.type == FCGI_GET_VALUES) {
            streams::OutStream s(this, FCGI_NULL_REQUEST_ID, streams::OutStreamBuffer::role_t::VALUES_RESULT);

            protocol::Variable _var(FCGI_MPXS_CONNS, "1");
            s << _var;
            s.close();

            return;
        }

        // TODO: Implement request dispatch
    };

    /**
     * Read data from socket
     */
    void Client::onRead(bufferevent* event)
    {
        char buffer[1024];
        size_t size = 0;

        while (size = (size_t)bufferevent_read(event, (void*)&buffer, 1024)) {
            char *pFrom = &buffer;

            while (size) {
                pFrom = this->extractHeader(pFrom, size);
                pFrom = this->extractContent(pFrom, size);
                pFrom = this->extractPadding(pFrom, size);

                if (this->paddingReady) {
                    this->dispatch();

                    delete[] this->currentRecord.content;

                    memset(&this->currentRecord, 0, sizeof(this->currentRecord));

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
        const char *raw = message.raw();

        if (raw != NULL) {
            std::lock_guard guard(this->socketMutex);
            bufferevent_write(this->event, raw, message.getSize());

            return;
        }

        protocol::Header header = message.getHeader();
        header.version = FCGI_VERSION_1;
        prepareOutRecordSegment(header);

        std::lock_guard guard(this->socketMutex);
        bufferevent_write(this->event, &header, sizeof(header));

        if (message.getContentSize()) {
            bufferevent_write(this->event, message.getData(), message.getContentSize());
        }

        if (message.getPaddingSize()) {
            char* padding = new char[message.getPaddingSize()];
            memset(padding, 0, message.getPaddingSize());

            bufferevent_write(this->event, padding, message.getPaddingSize());
            delete [] padding;
        }
    };

    ///////////////////////////////////////////////////
    // Request Impl
    //
    void Request::processIncommingRecord(const protocol::Record& record)
    {
        if (!this->valid) {
            return;
        }

        switch (record.header.type) {
            case FCGI_ABORT_REQUEST:
                this->valid = true;
                break;

            case FCGI_PARAMS:
                streams::InStreamBuffer* buf = dynamic_cast<streams::InStreamBuffer*>(this->_params.rdbuf());
                if (buf == NULL) {
                    break;
                }

                buf->addChunk(record);

                if (this->_params.isReady()) {
                    for (auto p : protocol::Variable::parseFromStream(this->_params)) {
                        this->params[p.name()] = p.value();
                    }

                    if (this->role != Role::FILTER) {
                        this->ready = true;
                    }
                }

                break;

            case FCGI_STDIN: // break intentionally omitted
            case FCGI_DATA:
                streams::InStreamBuffer* buf = (record.header.type == FCGI_STDIN)?
                        dynamic_cast<streams::InStreamBuffer*>(this->_stdin.rdbuf()) :
                        dynamic_cast<streams::InStreamBuffer*>(this->_datain.rdbuf());

                if (buf != NULL) {
                    buf->addChunk(record);
                }


                break;

            default:
                break;
        }
    }

    Request::Request(const uint16_t& id, ClientPtr client) :
            client(client),
            id(id),
            role(Role::RESPONDER),
            valid(false),
            ready(false),
            _params(*this),
            _stdin(*this),
            _datain(*this),
            _stdout(*this, streams::OutStreamBuffer::role_t::STDOUT),
            _stderr(*this, streams::OutStreamBuffer::role_t::STDERR)
    {
    }

    Request::~Request()
    {
    }

    void Request::send(protocol::Message& msg)
    {
        this->client->write(msg);
    }

    void Request::finish(uint32_t status)
    {
        protocol::EndRequestMessage end(this->getId(), status, 0);
        this->client->write(end);
    }

    /////////////////////////////////////////////////////////////////////
    //
    // I/O Handler
    //

    IOHandler::IOHandler(const std::string& bind) : IOHandler(0)
    {
        std::regex ipv4regex("^(\d{1,3}(?:\.\d{1,3}){3})(\:([1-9][0-9]*))?$");
        std::smatch m;

        sockaddr_un bindUnix;
        sockaddr_in bindIPv4;
        sockaddr_in6 bindIPv6;
        sockaddr* bindAddr = NULL;
        size_t bindAddrLen = 0;

        if (bind.substr(0, 5) == "unix:") {
            if (bind.size() - 5 > 108) {
                throw IOException("Unix path name too long for socket.");
            }

            memset(&bindUnix, 0, sizeof(bindUnix));
            memcpy(&bindUnix.sun_path, bind.substr(5).c_str(), bind.size());

            bindUnix.sun_family = AF_UNIX;
            bindAddr = (sockaddr*)&bindUnix;
            bindAddrLen = sizeof(bindUnix);

            this->fd = socket(AF_LOCAL, SOCK_STREAM, 0);
        } else if (std::regex_match(bind, m, ipv4regex)){
            int port = 9800;

            if ((m.size() >= 3) && m[3].matched) {
                std::istringstream iss(m[3].str());
                iss >> port;
            }

            bindIPv4.sin_family = AF_INET;
            bindIPv4.sin_port = htons(port);

            if (m[1].str() == "0.0.0.0") {
                bindIPv4.sin_addr.s_addr = INADDR_ANY;
            } else {
                // TODO: Bind specific address
                bindIPv4.sin_addr.s_addr = INADDR_ANY;
            }

            this->fd = socket(AF_INET, SOCK_STREAM, 0);
        // } else if () { // TODO: IPv6
        } else {
            std::ostringstream oss;
            oss << "Invalid bind expression: \"" << bind << "\"";
            throw IOException(oss.str());
        }

        if (bind(this->fd, bindAddr, bindAddrLen) < 0) {
            close(this->fd);
            std::ostringstream oss;
            oss << "Failed to bind socket to \"" << bind << "\"";
            throw IOException(oss.str());
        }

        if (!setNonBlocking(this->fd)) {
            throw IOException("Failed to make listener socket non blocking");
        }
    }

    IOHandler::IOHandler(int socket) : event(NULL), fd(socket)
    {
        this->eventBase = event_base_new();
        this->acceptcb = [this] (evconnlistener* listener, int fd, sockaddr* address, int socklen, void* ctx) {
            this->accept(fd, address, socklen);
        };
    }

    IOHandler::~IOHandler()
    {
        event_base_free(this->eventBase);
    }

    void IOHandler::addHandler(HandlerPtr handler)
    {
        this->handlers.push_back(handler);
    }

    bool IOHandler::isRoleAccepted(uint16_t role)
    {
        for (auto handler : this->handlers) {
            if (handler->acceptRole(role)) {
                return true;
            }
        }

        return false;
    }

    HandlerPtr IOHandler::getHandler(uint16_t role)
    {
        for (auto handler : this->handlers) {
            if (handler->acceptRole(role)) {
                return handler;
            }
        }

        std::ostringstream oss;
        oss << "Could not find any handler to handle role \"" << role << "\".";

        throw IOException(oss.str());
    }

    void IOHandler::run()
    {
        this->event = bufferevent_socket_new(this->eventBase, this->fd, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
        auto listener = evconnlistener_new(this->eventBase, this->acceptcb, NULL, LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE, -1, this->fd);

        if (!listener) {
            throw IOException("Could not initialize listener");
        }

        event_base_dispatch(this->eventBase);
        evconnlistener_free(listener);
    }

    void IOHandler::accept(int fd, sockaddr* address, int socketlen)
    {
        ClientPtr client(new Client(*this, fd));

        std::lock_guard guard(this->clientListMutex);
        this->clients.push_back(client);
    }

    void IOHandler::gc()
    {
        std::lock_guard guard(this->clientListMutex);

        for (auto it = this->clients.begin(); it != this->clients.end(); it++) {
            if (!(*it)->valid()) {
                this->clients.erase(it);
            }
        }
    }

    static bool IOHandler::setNonBlocking(int fd)
    {
        return (evutil_make_socket_nonblocking(fd) == 0);
    }


    // Responder is the only supported role by default
    bool Handler::acceptRole(uint16_t role)
    {
        return (role == FCGI_RESPONDER);
    }


    ///////////////////////////////////////////////////////////////
    //
    // Worker queue
    //

    void WorkerQueue::push(WorkerCallbackPtr& ptr)
    {
        std::lock_guard guard(this->protector);
        parent::push(ptr);
    }

    WorkerCallbackPtr WorkerQueue::pop()
    {
        std::lock_guard guard(this->protector);

        if (this->empty()) {
            return WorkerCallbackPtr(NULL);
        }

        WorkerCallbackPtr ptr = this->front();
        parent::pop();

        return ptr;
    }

    ///////////////////////////////////////////////////////////////////////////
    //
    // Protocol specific
    //
    ///////////////////////////////////////////////////////////////////////////

    namespace protocol {

        ////////////////////////
        // Variable Container

        int32_t readSizeFromStream(std::istream& in)
        {
            if (!in.good()) {
                throw std::runtime_error("Failed to read variable size");
            }

            char s;
            uint32_t size = 0;

            in >> s;

            if (!in.good()) {
                throw std::runtime_error("Failed to read variable size");
            }

            if (s >> 7) { // High value bit
                char *sbuf = new char[3];
                char* p = (char*)&size;

                in.get(sbuf, 3);

                if (!in.good()) {
                    delete [] sbuf;
                    throw std::runtime_error("Failed to read variable size");
                }

                memcpy(p++, &s, 1);
                memcpy(p, sbuf, 3);

                delete [] sbuf;
                size = convertFromBigEndian(size);
            } else {
                size = (uint32_t)s;
            }

            return size;
        }

        std::ostream& operator<<(std::ostream& out, Variable& var)
        {
            if (var.getSize() <= 0) {
                return out;
            }

            char* buffer = new char[var.getSize()];

            var.putData(buffer);
            out.write(buffer, var.getSize());

            delete [] buffer;
            return out;
        }

        /**
         * Read var from stream operator
         */
        std::istream& operator>>(std::istream& in, Variable& var)
        {
            uint32_t nameSize = readSizeFromStream(in);
            uint32_t valueSize = readSizeFromStream(in);

            bool  good = false;
            char* name = new char[nameSize + 1];
            char* value = new char[valueSize + 1];

            in.read(name, nameSize);
            if (in.good()) {
                in.read(value, valueSize);

                if (valueSize != in.gcount()) {
                    delete [] name;
                    delete [] value;

                    throw std::runtime_error("Failed to read variable data from fcgi stream");
                }
            }

            var.name(std::string(name));
            var.value(std::string(value));

            delete [] name;
            delete [] value;

            return in;
        }

        uint32_t Variable::readSize(char* buffer, size_t& readSize) const
        {
            if (!(*buffer >> 7)) {
                return (uint32_t)*buffer;
            }

            if (readSize < sizeof(uint32_t)) {
                throw IOSegmentViolationException("Cannot read variable size from buffer (buffer too small).");
            }

            uint32_t size = 0;
            memcpy(&size, buffer, sizeof(uint32_t));
            size = convertFromBigEndian(size);

            return size;
        }

        std::vector<Variable> protocol::Variable::parseFromStream(std::istream& in)
        {
            std::vector<Variable> result;

            while (in.good()) {
                try {
                    Variable v;
                    in >> v;

                    result.push_back(v);
                } catch (std::runtime_error& e) {
                }
            }

            return result;
        }

        size_t Variable::putSize(char *buffer, const size_t& size) const
        {
            if (size > MAX_BYTE_SIZE) {
                uint32_t s = convertToBigEndian(dynamic_cast<uint32_t>(size));
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

        Message::Message(uint16_t id, unsigned char type)
        {
            this->header.requestId = id;
            this->header.type = type;
            this->header.version = FCGI_VERSION_1;
            this->header.reserved = 0;
            this->header.contentLength = 0;
            this->header.paddingLength = 0;
        };

        Message::~Message()
        {};

        /**
         * Calculate the padding length
         */
        uint16_t Message::calculatePaddingLength()
        {
            uint16_t mod = this->header.contentLength % 8;
            this->header.paddingLength = (mod == 0)? 0 : 8 - mod;

            return this->header.paddingLength;
        }

        bool Message::empty() const
        {
            return (this->getContentSize() == 0);
        }

        const char* Message::raw() const
        {
            return NULL;
        }

        const Header& Message::getHeader() const
        {
            return this->header;
        }

        size_t Message::getPaddingSize() const
        {
            return this->header.paddingLength;
        }

        size_t Message::getSize() const
        {
            return sizeof(this->header) + this->getContentSize() + this->getPaddingSize();
        };

        /**
         * Short accessor for header.contentLength
         */
        size_t Message::getContentSize() const
        {
            return this->header.contentLength;
        }



        /////////////////////////////////////////////////////
        //
        // Generic Message impl
        //

        GenericMessage::GenericMessage(uint16_t id, unsigned char type, const char* data, size_t size) : Message(id, type),
                data(data),
                size(size)
        {
            if (size > MAX_INT16_SIZE) {
                throw IOException("Message data size too large");
            }

            this->header.contentLength = size;
            this->calculatePaddingLength();
        }

        GenericMessage::~GenericMessage()
        {
        }

        const char* GenericMessage::getData() const
        {
            return this->data;
        }

        EndRequestMessage::EndRequestMessage(uint16_t id, uint32_t status, unsigned char fcgiStatus) : Message(id, FCGI_END_REQUEST)
        {
            this->header.contentLength = sizeof(this->record.body);
            this->record.header = this->header;
            this->record.body.appStatus = status;
            this->record.body.protocolStatus = fcgiStatus;
            this->_raw = new char[sizeof(this->record)];
        }

        EndRequestMessage::~EndRequestMessage()
        {
            delete [] this->_raw;
        }

        const char* EndRequestMessage::raw() const
        {
            EndRequestRecord* r = (EndRequestRecord*)this->_raw;
            *r = this->record;
            Client::prepareOutRecordSegment(*r);

            return this->_raw;
        }

        const char* EndRequestMessage::getData() const
        {
            return (char*)this->record.body;
        }
    }
}
