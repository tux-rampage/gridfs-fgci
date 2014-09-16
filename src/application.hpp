#pragma once
#include <map>
#include <string>
#include <mongo/client/dbclientinterface.h>

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


    class Factory
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
    };
};
