#include <flowcoro.hpp>

int main() {
    // Test if functions exist by trying to get their addresses
    auto* func1 = &flowcoro::when_any<flowcoro::Task<int>>;
    auto* func2 = &flowcoro::when_race<flowcoro::Task<int>>;
    auto* func3 = &flowcoro::when_all_settled<flowcoro::Task<int>>;
    
    (void)func1;
    (void)func2;
    (void)func3;
    
    return 0;
}
