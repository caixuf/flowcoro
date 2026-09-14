/**
 * @file test_cpu_affinity.cpp
 * @brief cpu_affinity 契约测试：物理核枚举（SMT 兄弟不重合）+ 绑核生效。
 *
 * 口径来源：IMFL 测试平台的 scripts/deploy/cpu_topology.sh 用
 * thread_siblings_list 分组（不是 core_id 去重），并被
 * tests/unit/test_physical_core_contract.py 钉死为
 * 「Intel 12 物理核 ×2 线程 → 0,12 / 1,13 / ... / 11,23」，绝不是 0,1。
 * C++ 侧必须同口径，否则会把 SMT 兄弟线程派给两个 worker = 假并行。
 *
 * 拓扑通过环境变量 FLOWCORO_SYSFS_CPU 注入假 sysfs 目录来验证。
 *
 * 注意：这里**故意不用 std::filesystem**。本仓库测试目标在 Release 下会同时
 * 拿到 `-O3 -flto=auto`（来自 CMAKE_CXX_FLAGS_RELEASE）与 `-fno-inline`
 * （来自 tests/CMakeLists.txt），该组合在 GCC 13.3 上会让 filesystem 的
 * path 构造在静态初始化期抛 bad_alloc（已最小复现：纯 <filesystem> +
 * 同样 flags 即 abort）。用 POSIX 目录 API 绕开这个坑。
 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "flowcoro/cpu_affinity.h"
#include "flowcoro/thread_pool.h"
#include "test_framework.h"

#ifdef __linux__
#include <sched.h>
#endif

#ifdef _WIN32
#include <direct.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace flowcoro;
using namespace flowcoro::test;

namespace {

// ---------------------------------------------------------------------------
// 极简目录/文件工具（替代 std::filesystem，原因见文件头注释）
// ---------------------------------------------------------------------------

void set_env(const char* key, const std::string& value) {
#ifdef _WIN32
    _putenv_s(key, value.c_str());
#else
    ::setenv(key, value.c_str(), 1);
#endif
}

std::string temp_root() {
    for (const char* key : {"TMPDIR", "TEMP", "TMP"}) {
        if (const char* value = std::getenv(key)) {
            if (*value != '\0') return std::string(value);
        }
    }
    return "/tmp";
}

void make_dir(const std::string& path) {
#ifdef _WIN32
    ::_mkdir(path.c_str());
#else
    ::mkdir(path.c_str(), 0755);
#endif
}

// 逐级创建目录（path 用 '/' 分隔）
void make_dirs(const std::string& path) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current += path[i];
        if (path[i] == '/' || i + 1 == path.size()) {
            if (current.size() > 1) make_dir(current);
        }
    }
}

void remove_tree(const std::string& path) {
#ifndef _WIN32
    if (DIR* dir = ::opendir(path.c_str())) {
        while (dirent* entry = ::readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            remove_tree(path + "/" + name);
        }
        ::closedir(dir);
    }
    if (::unlink(path.c_str()) != 0) {
        ::rmdir(path.c_str());
    }
#else
    (void)path;
#endif
}

void write_file(const std::string& path, const std::string& content) {
    const auto slash = path.find_last_of('/');
    if (slash != std::string::npos) make_dirs(path.substr(0, slash));
    std::ofstream out(path);
    out << content << "\n";
}

// FLOWCORO_SYSFS_CPU 指向注入目录，析构时恢复
class ScopedSysfsRoot {
public:
    explicit ScopedSysfsRoot(const std::string& root) {
        set_env("FLOWCORO_SYSFS_CPU", root);
    }
    ~ScopedSysfsRoot() {
#ifdef _WIN32
        _putenv_s("FLOWCORO_SYSFS_CPU", "");
#else
        ::unsetenv("FLOWCORO_SYSFS_CPU");
#endif
    }
};

std::string join(const std::vector<int>& values, const std::string& sep) {
    std::string s;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i) s += sep;
        s += std::to_string(values[i]);
    }
    return s;
}

std::string fmt(const std::vector<int>& values) {
    return "[" + join(values, ",") + "]";
}

// 构造假 sysfs 拓扑：nphys 个物理核 × smt 线程。
// pair_adjacent=true 时兄弟线程相邻（0,1 / 2,3 …），否则为 Intel 风格
// （0,12 / 1,13 …）。range_form 用 "0-1" 区间写法（仅 smt==2 时）。
std::string make_topology(const std::string& name, int nphys, int smt,
                          bool pair_adjacent, bool range_form) {
    const std::string root = temp_root() + "/" + name;
    remove_tree(root);

    const int nlog = nphys * smt;
    for (int i = 0; i < nlog; ++i) {
        const std::string topo =
            root + "/cpu" + std::to_string(i) + "/topology";
        write_file(topo + "/core_id", std::to_string(i % nphys));
        write_file(topo + "/physical_package_id", "0");

        std::vector<int> siblings;
        for (int t = 0; t < smt; ++t) {
            siblings.push_back(pair_adjacent ? (i / smt) * smt + t
                                             : (i % nphys) + nphys * t);
        }
        std::sort(siblings.begin(), siblings.end());

        std::string text;
        if (range_form && siblings.size() == 2) {
            text = std::to_string(siblings[0]) + "-" + std::to_string(siblings[1]);
        } else {
            text = std::to_string(siblings[0]);
            for (size_t k = 1; k < siblings.size(); ++k) {
                text += "," + std::to_string(siblings[k]);
            }
        }
        write_file(topo + "/thread_siblings_list", text);
    }
    write_file(root + "/possible", "0-" + std::to_string(nlog - 1));
    return root;
}

bool contains(const std::vector<int>& v, int value) {
    return std::find(v.begin(), v.end(), value) != v.end();
}

#ifdef __linux__
std::vector<int> current_affinity() {
    std::vector<int> cpus;
    cpu_set_t set;
    CPU_ZERO(&set);
    if (::sched_getaffinity(0, sizeof(set), &set) == 0) {
        for (int c = 0; c < CPU_SETSIZE; ++c) {
            if (CPU_ISSET(static_cast<size_t>(c), &set)) cpus.push_back(c);
        }
    }
    return cpus;
}
#endif

}  // namespace

// ---------------------------------------------------------------------------
// 物理核枚举
// ---------------------------------------------------------------------------

TEST_CASE(physical_cores_intel_style_siblings_pick_one_per_group) {
    std::cout << "\n=== Intel 风格 12 核 x2 线程：每组取一个代表 ===\n";
    const std::string root = make_topology("flowcoro_sysfs_intel", 12, 2,
                                          /*pair_adjacent=*/false,
                                          /*range_form=*/false);

    std::vector<int> expected(12);
    std::iota(expected.begin(), expected.end(), 0);

    {
        ScopedSysfsRoot guard(root);
        const std::vector<int> cores = physical_core_cpus();
        std::cout << "  cores=" << fmt(cores) << std::endl;
        TEST_EXPECT_TRUE(cores == expected);
        TEST_EXPECT_EQ(physical_core_count(), static_cast<size_t>(12));
        // SMT 兄弟（0 与 12）绝不同时入选
        TEST_EXPECT_FALSE(contains(cores, 12));
        TEST_EXPECT_FALSE(contains(cores, 23));
    }
    remove_tree(root);
}

TEST_CASE(physical_cores_adjacent_siblings_skip_the_twin) {
    std::cout << "\n=== 相邻兄弟风格（0-1 / 2-3 …）：必须跳过 1 ===\n";
    const std::string root = make_topology("flowcoro_sysfs_adjacent", 12, 2,
                                          /*pair_adjacent=*/true,
                                          /*range_form=*/true);

    std::vector<int> expected;
    for (int c = 0; c < 12; ++c) expected.push_back(c * 2);

    {
        ScopedSysfsRoot guard(root);
        const std::vector<int> cores = physical_core_cpus();
        std::cout << "  cores=" << fmt(cores) << std::endl;
        TEST_EXPECT_TRUE(cores == expected);
        TEST_EXPECT_FALSE(contains(cores, 1));
        TEST_EXPECT_FALSE(contains(cores, 23));
    }
    remove_tree(root);
}

TEST_CASE(physical_cores_sysfs_unavailable_falls_back_to_logical_range) {
    std::cout << "\n=== sysfs 不可用：回退 0..hw-1 ===\n";
    const std::string root = temp_root() + "/flowcoro_sysfs_missing";
    remove_tree(root);
    make_dirs(root);  // 存在但没有任何 cpu* 子目录

    {
        ScopedSysfsRoot guard(root);
        const std::vector<int> cores = physical_core_cpus();
        const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
        std::cout << "  fallback cores=" << fmt(cores) << " (hw=" << hw << ")"
                  << std::endl;
        TEST_EXPECT_TRUE(!cores.empty());
        TEST_EXPECT_EQ(cores.size(), static_cast<size_t>(hw));
        TEST_EXPECT_TRUE(std::is_sorted(cores.begin(), cores.end()));
        TEST_EXPECT_EQ(cores.front(), 0);
    }
    remove_tree(root);
}

TEST_CASE(parse_cpu_list_handles_ranges_and_lists) {
    std::cout << "\n=== parse_cpu_list ===\n";
    std::vector<int> zero_to_23(24);
    std::iota(zero_to_23.begin(), zero_to_23.end(), 0);
    const std::vector<int> empty;
    const std::vector<int> pair_asc{0, 12};
    const std::vector<int> pair_desc{12, 0};
    const std::vector<int> multi{0, 1, 24, 25};

    TEST_EXPECT_TRUE(parse_cpu_list("0-23") == zero_to_23);
    TEST_EXPECT_TRUE(parse_cpu_list("0,12") == pair_asc);
    // 输入乱序也会归一到升序（与 shell 侧 sort -n 同口径）
    TEST_EXPECT_TRUE(parse_cpu_list("12,0") == pair_asc);
    TEST_EXPECT_TRUE(parse_cpu_list("12,0") != pair_desc);
    TEST_EXPECT_TRUE(parse_cpu_list("0-1,24-25") == multi);
    TEST_EXPECT_TRUE(parse_cpu_list("") == empty);
    TEST_EXPECT_TRUE(parse_cpu_list("bogus") == empty);
}

// ---------------------------------------------------------------------------
// 绑核生效
// ---------------------------------------------------------------------------

TEST_CASE(pin_current_thread_to_cpu_restricts_affinity_mask) {
    std::cout << "\n=== pin_current_thread_to_cpu ===\n";

    // 越界/负数 CPU 必须失败但不崩溃
    TEST_EXPECT_FALSE(pin_current_thread_to_cpu(-1));
    TEST_EXPECT_FALSE(pin_current_thread_to_cpu(1000000));

#ifndef __linux__
    std::cout << "  (非 Linux 平台：绑核为 no-op，跳过掩码校验)\n";
#else
    const std::vector<int> cores = physical_core_cpus();
    if (cores.empty()) {
        std::cout << "  (无法枚举物理核，跳过)\n";
        return;
    }

    cpu_set_t saved;
    CPU_ZERO(&saved);
    const bool have_saved = ::sched_getaffinity(0, sizeof(saved), &saved) == 0;

    const int target = cores.front();
    TEST_EXPECT_TRUE(pin_current_thread_to_cpu(target));
    TEST_EXPECT_TRUE(current_affinity() == std::vector<int>{target});

    // 还原，避免影响同进程后续用例
    if (have_saved) {
        ::sched_setaffinity(0, sizeof(saved), &saved);
    }
#endif
}

TEST_CASE(thread_pool_workers_get_distinct_physical_cores) {
    std::cout << "\n=== ThreadPool(threads, cpus)：worker 绑到不同物理核 ===\n";

    const std::vector<int> cores = physical_core_cpus();
    if (cores.size() < 2) {
        std::cout << "  (物理核不足 2，跳过)\n";
        return;
    }
    const size_t n = std::min<size_t>(cores.size(), 4);
    const std::vector<int> cpus(cores.begin(), cores.begin() + static_cast<long>(n));

    {
        lockfree::ThreadPool pool(n, cpus);

        // 池是单条 MPMC 队列 + n 个 worker，任务落到哪个 worker 不确定。
        // 用栅栏保证「n 个任务同时被 n 个 worker 各持一个」，这样采样到的
        // 亲和性恰好覆盖全部 worker。
        std::mutex mtx;
        std::vector<std::vector<int>> observed;
        std::atomic<int> started{0};
        std::atomic<bool> release{false};

        std::vector<std::future<void>> futures;
        futures.reserve(n);
        for (size_t k = 0; k < n; ++k) {
            futures.push_back(pool.enqueue([&mtx, &observed, &started, &release] {
#ifdef __linux__
                std::vector<int> affinity = current_affinity();
#else
                std::vector<int> affinity{-1};
#endif
                {
                    std::lock_guard<std::mutex> lock(mtx);
                    observed.push_back(std::move(affinity));
                }
                started.fetch_add(1, std::memory_order_release);
                while (!release.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }));
        }

        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (started.load(std::memory_order_acquire) < static_cast<int>(n) &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        release.store(true, std::memory_order_release);
        for (auto& f : futures) f.get();

        TEST_EXPECT_EQ(observed.size(), n);
#ifdef __linux__
        std::vector<int> picked;
        for (const auto& v : observed) {
            std::cout << "  worker affinity=" << fmt(v) << std::endl;
            TEST_EXPECT_EQ(v.size(), static_cast<size_t>(1));
            if (!v.empty()) picked.push_back(v.front());
        }
        std::sort(picked.begin(), picked.end());
        std::cout << "  picked=" << fmt(picked) << " expected=" << fmt(cpus)
                  << std::endl;
        // 每个 worker 拿到自己的那个物理核，且互不重合
        TEST_EXPECT_TRUE(picked == cpus);
#else
        std::cout << "  (非 Linux 平台：绑核为 no-op，跳过亲和性校验)\n";
#endif
    }

    // 不传 cpus：不绑核也不应报错
    {
        lockfree::ThreadPool plain(2);
        auto f = plain.enqueue([] { return 42; });
        TEST_EXPECT_EQ(f.get(), 42);
    }
}

int main() {
    TEST_SUITE("cpu_affinity");
    TestRunner::print_summary();
    return TestRunner::all_passed() ? 0 : 1;
}
