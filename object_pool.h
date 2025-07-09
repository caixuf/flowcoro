#pragma once
#include <vector>
#include <stack>
#include <memory>
#include <mutex>

namespace flowcoro {

template<typename T>
class ObjectPool {
public:
    ObjectPool(size_t initial = 16) {
        for (size_t i = 0; i < initial; ++i) {
            pool.push(std::make_unique<T>());
        }
    }
    std::unique_ptr<T> acquire() {
        std::lock_guard<std::mutex> lock(mtx);
        if (pool.empty()) {
            return std::make_unique<T>();
        } else {
            auto obj = std::move(pool.top());
            pool.pop();
            return obj;
        }
    }
    void release(std::unique_ptr<T> obj) {
        std::lock_guard<std::mutex> lock(mtx);
        pool.push(std::move(obj));
    }
private:
    std::stack<std::unique_ptr<T>> pool;
    std::mutex mtx;
};

} // namespace flowcoro
