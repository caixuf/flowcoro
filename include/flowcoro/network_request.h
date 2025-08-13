#pragma once
#include <string>
#include <functional>

namespace flowcoro {

// 网络请求的抽象接口
class INetworkRequest {
public:
    virtual ~INetworkRequest() = default;

    // 发起网络请求的通用接口
    // url: 请求地址
    // callback: 回调函数，用于处理响应数据
    virtual void request(const std::string& url, const std::function<void(const std::string&)>& callback) = 0;
};

} // namespace flowcoro
