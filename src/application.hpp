#pragma once
#include <map>
#include <string>
#include <mongo/client/dbclientinterface.h>

#include "requesthandler.hpp"

namespace gfsfcgi
{
    /**
     * Options definition
     */
    class Options : public std::map<std::string, std::string>
    {
        public:
            virtual inline ~Options() {};
    };

    class RequestHandlerFactoryInterface
    {
        public:
            virtual inline ~RequestHandlerFactoryInterface() {};
            virtual RequestHandler* createRequestHandler() const = 0;
    };

    /**
     * Application entry
     */
    class Application
    {
        protected:
            Options options;

        public:
            Application(Options options);
            virtual ~Application();

            virtual int run();
    };

    /**
     * Options parser
     */
    class ConfigOptions : public Options
    {
        public:
            ConfigOptions(int argc, char** argv);
            virtual inline ~ConfigOptions() {};
    };

    /**
     * Dependency factory class
     */
    class Factory : public RequestHandlerFactoryInterface
    {
        protected:
            Options options;
            mongo::DBClientConnection* connection;
            Application* app;

            virtual void createConnection();

        public:
            Factory(int argc, char** argv);
            virtual ~Factory();

            Application& getApplication();
            inline mongo::DBClientConnection& getMongoConnection();

            RequestHandler* createRequestHandler() const;
    };
};
