// =====================================================================
// flowcoro/cpu_affinity.h
//
// 线程绑核与物理核枚举。
//
// 为什么需要：
//   - lockfree::ThreadPool / rt::RtExecutor / CoroutineScheduler 都需要把
//     线程绑到确定的核上，此前各自内联了一份实现（rt_executor.h 用
//     sched_setaffinity，coroutine_pool.cpp 用 pthread_setaffinity_np），
//     口径不一。这里统一成一份。
//   - 绑核必须按「物理核」而不是「逻辑核」：SMT 兄弟线程共享执行单元，
//     把两个 worker 派到 0/1（同一物理核的两个线程）等于没并行。
//
// 口径与 IMFL 测试平台的 scripts/deploy/cpu_topology.sh 完全一致：
//   优先读 thread_siblings_list（兄弟线程组），每组取一个代表 CPU；
//   而不是按 core_id 去重 —— hybrid/多 die 拓扑下两者会分叉。
//   该契约被测试钉死：Intel 12 物理核 ×2 线程 → 0,12 / 1,13 / ... / 11,23。
//
// 跨平台：非 Linux 平台 pin_current_thread_to_cpu 返回 false（不绑核、
// 不报错），physical_core_cpus 回退为 0..hardware_concurrency()-1。
// =====================================================================

#ifndef FLOWCORO_CPU_AFFINITY_H
#define FLOWCORO_CPU_AFFINITY_H

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
#include <sched.h>
#endif

namespace flowcoro {

// sysfs CPU 拓扑根目录。默认 /sys/devices/system/cpu；
// 可用环境变量覆盖（测试注入假拓扑时用，对应 shell 侧的 IMFL_SYSFS_CPU）。
inline std::string cpu_sysfs_root() {
    if (const char* env = std::getenv("FLOWCORO_SYSFS_CPU")) {
        if (*env != '\0') return env;
    }
    return "/sys/devices/system/cpu";
}

// 把 "0-1,24-25" 形式的 CPU 列表展开为有序 id 列表（升序、去重）。
inline std::vector<int> parse_cpu_list(const std::string& text) {
    std::vector<int> cpus;
    std::stringstream ss(text);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // 去掉空白
        const auto begin = token.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) continue;
        const auto end = token.find_last_not_of(" \t\r\n");
        token = token.substr(begin, end - begin + 1);

        const auto dash = token.find('-');
        if (dash == std::string::npos) {
            try {
                cpus.push_back(std::stoi(token));
            } catch (...) {
                // 忽略非法项
            }
            continue;
        }
        try {
            const int lo = std::stoi(token.substr(0, dash));
            const int hi = std::stoi(token.substr(dash + 1));
            for (int c = lo; c <= hi; ++c) cpus.push_back(c);
        } catch (...) {
            // 忽略非法项
        }
    }
    std::sort(cpus.begin(), cpus.end());
    cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
    return cpus;
}

// 读取 sysfs 文件全部内容。失败返回空串。
inline std::string read_sysfs_file(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return {};
    std::string content;
    std::getline(in, content);
    return content;
}

// 枚举候选 CPU 号。优先读 {root}/possible（"0-23"），
// 读不到则用 hardware_concurrency() 兜底。
inline std::vector<int> candidate_cpus(const std::string& root) {
    std::vector<int> cpus = parse_cpu_list(read_sysfs_file(root + "/possible"));
    if (!cpus.empty()) return cpus;

    const unsigned hw = std::thread::hardware_concurrency();
    const int n = hw > 0 ? static_cast<int>(hw) : 1;
    cpus.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) cpus.push_back(i);
    return cpus;
}

// 物理核代表 CPU 列表：每个 SMT 兄弟组取一个代表（组内最小 CPU 号），
// 结果按代表 CPU 号升序。与 cpu_topology.sh 的 _detect_phys_core_cpusets 同口径。
//
// 返回空表示无法从 sysfs 判定（此时调用方应回退到 candidate_cpus）。
inline std::vector<int> physical_core_cpus_from_sysfs(const std::string& root) {
    std::vector<int> representatives;
    for (const int cpu : candidate_cpus(root)) {
        const std::string path =
            root + "/cpu" + std::to_string(cpu) + "/topology/thread_siblings_list";
        std::vector<int> siblings = parse_cpu_list(read_sysfs_file(path));
        if (siblings.empty()) continue;  // 该 cpu 目录不存在或不可读

        const int rep = siblings.front();  // 组内最小 CPU 号
        if (std::find(representatives.begin(), representatives.end(), rep) ==
            representatives.end()) {
            representatives.push_back(rep);
        }
    }
    std::sort(representatives.begin(), representatives.end());
    return representatives;
}

// 本机物理核代表 CPU 列表。sysfs 不可用时回退为 0..hardware_concurrency()-1。
inline std::vector<int> physical_core_cpus() {
    std::vector<int> cores = physical_core_cpus_from_sysfs(cpu_sysfs_root());
    if (!cores.empty()) return cores;
    return candidate_cpus(cpu_sysfs_root());
}

// 本机物理核数（绑定并发上限时用这个，别用 cpu_count/hardware_concurrency）。
inline size_t physical_core_count() {
    return physical_core_cpus().size();
}

// 把当前线程绑到指定 CPU。成功返回 true。
// Linux 之外返回 false（不绑核也不报错，调用方无需分支）。
inline bool pin_current_thread_to_cpu(int cpu) {
#ifdef __linux__
    if (cpu < 0) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<size_t>(cpu), &set);
    return ::sched_setaffinity(0, sizeof(set), &set) == 0;
#else
    (void)cpu;
    return false;
#endif
}

}  // namespace flowcoro

#endif  // FLOWCORO_CPU_AFFINITY_H
