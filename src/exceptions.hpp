#pragma once

#include <exception>
#include <sstream>

#define GFSFCGI_EXCEPTION_CLASSNAME(clsname) protected: virtual inline const char* classname() const throw() { return #clsname;  };

namespace gfsfcgi
{
    /**
     * Abstract exception class
     */
    class AbstractException : public std::exception
    {
        protected:
            const char* reason;

            virtual const char* classname() const throw() = 0;

        public:
            inline AbstractException(const char *reason) : reason(reason) {};
            virtual inline ~AbstractException() throw() {};

            /**
             * C-Stringify
             */
            inline const char* what() const throw()
            {
                return this->toString().c_str();
            }

            /**
             * Stringify
             */
            virtual inline std::string toString() const throw()
            {
                std::ostringstream o;
                o << this->classname() << ": " << this->reason;
                return o.str();
            }
    };


    class NullPointerException : public AbstractException
    {
        GFSFCGI_EXCEPTION_CLASSNAME(NullPointerException);

        public:
            inline NullPointerException(const char* name) : AbstractException(name) {};

            /**
             * Custom stringification
             */
            inline std::string toString() const throw()
            {
                std::ostringstream o;
                o << "NullPointerException: " << this->reason << " is NULL";
                return o.str().c_str();
            }
    };

    /**
     * Exception class thrown when a thread context is violated
     */
    class ThreadContextViolatedException : public AbstractException
    {
        GFSFCGI_EXCEPTION_CLASSNAME(NullPointerException);
    };

    class RuntimeException : public AbstractException
    {
        GFSFCGI_EXCEPTION_CLASSNAME(RuntimeException);
    };

    class IOException : public AbstractException
    {
        GFSFCGI_EXCEPTION_CLASSNAME(IOException);
    };
};
