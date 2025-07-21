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
        return 1;
    }
    
    std::cout << "Test completed, exiting..." << std::endl;
    
    // 立即退出，避免全局对象析构问题
    std::_Exit(0);
}
