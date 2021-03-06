
#include <functional>
#include <regex>
#include <sstream>
#include <signal.h>
#include <unistd.h>

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

    void Client::eventReadCallback(bufferevent* event, void* arg)
    {
        Client* client = (Client*)arg;
        client->onRead(event);
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
            isValid(true),
            keepConnection(true)
    {
        if (!IOHandler::setNonBlocking(socket)) {
            throw IOException("Failed to make socket fd non-blocking.");
        }

        this->event = bufferevent_socket_new(io.eventBase, socket, BEV_OPT_DEFER_CALLBACKS | BEV_OPT_CLOSE_ON_FREE | BEV_OPT_THREADSAFE);

        if (this->event == NULL) {
            throw IOException("Failed to allocate event buffer for client");
        }

        bufferevent_setcb(this->event, Client::eventReadCallback, NULL, NULL, this);
        bufferevent_enable(this->event, EV_READ|EV_WRITE);
    }

    Client::~Client()
    {
        this->destroy();
    }

    /////////////////////////////////////////////////////////////////
    // Incomming record preparation (possible big endian conversion)
    //

    //! Specialization for header segment
    template<> void Client::prepareInRecordSegment<protocol::Header>(protocol::Header& segment) {
        segment.contentLength = convertFromBigEndian(segment.contentLength);
        segment.requestId = convertFromBigEndian(segment.contentLength);
    };

    //! Specialization for BeginRequestBody
    template<> void Client::prepareInRecordSegment<protocol::BeginRequestBody>(protocol::BeginRequestBody& body)
    {
        body.role = convertFromBigEndian(body.role);
    };

    //! Specialization for EndRequestBody
    template<> void Client::prepareInRecordSegment<protocol::EndRequestBody>(protocol::EndRequestBody& segment)
    {
        segment.appStatus = convertFromBigEndian(segment.appStatus);
    };

    //! Default implementation
    template<class T> void Client::prepareInRecordSegment(T& segment)
    {
        // NOOP
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
        prepareOutRecordSegment(segment.header);
        prepareOutRecordSegment(segment.body);
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
            streams::OutStream s(ClientPtr(this), FCGI_NULL_REQUEST_ID, streams::OutStreamBuffer::role_t::VALUES_RESULT);

            protocol::Variable _var(FCGI_MPXS_CONNS, "1");
            s << _var;
            s.close();

            return;
        }

        uint16_t id = this->currentRecord.header.requestId;
        protocol::Header& header(this->currentRecord.header);

        if (id == FCGI_NULL_REQUEST_ID) {
            //this->write()
            // TODO: Unknown record
            return;
        }

        // FIXME: Race contition

        if (this->currentRecord.header.type == FCGI_BEGIN_REQUEST) {
            if (this->hasRequest(id)) {
                std::ostringstream msg;
                msg << "The request " << id << " was already started!";
                throw IOSegmentViolationException(msg.str());
            }

            if (header.contentLength != sizeof(protocol::BeginRequestBody)) {
                throw IOSegmentViolationException("Bad content length for begin request record!");
            }

            protocol::BeginRequestBody* body = (protocol::BeginRequestBody*)this->currentRecord.content;
            prepareInRecordSegment(*body);

            if (!this->io.isRoleAccepted(body->role)) {
                // TODO: unaccepted role
                return;
            }

            if ((body->flags & FCGI_KEEP_CONN) == 0) {
                this->keepConnection = false;
            }

            this->requests[id] = std::make_shared<Request>(id, body->role, *this);
            this->requests[id]->setHandler(this->io.getHandlerFactory(body->role)->factory(*this->requests[id]));

            return;
        }

        if (!this->hasRequest(id)) {
            std::ostringstream msg;
            msg << "Request (" << id << ") was not started with FCGI_BEGIN_REQUEST";
            throw IOSegmentViolationException(msg.str());
        }

        this->requests[id]->processIncommingRecord(this->currentRecord);
    };

    /**
     * reset the record state
     */
    void Client::resetRecordState()
    {
        if (this->currentRecord.content != NULL) {
            delete[] this->currentRecord.content;
        }

        memset(&this->currentRecord, 0, sizeof(this->currentRecord));

        this->currentRecord.content = NULL;
        this->headerReady = false;
        this->contentReady = false;
        this->paddingReady = false;
        this->headerBytesRead = 0;
        this->contentBytesRead = 0;
        this->paddingBytesRead = 0;
    }

    /**
     * Read data from socket
     */
    void Client::onRead(bufferevent* event)
    {
        if (!this->isValid) {
            return;
        }

        char buffer[1024];
        size_t size = 0;

        while ((0 < (size = (size_t)bufferevent_read(event, (void*)&buffer, 1024))) && this->valid()) {
            char *pFrom = (char*)&buffer;

            while (size && this->valid()) {
                pFrom = this->extractHeader(pFrom, size);
                pFrom = this->extractContent(pFrom, size);
                pFrom = this->extractPadding(pFrom, size);

                if (this->paddingReady) {
                    try {
                        this->dispatch();
                    } catch (IOException& e) {
                        std::cerr << "Client (" << this->socket << "): IOException: " << e.what() << std::endl;
                        this->destroy();
                        return;
                    }

                    this->resetRecordState();
                }
            }
        }
    };

    bool Client::hasRequest(uint16_t id)
    {
        auto result = this->requests.find(id);
        return ((result != this->requests.end()) && (result->second != NULL) && result->second->isValid());
    }

    /**
     * Destroy the client instance
     */
    void Client::destroy()
    {
        std::lock_guard<std::mutex> guard(this->socketMutex);

        this->isValid = false;
        this->resetRecordState();

        if (this->event != NULL) {
            bufferevent_disable(this->event, EV_READ | EV_WRITE);
            bufferevent_free(this->event);
            this->event = NULL;
        }
    }

    void Client::eventGcCallback(int fd, short eventType, void* ptr)
    {
        ((Client*)ptr)->gc();
    }

    // Perform garbage collection
    void Client::gc()
    {
        // FIXME: Race condition

        auto iterator = this->requests.begin();

        while (iterator != this->requests.end()) {
            if ((iterator->second == NULL) || !iterator->second->isValid()) {
                this->requests.erase(iterator++);
            } else {
                iterator++;
            }
        }
    }

    /**
     * Write chunk implementation
     */
    void Client::write(protocol::Message &message)
    {
        std::unique_lock<std::mutex> guard(this->socketMutex);
        guard.lock();

        if (!this->valid()) {
            return;
        }

        const char *raw = message.raw();
        if (raw != NULL) {
            bufferevent_write(this->event, raw, message.getSize());
            return;
        }

        protocol::Header header = message.getHeader();
        header.version = FCGI_VERSION_1;
        prepareOutRecordSegment(header);

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

        guard.unlock();

        if (message.getHeader().type == FCGI_END_REQUEST) {
            this->destroy();
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

        streams::InStreamBuffer* buf = NULL;

        switch (record.header.type) {
            case FCGI_ABORT_REQUEST:
                this->valid = true;
                break;

            case FCGI_PARAMS:
                buf = dynamic_cast<streams::InStreamBuffer*>(this->paramStream.rdbuf());
                if (buf == NULL) {
                    break;
                }

                buf->addChunk(record);

                if (this->paramStream.isReady()) {
                    for (auto p : protocol::Variable::parseFromStream(this->paramStream)) {
                        this->params[p.name()] = p.value();
                    }

                    this->ready = true;
                }

                break;

            case FCGI_STDIN: // break intentionally omitted
            case FCGI_DATA:
                if (!this->ready) {
                    std::ostringstream msg;
                    msg << "Invalid stream record order. FastCGI PARAMS is not complete, yet - thus the request is not ready.";
                    throw IOSegmentViolationException(msg.str());
                }

                buf = (record.header.type == FCGI_STDIN)?
                    dynamic_cast<streams::InStreamBuffer*>(this->_stdin.rdbuf()) :
                    dynamic_cast<streams::InStreamBuffer*>(this->_datain.rdbuf());

                if (buf == NULL) {
                    break;
                }

                buf->addChunk(record);

                if (this->handler != NULL) {
                    this->handler->onReceiveData(record);
                }

                break;

            case FCGI_ABORT_REQUEST:
                if (this->handler) {
                    this->handler->onAbort();
                } else {
                    this->finish(1);
                }

                break;

            default:
                // Ignore unknown records. Is this a good idea?
                break;
        }
    }

    Request::Request(const uint16_t& id, Role role, ClientPtr client) :
            client(client),
            id(id),
            role(role),
            valid(false),
            ready(false),
            handler(NULL),
            paramStream(*this),
            _stdin(*this),
            _datain(*this),
            _stdout(*this, streams::OutStreamBuffer::role_t::STDOUT),
            _stderr(*this, streams::OutStreamBuffer::role_t::STDERR)
    {
    }

    Request::~Request()
    {
    }


    void Request::setHandler(RequestHandlerPtr handler)
    {
        this->handler = handler;
    }

    void Request::send(protocol::Message& msg)
    {
        this->client->write(msg);
    }

    // Finish the request
    void Request::finish(uint32_t status)
    {
        protocol::EndRequestMessage end(this->getId(), status, 0);
        this->client->write(end);

        this->_datain.close();
        this->_stdin.close();
        this->_stderr.close();
        this->_stdout.close();

        this->valid = false;
    }

    bool Request::isValid()
    {
        return this->valid;
    }

    /////////////////////////////////////////////////////////////////////
    //
    // I/O Handler
    //

    IOHandler::IOHandler(const std::string& bind) : bind(bind), IOHandler(0)
    {
    }

    IOHandler::IOHandler(int socket) :
            fd(socket),
            gcInterval({ 10, 0 }) // default gc every 10 seconds
    {
        this->eventBase = event_base_new();
    }

    IOHandler::~IOHandler()
    {
        this->clearListeners();
        event_base_free(this->eventBase);
        close(this->fd);
    }

    //! accept callback helper
    void IOHandler::eventAcceptCallback(evconnlistener* event, int fd, sockaddr* clientAddress, int len, void* ptr)
    {
        ((IOHandler*)ptr)->accept(fd, clientAddress, len);
    }

    //! Error callback
    void IOHandler::eventErrorCallback(evconnlistener* listener, void* ptr)
    {
        ((IOHandler*)ptr)->onError(listener);
    }

    //! GC trigger
    void IOHandler::eventGcCallback(int fd, short type, void *ptr)
    {
        ((IOHandler*)ptr)->gc();
    }

    void IOHandler::eventSignalCallback(int signal, short type, void* ptr)
    {
        // TODO: Exit the I/O Handler
    }

    void IOHandler::addHandlerFactory(HandlerFactoryPtr handler)
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

    HandlerFactoryPtr IOHandler::getHandlerFactory(uint16_t role)
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

    ///! Create the socket connection
    void IOHandler::createListenerSocket()
    {
        if (this->fd != 0) {
            return;
        }

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

            std::string addr = m[1].str();

            if (addr == "0.0.0.0") {
                bindIPv4.sin_addr.s_addr = INADDR_ANY;
            } else {
                // TODO: Bind specific address
                // gethostbyaddr(addr.c_str(), addr.size());
                bindIPv4.sin_addr.s_addr = INADDR_ANY;
            }

            this->fd = socket(AF_INET, SOCK_STREAM, 0);
        // } else if () { // TODO: IPv6
        } else {
            std::ostringstream oss;
            oss << "Invalid bind expression: \"" << bind << "\"";
            throw IOException(oss.str());
        }

        if (::bind(this->fd, bindAddr, bindAddrLen) < 0) {
            close(this->fd);
            std::ostringstream oss;
            oss << "Failed to bind socket to \"" << bind << "\"";
            throw IOException(oss.str());
        }

        if (!setNonBlocking(this->fd)) {
            throw IOException("Failed to make listener socket non blocking");
        }
    }

    //! Run the IO handler thread
    void IOHandler::run(unsigned int workerCount)
    {
        this->createListenerSocket();

        auto gc = event_new(this->eventBase, -1, EV_PERSIST, IOHandler::eventGcCallback, this);
        auto sig = evsignal_new(this->eventBase, SIGTERM, IOHandler::eventSignalCallback, this);
        auto listener = evconnlistener_new(this->eventBase, IOHandler::eventAcceptCallback, this, LEV_OPT_REUSEABLE, -1, this->fd);

        this->eventListeners.push_back(gc);
        this->eventListeners.push_back(sig);

        if (!listener) {
            throw IOException("Could not initialize event listeners");
        }

        evtimer_add(gc, &this->gcInterval);
        evsignal_add(sig, NULL);

        evconnlistener_set_error_cb(listener, IOHandler::eventErrorCallback);
        evconnlistener_enable(listener);

        // Start the worker queue
        this->workerQueue.run(workerCount);

        // Dispatch the event loop
        event_base_dispatch(this->eventBase);

        this->clearListeners();
        evconnlistener_disable(listener);
        evconnlistener_free(listener);
    }

    //! Accept a client connection
    void IOHandler::accept(int fd, sockaddr* address, int socketlen)
    {
        ClientPtr client(new Client(*this, fd));

        std::lock_guard<std::mutex> guard(this->clientListMutex);
        this->clients.push_back(client);
    }

    //! Garbage collect
    void IOHandler::gc()
    {
        std::lock_guard<std::mutex> guard(this->clientListMutex);

        for (auto it = this->clients.begin(); it != this->clients.end(); it++) {
            if (!(*it)->valid()) {
                this->clients.erase(it);
            }
        }
    }

    //! Handle errors
    void IOHandler::onError(evconnlistener* error)
    {
        int err = EVUTIL_SOCKET_ERROR();
        std::cerr << "Socket listener error " << err << ": " << evutil_socket_error_to_string(err) << std::endl;

        event_base_loopexit(this->eventBase, NULL);
    }

    /**
     * Clear all event listeners
     */
    void IOHandler::clearListeners()
    {
        for (auto event : this->eventListeners) {
            if (event == NULL) {
                continue;
            }

            event_del(event);
            event_free(event);
        }

        this->eventListeners.clear();
    }

    //! Make filedescriptor non-blocking
    bool IOHandler::setNonBlocking(int fd)
    {
        return (evutil_make_socket_nonblocking(fd) == 0);
    }


    // Responder is the only supported role by default
    bool HandlerFactory::acceptRole(uint16_t role)
    {
        return (role == FCGI_RESPONDER);
    }


    ///////////////////////////////////////////////////////////////
    //
    // Request handler
    //

    RequestHandler::RequestHandler(Request& request)
    {
        this->request = &request;
    }

    RequestHandler::~RequestHandler()
    {
    }

    Request& RequestHandler::getRequest() throw (std::runtime_error)
    {
        if (this->request == NULL) {
            throw std::runtime_error("Request is NULL for the current handler instance");
        }

        return *this->request;
    }

    void RequestHandler::finish(uint16_t status)
    {
        this->getRequest().finish(status);
        this->request = NULL;
    }

    void RequestHandler::onReceiveData(const protocol::Record& record)
    {
        // NOOP
    }

    void RequestHandler::onAbort()
    {
        this->finish(1);
    }


    ///////////////////////////////////////////////////////////////
    //
    // Worker queue
    //

    WorkerQueue::WorkerQueue() : terminated(false)
    {
    }

    WorkerQueue::~WorkerQueue()
    {
        this->terminate();

        for (auto thread : this->threadPool) {
            if (thread->joinable()) {
                thread->join();
            }

            delete thread;
        }

        this->threadPool.clear();
    }

    void WorkerQueue::push(WorkerCallbackPtr& ptr)
    {
        std::unique_lock<std::mutex> lock(this->protector);

        lock.lock();
        parent::push(ptr);
        lock.unlock();

        this->readyCondition.notify_one();
    }

    WorkerCallbackPtr WorkerQueue::pop()
    {
        std::unique_lock<std::mutex> lock(this->protector);
        lock.lock();

        if (this->empty()) {
            lock.unlock();
            this->readyCondition.wait(lock);
        }

        // Still empty or terminated?
        if (this->empty() || this->terminated) {
            return WorkerCallbackPtr(NULL);
        }

        WorkerCallbackPtr ptr = this->front();

        parent::pop();
        lock.unlock();

        return ptr;
    }

    /**
     * Request worker termination
     */
    void WorkerQueue::terminate()
    {
        this->terminated = true;
        this->readyCondition.notify_all();
    }

    /**
     * Check if queue is terminated
     */
    bool WorkerQueue::isTerminated() const
    {
        return this->terminated;
    }

    /**
     * Run the worker queue
     */
    void WorkerQueue::run(unsigned int threadCount)
    {
        this->terminated = false;

        if (threadCount < 1) {
            threadCount = std::thread::hardware_concurrency();

            if (threadCount < 1) {
                threadCount = 1;
            }
        }

        for (auto i = 0; i < threadCount; i++) {
            auto thread = new std::thread(Worker(*this));
            this->threadPool.push_back(thread);
        }
    }

    /**
     * Create a new worker
     */
    Worker::Worker(WorkerQueue& queue) : queue(queue)
    {
    }

    Worker::~Worker()
    {
    }

    void Worker::operator ()()
    {
        while(!this->queue.isTerminated()) {
            auto handler = this->queue.pop();

            // Null pointer
            if (handler == NULL) {
                continue;
            }

            if (!(*handler)()) {
                this->queue.push(handler);
            }
        }
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
                uint32_t s = convertToBigEndian((uint32_t)size);
                memcpy(buffer, &s, sizeof(int32_t));

                return sizeof(uint32_t);
            }

            unsigned char v = (unsigned char)size;
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
            return (const char*)&this->record.body;
        }
    }
}
