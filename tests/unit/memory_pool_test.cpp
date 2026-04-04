#include <cassert>
#include <cstddef>

#include <ultrahft/core/memory_pool.hpp>

namespace {

struct TestOrder {
    static inline int live_objects = 0;

    int id;
    int qty;

    TestOrder(int id_, int qty_) : id(id_), qty(qty_) { ++live_objects; }
    ~TestOrder() { --live_objects; }
};

}  // namespace

int main() {
    using ultrahft::core::MemoryPool;

    {
        MemoryPool<TestOrder, 2> pool;

        TestOrder* a = pool.allocate(101, 5);
        assert(a != nullptr);
        assert(a->id == 101);
        assert(a->qty == 5);
        assert(pool.size() == 1);
        assert(pool.available() == 1);

        TestOrder* b = pool.allocate(202, 7);
        assert(b != nullptr);
        assert(pool.size() == 2);
        assert(pool.available() == 0);

        TestOrder* c = pool.allocate(303, 9);
        assert(c == nullptr);

        assert(pool.deallocate(a));
        assert(pool.size() == 1);
        assert(pool.available() == 1);

        TestOrder* d = pool.allocate(404, 11);
        assert(d != nullptr);
        assert(d->id == 404);
        assert(pool.size() == 2);

        int external = 0;
        assert(!pool.deallocate(reinterpret_cast<TestOrder*>(&external)));
    }

    assert(TestOrder::live_objects == 0);

    return 0;
}
