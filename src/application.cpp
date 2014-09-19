/*
 * application.cpp
 *
 *  Created on: 16.09.2014
 *      Author: unreality
 */

#include "application.hpp"

using namespace gfsfcgi;

gfsfcgi::Factory::Factory(int argc, char** argv) : app(NULL),
        connection(true)
{
    this->options = ConfigOptions(argc, argv);
}

gfsfcgi::Factory::~Factory()
{
    if (this->app != NULL) {
        delete this->app;
    }
}

Application& gfsfcgi::Factory::getApplication()
{
    if (this->app == NULL) {
        this->app = new Application(this->options);
    }

    return *(this->app);
}

void gfsfcgi::Factory::createConnection()
{
    if (this->connection != NULL) {
        return;
    }

    this->connection = new mongo::DBClientConnection(true);
    this->connection->connect(this->options[""]);
}

mongo::DBClientConnection& gfsfcgi::Factory::getMongoConnection()
{
    this->createConnection();
    return *(this->connection);
}

gfsfcgi::Application::Application(Options options)
{
}

gfsfcgi::Application::~Application()
{
}

int gfsfcgi::Application::run()
{
}

gfsfcgi::ConfigOptions::ConfigOptions(int argc, char** argv)
{
}

RequestHandler* gfsfcgi::Factory::createRequestHandler() const
{
}
