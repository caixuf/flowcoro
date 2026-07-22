#pragma once

// =====================================================================
// flowcoro/hazard_pointer.h
//
// Hazard pointer based Safe Memory Reclamation (SMR) for lock-free
// data structures. Implements the classic Maged Michael scheme:
//
//   - Each thread owns K hazard slots (K = HAZARD_SLOTS_PER_THREAD).
//   - Before dereferencing a shared pointer P, a thread publishes P
//     into one of its hazard slots with a release store.
//   - Other threads that want to reclaim a node call retire(); the
//     node is appended to a per-thread retire list and reclaimed
//     lazily once no hazard slot references it any more.
//
// This breaks the ABA / use-after-free cycle in lockfree::Queue and
// lockfree::Stack: a node can never be freed while some concurrent
// dequeue/pop is still mid-read.
// =====================================================================

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flowcoro::smr {

// Maximum number of hazard slots a single thread can hold simultaneously.
// Two slots are enough for Michael-Scott queue dequeue:
//   slot 0 -> head node, slot 1 -> next node.
inline constexpr std::size_t HAZARD_SLOTS_PER_THREAD = 2;

// Maximum number of threads we support. Bounded to keep the slot
// table a flat array (cache-friendly, no locking on registration).
inline constexpr std::size_t MAX_HAZARD_THREADS = 128;

// Reclaim threshold = scan when retire list grows past this. Larger
// values mean less frequent scanning but more memory held in retire
// list. The classic bound is N * K, we use 4x to amortize scanning.
inline constexpr std::size_t RETIRE_THRESHOLD_FACTOR = 4;

// Function pointer deleter. Lambdas without captures convert implicitly.
using HazardDeleter = void (*)(void*);

struct RetiredNode {
    void* ptr;
    HazardDeleter deleter;
};

class HazardManager {
public:
    // Each thread's hazard record. Aligned to a cache line to avoid
    // false sharing between slots belonging to different threads.
    struct alignas(64) ThreadSlot {
        std::array<std::atomic<void*>, HAZARD_SLOTS_PER_THREAD> hazards{};
        std::vector<RetiredNode> retired;
        // Bug#4 修复：保护 retired vector 的 spinlock。
        // retire()（热路径，自己的 slot）加锁后 push_back；
        // drain_all()（冷路径，跨线程 scan 别人的 retired）用 try_lock，
        // 锁不上就跳过该线程（保守，不回收，避免堆损坏）。
        std::atomic_flag retired_lock = ATOMIC_FLAG_INIT;
        // Padding keeps slots on distinct cache lines (also helps the
        // retired vector land on its own line).
        char pad[64 - (sizeof(hazards) + sizeof(retired) + sizeof(retired_lock)) % 64];
    };

    // Destructor sets a flag so that drain_all() (called later from
    // ~Queue() during static destruction) becomes a no-op. This avoids
    // a static-destruction-order fiasco: ~HazardManager() frees the
    // retired vectors, then ~EventLoop() → ~Queue() → drain_all() →
    // scan() would read the freed memory.
    ~HazardManager() {
        destroyed_.store(true, std::memory_order_release);
    }

    static HazardManager& instance() noexcept {
        static HazardManager m;
        return m;
    }

    // Returns a stable per-thread id in [0, MAX_HAZARD_THREADS).
    // Idempotent: a thread always gets the same id.
    std::size_t thread_id() noexcept {
        thread_local std::size_t tid = register_thread_impl();
        return tid;
    }

    // Publish `ptr` into the calling thread's slot `slot`. Other threads
    // reclaiming the same pointer will observe it and skip reclaim.
    void set_hazard(std::size_t slot, void* ptr) noexcept {
        auto& s = slot_of(thread_id());
        s.hazards[slot].store(ptr, std::memory_order_release);
        // Compiler/arch fence: ensure the hazard is visible to other
        // cores before we dereference the pointer that follows.
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void clear_hazard(std::size_t slot) noexcept {
        auto& s = slot_of(thread_id());
        s.hazards[slot].store(nullptr, std::memory_order_release);
    }

    // Read the current hazard value of some thread's slot. Used by
    // scan() to figure out which retired pointers are still in flight.
    void* read_hazard(std::size_t tid, std::size_t slot) const noexcept {
        return slots_[tid].hazards[slot].load(std::memory_order_acquire);
    }

    // Enqueue a pointer for reclamation. Reclamation may be deferred
    // until the retire list crosses the threshold. The caller MUST
    // guarantee that no further dereferences of `ptr` will happen on
    // the current thread after this call.
    void retire(void* ptr, HazardDeleter deleter) {
        auto tid = thread_id();
        auto& s = slot_of(tid);
        // Bug#4 修复：加锁保护 retired vector，防止 drain_all() 跨线程
        // scan 时与 push_back 并发导致堆损坏。
        lock_retired(s);
        s.retired.push_back({ptr, deleter});
        const bool need_scan = s.retired.size() >
            RETIRE_THRESHOLD_FACTOR * HAZARD_SLOTS_PER_THREAD * MAX_HAZARD_THREADS;
        unlock_retired(s);
        if (need_scan) {
            scan(tid);
        }
    }

    // Reclaim all retired nodes that are no longer hazardous on the
    // current thread. Should be called when a thread is about to exit
    // or when the queue is being torn down.
    void drain_current_thread() noexcept {
        auto tid = thread_id();
        scan(tid, /*force_all=*/true);
    }

    // Best-effort: try to reclaim on all registered threads. Called
    // from Queue destructor to avoid leaking memory.
    //
    // Bug#4 修复：用 try_lock 保护跨线程 scan。如果某线程的 retired_lock
    // 锁不上（说明该线程正在 retire），就跳过该线程（保守，不回收）。
    // 这样即使 worker 未 join 干净，也不会与 drain_all 的 scan 并发
    // 操作 retired vector 导致堆损坏。
    void drain_all() noexcept {
        if (destroyed_.load(std::memory_order_acquire)) return;
        const std::size_t n = next_thread_id_.load(std::memory_order_relaxed);
        for (std::size_t t = 0; t < n && t < MAX_HAZARD_THREADS; ++t) {
            scan(t, /*force_all=*/true);
        }
    }

    // Stats (for testing): how many retired nodes are pending reclamation
    // across all threads.
    std::size_t pending_retired_count() const noexcept {
        std::size_t total = 0;
        const std::size_t n = next_thread_id_.load(std::memory_order_relaxed);
        for (std::size_t t = 0; t < n && t < MAX_HAZARD_THREADS; ++t) {
            total += slots_[t].retired.size();
        }
        return total;
    }

private:
    HazardManager() = default;
    HazardManager(const HazardManager&) = delete;
    HazardManager& operator=(const HazardManager&) = delete;

    std::size_t register_thread_impl() noexcept {
        std::size_t id = next_thread_id_.fetch_add(1, std::memory_order_relaxed);
        // Wrap-around guard: if a test runtime spawns more than
        // MAX_HAZARD_THREADS threads, we degrade by sharing slots. This
        // is not safe in general but at least keeps us in bounds.
        return id % MAX_HAZARD_THREADS;
    }

    ThreadSlot& slot_of(std::size_t tid) noexcept {
        return slots_[tid];
    }

    const ThreadSlot& slot_of(std::size_t tid) const noexcept {
        return slots_[tid];
    }

    // Bug#4: spinlock helpers for retired vector.
    static void lock_retired(ThreadSlot& s) noexcept {
        while (s.retired_lock.test_and_set(std::memory_order_acquire)) {
            // spin
        }
    }
    static void unlock_retired(ThreadSlot& s) noexcept {
        s.retired_lock.clear(std::memory_order_release);
    }

    // Force reclaim any retired nodes for thread `tid` that are not
    // currently held by any hazard pointer. If force_all=true, also
    // re-reclaim what's left, which after another scan attempt will
    // only leave truly still-hazardous nodes (still in use).
    void scan(std::size_t tid, bool force_all = false) noexcept {
        auto& slot = slot_of(tid);

        // Bug#4 修复：如果 scan 是由 drain_all() 对其他线程发起的（tid !=
        // 当前线程），用 try_lock 保护 retired vector。锁不上说明该线程
        // 正在 retire，跳过以避免堆损坏。自己的线程无需锁（单线程访问）。
        bool is_self = (tid == thread_id());
        bool locked = false;
        if (!is_self) {
            if (!slot.retired_lock.test_and_set(std::memory_order_acquire)) {
                locked = true;
            } else {
                return; // 该线程正在 retire，跳过
            }
        }

        auto& my_retired = slot.retired;
        if (my_retired.empty()) {
            if (locked) unlock_retired(slot);
            return;
        }

        // Bug#1 修复（命门）：reclaimer 侧补 seq_cst fence。
        //
        // Maged-Michael HP 的正确性靠两侧对称的 store-load 全序：
        //   protector 侧：[存 hazard(release) → fence(seq_cst) → 读 head 校验]
        //   reclaimer 侧：[摘链 CAS → **fence(seq_cst)** → 读 hazard 快照]
        //
        // set_hazard() 已有 fence，但 scan() 这里只 load(acquire) 读 hazard，
        // 缺少对称的 fence。弱序模型下 reclaimer 可能读到过期的 null hazard
        // → 提前 pool_free → 其他线程 next->data / head_node->next 命中已释放内存。
        //
        // 必须在快照循环之前插入 seq_cst fence，保证 reclaimer 的「摘链 CAS」
        // 与 protector 的「存 hazard」之间有全序关系。
        //
        // 注意：TSAN 对 atomic_thread_fence(seq_cst) 建模很弱，加完 fence 后
        // TSAN 可能对 fence 本身报假阳性。用具名 suppression 压掉 fence 自身
        // 的假阳性即可，绝不能因此删掉 fence——删了 = Bug#1 复活。
        std::atomic_thread_fence(std::memory_order_seq_cst);

        // Snapshot all currently published hazards.
        std::vector<void*> current_hazards;
        current_hazards.reserve(MAX_HAZARD_THREADS * HAZARD_SLOTS_PER_THREAD);
        const std::size_t n = next_thread_id_.load(std::memory_order_relaxed);
        for (std::size_t t = 0; t < n && t < MAX_HAZARD_THREADS; ++t) {
            for (std::size_t s = 0; s < HAZARD_SLOTS_PER_THREAD; ++s) {
                void* h = slots_[t].hazards[s].load(std::memory_order_acquire);
                if (h) current_hazards.push_back(h);
            }
        }
        std::sort(current_hazards.begin(), current_hazards.end());

        std::vector<RetiredNode> still_hazardous;
        still_hazardous.reserve(my_retired.size());
        for (auto& rn : my_retired) {
            if (std::binary_search(current_hazards.begin(),
                                   current_hazards.end(), rn.ptr)) {
                still_hazardous.push_back(rn);
            } else {
                // Safe to reclaim now.
                rn.deleter(rn.ptr);
            }
        }
        my_retired.swap(still_hazardous);

        if (locked) unlock_retired(slot);

        // If force_all and we still have nodes left, there is nothing
        // more we can do — they are genuinely still referenced. Leave
        // them for a future scan. (force_all mainly ensures we tried.)
        (void)force_all;
    }

    std::array<ThreadSlot, MAX_HAZARD_THREADS> slots_{};
    std::atomic<std::size_t> next_thread_id_{0};
    std::atomic<bool> destroyed_{false};
};

// RAII helper: acquires a hazard slot for the lifetime of the guard,
// automatically clearing it on destruction. Use this in dequeue/pop
// hot paths to keep the critical sections tidy.
class HazardGuard {
public:
    HazardGuard(std::size_t slot) noexcept : slot_(slot) {}

    void protect(void* ptr) noexcept {
        HazardManager::instance().set_hazard(slot_, ptr);
        ptr_ = ptr;
    }

    void release() noexcept {
        if (ptr_) {
            HazardManager::instance().clear_hazard(slot_);
            ptr_ = nullptr;
        }
    }

    ~HazardGuard() noexcept { release(); }

    void* get() const noexcept { return ptr_; }

    // Convenience: like get() but typed.
    template<typename T>
    T* as() const noexcept {
        return static_cast<T*>(ptr_);
    }

    HazardGuard(const HazardGuard&) = delete;
    HazardGuard& operator=(const HazardGuard&) = delete;

private:
    std::size_t slot_;
    void* ptr_ = nullptr;
};

// Convenience: retire `ptr` using `deleter`. The retire happens
// asynchronously (lazily) once no hazard slot references `ptr` any
// more. `deleter` must be a non-capturing callable convertible to
// HazardDeleter (void(*)(void*)).
template<typename T, typename Deleter>
inline void retire_node(T* ptr, Deleter deleter) noexcept {
    HazardManager::instance().retire(
        static_cast<void*>(ptr),
        static_cast<HazardDeleter>(deleter));
}

} // namespace flowcoro::smr
