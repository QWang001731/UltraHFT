#include <ultrahft/core/ring_buffer.hpp>
#include<cstddef>
#include<iostream>
using namespace std;
struct T
{
    T(int _x, int _y):x(_x),y(_y){}
    int x;
    int y;
};


template <typename ...Args>
int* allocate(Args&& ...args, int *p)
{
    std::construct_at(p, std::forward<Args>(args)...);
    return p;
}



int main() 
{
    int a = 19;

    const int * p = &a;

    a= 20;
    return 0; 
}    