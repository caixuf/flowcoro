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
        // Padding keeps slots on distinct cache lines (also helps the
        // retired vector land on its own line).
        char pad[64 - (sizeof(hazards) + sizeof(retired)) % 64];
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
        s.retired.push_back({ptr, deleter});
        // Bound: scan once retire list grows past 4 * K * N.
        const std::size_t threshold =
            RETIRE_THRESHOLD_FACTOR * HAZARD_SLOTS_PER_THREAD * MAX_HAZARD_THREADS;
        if (s.retired.size() > threshold) {
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

    // Force reclaim any retired nodes for thread `tid` that are not
    // currently held by any hazard pointer. If force_all=true, also
    // re-reclaim what's left, which after another scan attempt will
    // only leave truly still-hazardous nodes (still in use).
    void scan(std::size_t tid, bool force_all = false) noexcept {
        auto& my_retired = slot_of(tid).retired;
        if (my_retired.empty()) return;

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
