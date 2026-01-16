#pragma once
#include <string>
#include <functional>

namespace flowcoro {

// 
class INetworkRequest {
public:
    virtual ~INetworkRequest() = default;

    // 
    // url: 
    // callback: 
    virtual void request(const std::string& url, const std::function<void(const std::string&)>& callback) = 0;
};

} // namespace flowcoro
