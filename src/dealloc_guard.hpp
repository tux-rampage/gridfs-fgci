#pragma once

#include <cstdlib>
#include <list>

namespace gfsfcgi
{
	/**
	 * Deallocator interface
	 */
	class DeallocatorInterface
	{
		public:
			virtual inline ~DeallocatorInterface() {};
			virtual void free() = 0;
	};

	/**
	 * Deallocator guard
	 */
	class DeallocatorGuard
	{
		private:
			std::list<DeallocatorInterface> items;

		public:
			inline DeallocatorGuard() : items() {};
			inline ~DeallocatorGuard()
			{
				this->clear();
			};

			inline void add(DeallocatorInterface& dealloc)
			{
				this->items.push_back(dealloc);
			}

			/**
			 * Clear and free all pointers
			 */
			inline void clear()
			{
				for (auto item : this->items) {
					item.free();
				}

				this->items.clear();
			};
	};

	/**
	 * concrete c++ deallocator (templated)
	 */
	template <class T>
	class Deallocator : public DeallocatorInterface
	{
		protected:
			T* ptr;

		public:
			inline Deallocator(T* ptr) : ptr(ptr) {};
			inline ~Deallocator()
			{
				this->free();
			};

			inline void free()
			{
				if (this->ptr == NULL) {
					return;
				}

				delete this->ptr;
				this->ptr = NULL;
			}
	};
}
