#include <cassert>
#include <iostream>
#include <atomic>
#include <ultrahft/core/timer_wheel.hpp>

using namespace ultrahft::core;

int main() {
    TimerWheel<> wheel;
    
    std::cout << "Testing TimerWheel...\n";
    
    // Test 1: Basic timer scheduling
    std::atomic<int> callback_count{0};
    auto cb = [&callback_count]() { ++callback_count; };
    
    wheel.schedule(5'000'000, cb);  // 5ms
    wheel.schedule(10'000'000, cb); // 10ms
    
    assert(wheel.pending_count() == 2);
    std::cout << "✓ Test 1: Timers scheduled\n";
    
    // Test 2: Ticking and expiration
    std::size_t expired = wheel.tick(6'000'000);  // Tick past 5ms
    assert(callback_count == 1);
    assert(expired == 1);
    assert(wheel.pending_count() == 1);
    std::cout << "✓ Test 2: Timer expired\n";
    
    // Test 3: Multiple timers at same time
    callback_count = 0;
    wheel.clear();
    
    wheel.schedule(5'000'000, cb);
    wheel.schedule(5'000'000, cb);
    wheel.schedule(5'000'000, cb);
    
    assert(wheel.pending_count() == 3);
    wheel.tick(6'000'000);  // Tick past all timers
    assert(callback_count == 3);
    assert(wheel.pending_count() == 0);
    std::cout << "✓ Test 3: Multiple timers at same time\n";
    
    // Test 4: Cancellation
    callback_count = 0;
    wheel.clear();
    
    auto id1 = wheel.schedule(5'000'000, cb);
    auto id2 = wheel.schedule(5'000'000, cb);
    auto id3 = wheel.schedule(5'000'000, cb);
    (void)id1;
    (void)id3;
    
    assert(wheel.pending_count() == 3);
    assert(wheel.cancel(id2));
    assert(wheel.pending_count() == 2);
    
    wheel.tick(6'000'000);
    assert(callback_count == 2);
    std::cout << "✓ Test 4: Timer cancellation\n";
    
    // Test 5: Absolute time scheduling
    callback_count = 0;
    wheel.clear();
    
    wheel.schedule_at(100'000'000, cb);  // Expire at 100ms
    wheel.schedule_at(50'000'000, cb);   // Expire at 50ms
    
    wheel.tick(60'000'000);  // Tick to 60ms
    assert(callback_count == 1);  // Only the 50ms timer expired
    assert(wheel.pending_count() == 1);
    
    wheel.tick(110'000'000);  // Tick to 110ms
    assert(callback_count == 2);  // Now the 100ms timer expired
    assert(wheel.pending_count() == 0);
    std::cout << "✓ Test 5: Absolute time scheduling\n";
    
    // Test 6: Large number of timers
    callback_count = 0;
    wheel.clear();
    
    const int NUM_TIMERS = 1000;
    for (int i = 0; i < NUM_TIMERS; ++i) {
        wheel.schedule(5'000'000 + (i % 10) * 1'000'000, cb);
    }
    
    assert(wheel.pending_count() == NUM_TIMERS);
    wheel.tick(20'000'000);  // Tick past all timers
    assert(callback_count == NUM_TIMERS);
    assert(wheel.pending_count() == 0);
    std::cout << "✓ Test 6: Large number of timers\n";
    
    // Test 7: Sequential timer advancement
    callback_count = 0;
    wheel.clear();
    
    for (int i = 0; i < 5; ++i) {
        wheel.schedule((i + 1) * 1'000'000, cb);
    }
    
    for (int i = 0; i < 5; ++i) {
        wheel.tick((i + 1) * 1'000'000 + 100'000);
        assert(callback_count == i + 1);
    }
    std::cout << "✓ Test 7: Sequential timer advancement\n";
    
    std::cout << "\nAll TimerWheel tests passed! ✓\n";
    return 0;
}
