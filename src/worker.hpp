#pragma once

#include <list>
#include <thread>
#include <mutex>
#include <sstream>

#include "requesthandler.hpp"
#include "dealloc_guard.hpp"

namespace gfsfcgi
{
	class Worker
	{
		public:
			class NullPointerException : public std::exception
			{
				private:
					const char* name;

				public:
					inline NullPointerException(const char* name) : name(name) {};
					inline ~NullPointerException() throw() {};

					inline const char* what() const throw() {
						std::ostringstream o;
						o << "NullPointerException: " << this->name << " is NULL";
						return o.str().c_str();
					}
			};

		protected:
			struct RequestList {
				RequestHandler* handler = NULL;
				RequestList* previous = NULL;
				RequestList* next = NULL;
			};

			std::thread* thread;
			std::mutex mutex;
			std::size_t size;

			RequestList* first;
			RequestList* last;
			DeallocatorGuard allocatorGuard;

			void freeList();
			void doRun();

		private:
			void remove(RequestList* item);
			RequestList* next(RequestList* item);


		public:
			Worker();
			virtual inline ~Worker()
			{
				this->freeList();
			};

			inline std::size_t getRequestCount() const {
				std::lock_guard lock(this->mutex);
				return this->size;
			}

			void attachHandler(RequestHandler* handler);

			inline void f(RequestHandler* handler) {
			}

			void run();
			void exit();
	};

	class WorkerPool
	{
		protected:
			std::list<Worker> items;

		public:
			WorkerPool(std::size_t size);
			~WorkerPool();

			void run();
			void exit();
	};

}
