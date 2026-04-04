#include <cstddef>
#include <memory>
#include <bitset>
#include <optional>
#include <cstdint>
namespace ultrahft::core
{
    template <class T, size_t Capacity>
    class MemoryPool1
    {
        private:
        std::unique_ptr<std::size_t[]> free_list_;
        std::unique_ptr<std::byte[]> storage;
        std::bitset<Capacity> occupied;
        size_t free_top_;
        size_t live_count_;
        std::optional<size_t> index_of(T*p) const noexcept
        {
            if(p == nullptr)
            {
                return std::nullopt;
            }

            const auto addr = reinterpret_cast<std::uintptr_t>(p);
            const auto base = reinterpret_cast<std::uintptr_t>(storage.get());
            const auto end = reinterpret_cast<std::uintptr_t>(storage.get() + sizeof(T) * Capacity);
            if(addr < base || addr >= end )
            {
                return std::nullopt;
            }
            
            const auto offset = addr - base;
            const auto stride = sizeof(T);

            if(offset % stride != 0)
            {
                return std::nullopt;
            }
            

            size_t slot = static_cast<size_t> (offset / stride);

            if(slot >= Capacity)
            {
                return std::nullopt;
            }

            return slot;
        }

        T* ptr_at(size_t slot) const noexcept
        {
            return std::launder(reinterpret_cast<T*>(storage.get() + slot * sizeof(T)));
        }


        public:
            MemoryPool1():
            free_top_(Capacity),
            storage(new std::byte[sizeof(T) * Capacity]),
            free_list_(new size_t[Capacity]),
            occupied(Capacity),
            live_count_(0)
            {
                for(size_t i=0;i < Capacity;i++)
                {
                    free_list_[i] = Capacity - i - 1;
                    occupied.reset(i);
                }
            }

            ~MemoryPool1()
            {
                for(int i = 0;i < Capacity; i++)
                {
                    if(occupied.test(i))
                    {   
                        std::destroy_at(ptr_at(i));
                        occupied.reset(i);
                    }
                }

            }

            size_t available() const noexcept
            {
                return free_top_;
            }

            size_t capacity() const noexcept
            {
                return Capacity;
            }
            size_t live_count() const noexcept
            {
                return live_count_;
            }
            template<typename ...Args>
            [[nodiscard]] T* allocate(Args&& ...args) 
            {
                if(free_top_ == 0)
                {
                    return nullptr;
                }

                size_t slot = free_list_[free_top_ - 1];
                T* ptr = ptr_at(slot);
                std::construct_at(ptr, std::forward<Args>(args)...);
                occupied.set(slot);
                live_count_++;
                free_top_--;
                return ptr;
            }            


            bool deallocate(T* ptr) 
            {
                std::optional<size_t> idx = index_of(ptr);
                if(!idx.has_value())
                {
                    return false;
                }
                size_t slot = *idx;
                if(!occupied.test(slot))
                {
                    return false;
                }

                free_list_[free_top_++] = slot;
                live_count_--;
                std::destroy_at(ptr);
                occupied.reset(slot);
                return true;
            }
        };
}