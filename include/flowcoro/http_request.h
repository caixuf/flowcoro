#pragma once
#include "flowcoro.h"
#include <curl/curl.h>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace flowcoro {

// 使用libcurl实现的HTTP请求类
class HttpRequest : public INetworkRequest {
public:
    HttpRequest() {
        // 启动工作线程
        worker_thread_ = std::thread(&HttpRequest::worker_thread_func, this);
        // 初始化libcurl
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~HttpRequest() override {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stop_worker_thread_ = true;
        }
        queue_condition_.notify_one();
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        // 清理libcurl
        curl_global_cleanup();
    }

    void request(const std::string& url, const std::function<void(const std::string&)>& callback) override {
        // 创建新的CURL句柄
        CURL* curl = curl_easy_init();
        if (!curl) {
            // 错误处理
            return;
        }

        // 添加到请求队列
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            requests_queue_.push({url, curl, callback});
        }
        queue_condition_.notify_one();
    }

private:
    struct RequestInfo {
        std::string url;
        CURL* curl;
        std::function<void(const std::string&)> callback;
    };

    std::thread worker_thread_;
    std::mutex queue_mutex_;
    std::queue<RequestInfo> requests_queue_;
    std::condition_variable queue_condition_;
    bool stop_worker_thread_ = false;

    // 工作线程函数
    void worker_thread_func() {
        while (true) {
            RequestInfo request;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_condition_.wait(lock, [this] {
                    return stop_worker_thread_ || !requests_queue_.empty();
                });

                if (stop_worker_thread_ && requests_queue_.empty()) {
                    break;
                }

                if (!requests_queue_.empty()) {
                    request = requests_queue_.front();
                    requests_queue_.pop();
                } else {
                    continue;
                }
            }

            // 执行网络请求
            ResponseData response_data;
            curl_easy_setopt(request.curl, CURLOPT_URL, request.url.c_str());
            curl_easy_setopt(request.curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(request.curl, CURLOPT_WRITEDATA, &response_data);

            CURLcode res = curl_easy_perform(request.curl);

            // 处理响应
            if (res == CURLE_OK && request.callback) {
                request.callback(response_data.data);
            }

            // 清理
            curl_easy_cleanup(request.curl);
        }
    }

    struct ResponseData {
        std::string data;
        size_t total_size = 0;
    };

    // libcurl写回调函数
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t realsize = size * nmemb;
        ResponseData* response = static_cast<ResponseData*>(userp);
        char* data = static_cast<char*>(contents);
        response->data.append(data, realsize);
        return realsize;
    }
};

} // namespace flowcoro