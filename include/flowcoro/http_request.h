#pragma once
#include "flowcoro.h"
#include <curl/curl.h>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace flowcoro {

// libcurlHTTP
class HttpRequest : public INetworkRequest {
public:
    HttpRequest() {
        // 
        worker_thread_ = std::thread(&HttpRequest::worker_thread_func, this);
        // libcurl
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
        // libcurl
        curl_global_cleanup();
    }

    void request(const std::string& url, const std::function<void(const std::string&)>& callback) override {
        // CURL
        CURL* curl = curl_easy_init();
        if (!curl) {
            // 
            return;
        }

        // 
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

    // 
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

            // 
            ResponseData response_data;
            curl_easy_setopt(request.curl, CURLOPT_URL, request.url.c_str());
            curl_easy_setopt(request.curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(request.curl, CURLOPT_WRITEDATA, &response_data);

            CURLcode res = curl_easy_perform(request.curl);

            // 
            if (res == CURLE_OK && request.callback) {
                request.callback(response_data.data);
            }

            // 
            curl_easy_cleanup(request.curl);
        }
    }

    struct ResponseData {
        std::string data;
        size_t total_size = 0;
    };

    // libcurl
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t realsize = size * nmemb;
        ResponseData* response = static_cast<ResponseData*>(userp);
        char* data = static_cast<char*>(contents);
        response->data.append(data, realsize);
        return realsize;
    }
};

} // namespace flowcoro