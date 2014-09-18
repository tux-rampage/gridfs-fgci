
#include <functional>
#include "worker.hpp"
#include "dealloc_guard.hpp"

namespace gfsfcgi
{
	template<class T> class Deallocator<std::thread>;

	Worker::Worker() : thread(NULL), first(NULL), last(NULL), size(0)
	{
	}

	void Worker::doRun()
	{
		while (true) {
			auto current = this->first;

			while (current != NULL) {
				if (current->handler->sendData()) {
					current = this->next(current);
					std::this_thread::yield();
					continue;
				}

				auto next = this->next(current);
				this->remove(current);

				current = next;
				std::this_thread::yield();
			}

			std::this_thread::yield();
		}
	}

	void Worker::attachHandler(RequestHandler* handler)
	{
		if (handler == NULL) {
			throw NullPointerException("Request Handler");
		}

		std::lock_guard lock(this->mutex);

		if (this->last == NULL) {
			this->freeList(); // ensure first pointer is released
			this->first = this->last = new RequestList;
			this->last->handler = handler;
		} else {
			auto newItem = new RequestList;
			newItem->handler = handler;
			newItem->previous = this->last;

			this->last->next = newItem;
			this->last = newItem;
		}

		this->size++;
	}

	void Worker::freeList()
	{
		RequestList* current = this->first;

		while (current != NULL) {
			RequestList* next = current->next;
			delete current;
			current = next;
		}

		this->first = NULL;
		this->last = NULL;
		this->size = 0;
	}

	void Worker::remove(RequestList* item)
	{
		std::lock_guard lock(this->mutex);

		// todo: validate first/last pointer
		if ((item->previous == NULL) && (item->next != NULL)) {
			this->first = item->next;
		} else if ((item->next == NULL) && (item->previous != NULL)) {
			this->last = item->previous;
		} else if ((item->next == NULL) && (item->previous == NULL)) {
			this->first = this->last = NULL;
		}

		if (item->previous != NULL) {
			item->previous->next = item->next;
		}

		if (item->next != NULL) {
			item->next->previous = item->previous;
		}

		delete item;
		this->size--;
	}

	Worker::RequestList* Worker::next(RequestList* item)
	{
		std::lock_guard lock(this->mutex);
		return item->next;
	}

	void Worker::run()
	{
		this->thread = new std::thread(std::bind(Worker::doRun(), this));
		this->allocatorGuard.add(dynamic_cast<DeallocatorInterface&>(Deallocator(this->thread)));
	}

	WorkerPool::WorkerPool(std::size_t size) : items(size)
	{
	}

	WorkerPool::~WorkerPool()
	{
	}

	void WorkerPool::run()
	{
		for (auto worker : this->items) {
			worker.run();
		}
	}

	void WorkerPool::exit()
	{
		for (auto worker : this->items) {
			worker.exit();
		}
	}

}
