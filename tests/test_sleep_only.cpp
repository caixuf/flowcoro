#include <flowcoro/core.h>
#include <iostream>

using namespace flowcoro;

Task<void> simple_sleep_test() {
    std::cout << "Before sleep..." << std::endl;
    co_await sleep_for(std::chrono::milliseconds(10));
    std::cout << "After sleep..." << std::endl;
    co_return;
}

int main() {
    std::cout << "Testing sleep_for directly..." << std::endl;
    
    try {
        sync_wait(simple_sleep_test());
        std::cout << "Sleep test passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Sleep test failed: " << e.what() << std::endl;
    }
    
    // 主动关闭线程池，避免析构时的竞态条件
    std::cout << "Shutting down thread pool..." << std::endl;
    GlobalThreadPool::shutdown();
    
    std::cout << "Test completed, exiting..." << std::endl;
    
    return 0;
}
