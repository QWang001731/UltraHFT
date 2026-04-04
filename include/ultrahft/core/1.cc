#include <cassert>
#include<bitset>
#include<cstddef>
#include<memory>
#include<iostream>
#include<memory_pool_1.hpp>
#include<thread>
using namespace std;

struct S1
{
    S1(size_t id,double d, char c):id_(id),d_(d),c_(c){}
    size_t id_;
    double d_;
    char c_;
};


int main()
{    

    ultrahft::core::MemoryPool1<S1,1024> mp1;
    S1*ptr=mp1.allocate(100,10.1,'c');

    mp1.deallocate(ptr);
    std::this_thread::sleep_for(std::chrono::seconds(100));
    cout<<ptr->d_;
}