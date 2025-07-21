#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>

int main(int argc, char* argv[]) {
    std::cout << "🧪 FlowCoro Unified Test Runner" << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    
    // 简化的测试套件，运行子进程
    std::vector<std::pair<std::string, std::string>> test_executables = {
        {"Core Functionality", "./test_core"},
        {"Database Layer", "./test_database"}, 
        {"HTTP Client", "./test_http_client"},
        {"Simple DB", "./test_simple_db"},
        {"RPC System", "./test_rpc"},
        {"Sleep Tests", "./test_sleep_only"}
    };
    
    int total_passed = 0;
    int total_failed = 0;
    
    for (const auto& [name, executable] : test_executables) {
        std::cout << "\n🔬 Running: " << name << std::endl;
        std::cout << std::string(30, '-') << std::endl;
        
        // 运行测试可执行文件
        int result = std::system(executable.c_str());
        
        if (result == 0) {
            std::cout << "✅ " << name << " - PASSED" << std::endl;
            total_passed++;
        } else {
            std::cout << "❌ " << name << " - FAILED" << std::endl;
            total_failed++;
        }
    }
    
    // 打印总结
    std::cout << "\n🎯 Test Summary:" << std::endl;
    std::cout << std::string(50, '=') << std::endl;
    std::cout << "✅ Passed: " << total_passed << std::endl;
    std::cout << "❌ Failed: " << total_failed << std::endl;
    std::cout << "📊 Total:  " << (total_passed + total_failed) << std::endl;
    
    if (total_failed == 0) {
        std::cout << "\n🎉 All test suites passed!" << std::endl;
        return 0;
    } else {
        std::cout << "\n💥 " << total_failed << " test suite(s) failed!" << std::endl;
        return 1;
    }
}
