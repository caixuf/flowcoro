#include <flowcoro.hpp>
#include <iostream>

int main() {
    std::cout << "Testing if future combinators are available" << std::endl;
    
    // Test if we can see the functions
    std::cout << "sizeof(flowcoro::WhenAnyAwaiter<flowcoro::Task<int>>) = "
              << sizeof(flowcoro::WhenAnyAwaiter<flowcoro::Task<int>>) << std::endl;
    
    return 0;
}
