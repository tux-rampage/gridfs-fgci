#pragma once

#include <list>
#include <thread>
#include <mutex>
#include <sstream>

#include "exceptions.hpp"
#include "requesthandler.hpp"
#include "dealloc_guard.hpp"

namespace gfsfcgi
{
	class Worker
	{
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

			void doRun();

		private:
			void inline assertSameThreadContext() throw (ThreadContextViolatedException);

			void freeList();
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
