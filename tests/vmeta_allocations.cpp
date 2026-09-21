#include "vmeta.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <new>

static std::atomic<std::size_t> allocations = 0;
void* operator new(std::size_t size)
{
    if (void* memory = std::malloc(size ? size : 1))
    {
        ++allocations;
        return memory;
    }
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
struct AllocationSubject
{
    V_META(AllocationSubject, vstd::meta::empty, V_METHOD(AllocationSubject, add, int, int))
  public:
    int add(int value) { return value + 1; }
};
int main()
{
    auto object = std::make_shared<AllocationSubject>();
    auto method = object->meta()->find_method<AllocationSubject, int>("add", object);
    std::vector<std::any> arguments{object, 41};
    method->invoke(arguments);
    const auto before = allocations.load();
    for (int i = 0; i < 1000; ++i)
    {
        if (std::any_cast<int>(method->invoke(arguments)) != 42) { return 1; }
    }
    const auto total = allocations.load() - before;
    std::cout << "cached integer invocations: " << total << " allocations / 1000 calls\n";
    return total == 0 ? 0 : 1;
}
