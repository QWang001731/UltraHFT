/*
    SPSC queue
*/

#pragma once
#include<memory>
#include<atomic>
namespace ultrahft::core 
{

template<typename T, size_t N>
class SpscQueue
{
    public:
        SpscQueue()
        {
            static_assert(((N - 1) & N) == 0u && N != 0);
            ptr = new T[N];
            head_.store(0);
            tail_.store(0);
        }

        ~SpscQueue()
        {
            delete[] ptr;
        }

        bool push(const T& ele)
        {
            size_t t = tail_.load(std::memory_order_relaxed);
            size_t h = head_.load(std::memory_order_acquire);
            if( (t + 1) % N == h )
            {
                return false;
            }

            ptr[t] = ele;
            tail_.store((t + 1) % N, std::memory_order_release);
            return true;
        }

        bool pop(T& ele)
        {
            size_t t = tail_.load(std::memory_order_acquire);
            size_t h =head_.load(std::memory_order_relaxed);
            if(t == h)
            {
                return false;
            }

            ele = ptr[h];
            head_.store((h + 1) % N,std::memory_order_release);
            return true;
        }
        
    private:
        T* ptr;
        std::atomic<size_t> head_, tail_;
    
    };
}
