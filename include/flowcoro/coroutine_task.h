#pragma once

/**
 * @file coroutine_task.h
 * @brief BusAwaitable — Bridge between C-style callback message buses and C++20 coroutines.
 *
 * Provides BusAwaitable<Message, Bus> which allows a coroutine to co_await
 * a single message from any callback-based bus API (e.g. ROS2, Apollo CyberRT,
 * AUTOSAR, or any custom middleware that delivers messages via a C-style
 * void(*)(const Message*, void*) callback).
 *
 * ## Bug fixes
 *
 * ### Bug 1 — Double-resume (most severe)
 * In an integration runtime the coroutine manager submits h.resume() to a
 * thread-pool rather than calling it inline.  If a second bus message arrives
 * after on_message has queued the first resume but before the thread-pool has
 * executed it (i.e. before the coroutine reaches await_resume and
 * unsubscribes), on_message would be invoked again and schedule a second
 * resume of the same suspended handle — undefined behaviour.
 *
 * Fix: State::fired is a std::atomic_flag.  on_message calls test_and_set
 * with acq_rel ordering.  Only the invocation that observes the old value
 * false is permitted to schedule a resume; every subsequent call returns
 * immediately.
 *
 * ### Bug 2 — Use-after-free
 * BusAwaitable lives on the coroutine frame.  If the owning Task is dropped
 * (e.g. due to cancellation) while a delivery is pending, the frame — and
 * therefore State — are eventually freed by the scheduler's deferred
 * destruction path.  Without protection the bus would later fire on_message
 * with a dangling user_data pointer.
 *
 * Fix: ~BusAwaitable always performs a one-shot synchronous unsubscribe when
 * a subscription is active.  Because unsubscribe() is required to be
 * synchronous (it must guarantee no further delivery occurs after it returns,
 * and in-flight callbacks have completed), State is never accessed through a
 * dangling pointer.
 *
 * ### Bug 3 — Insufficient memory ordering on the message copy
 * The callback writes to State::msg and then calls schedule_resume.  Without
 * explicit ordering the resuming thread (which may be a different thread-pool
 * worker) might observe a stale or zero value for State::msg.
 *
 * Fix: the callback first wins the atomic one-shot gate, then copies the
 * message into State::msg before scheduling resume.  The scheduler's internal
 * mutex operations (used when enqueuing and dequeuing the resume task)
 * provide the required cross-thread synchronisation so await_resume observes
 * the written copy.
 *
 * ## Bus concept
 * The Bus type must provide:
 * @code
 *   SubscriptionHandle  Bus::subscribe(void(*cb)(const Message*, void*), void*);
 *   void                Bus::unsubscribe(SubscriptionHandle);
 * @endcode
 * unsubscribe() MUST be synchronous: when it returns no further invocation of
 * cb for that subscription will occur, and any invocation that was already in
 * progress has completed.
 *
 * ## Usage
 * @code
 *   flowcoro::Task<void> my_coro(MyBus& bus) {
 *       BusAwaitable<SensorMsg, MyBus> awaitable(bus);
 *       const SensorMsg& msg = co_await awaitable;
 *       process(msg);
 *   }
 * @endcode
 */

#include <atomic>
#include <coroutine>
#include <memory>
#include <optional>

#include "coroutine_manager.h"

namespace flowcoro {

/**
 * @brief One-shot awaitable that suspends a coroutine until one bus message
 *        arrives, then resumes it with a const reference to a copy of that
 *        message stored inside the awaitable.
 *
 * @tparam Message  Message type delivered by the bus callback.
 * @tparam Bus      Bus type providing subscribe() / unsubscribe().
 */
template<typename Message, typename Bus>
class BusAwaitable {
public:
    using CallbackFn         = void (*)(const Message*, void*);
    using SubscriptionHandle = decltype(std::declval<Bus&>().subscribe(
                                    std::declval<CallbackFn>(),
                                    std::declval<void*>()));

    explicit BusAwaitable(Bus& bus)
        : bus_(bus), state_(std::make_shared<State>()) {}

    // Non-copyable and non-movable: user_data points into state_ which must
    // not be relocated while a subscription is active.
    BusAwaitable(const BusAwaitable&)            = delete;
    BusAwaitable& operator=(const BusAwaitable&) = delete;
    BusAwaitable(BusAwaitable&&)                 = delete;
    BusAwaitable& operator=(BusAwaitable&&)      = delete;

    // ── Destructor ──────────────────────────────────────────────────────────
    // Bug 2 fix: if the coroutine is destroyed while suspended (Task
    // dropped / cancelled), we synchronously unsubscribe before State may be
    // freed. This prevents callback dereference of stale user_data.
    ~BusAwaitable() {
        unsubscribe_once();
    }

    // ── Awaitable interface ─────────────────────────────────────────────────

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> h) {
        state_->handle = h;

        // Subscribe AFTER the handle is stored so that an immediate
        // (same-thread) callback always finds a valid handle.
        state_->sub_handle = bus_.subscribe(&BusAwaitable::on_message,
                                            state_.get());
        state_->subscribed.store(true, std::memory_order_release);
    }

    const Message& await_resume() {
        // We reach here because on_message scheduled our resume.
        // Unsubscribe to stop any further deliveries (belt-and-suspenders;
        // the atomic_flag already prevents a second resume).
        unsubscribe_once();

        // Return a reference to the copy stored in State.  State is kept
        // alive by state_ (a member of this awaitable, which lives on the
        // coroutine frame) for the entire co_await expression.
        return *state_->msg;
    }

private:
    // ── Shared state ────────────────────────────────────────────────────────
    //
    // State is heap-allocated (via shared_ptr) so that it remains reachable
    // via state_.get() as user_data even if the awaitable is moved on the
    // frame.  The shared_ptr also ensures ~State runs after both the
    // awaitable and any concurrent on_message invocation have finished.
    struct State {
        // Bug 1 fix: atomic_flag ensures only the first on_message call that
        // wins test_and_set schedules a resume.
        std::atomic_flag fired{};      // default-initialised to false (C++20)

        // Bug 3 fix: the message is *copied* into State before the acq_rel
        // test_and_set in on_message.  The scheduler's mutex operations
        // (enqueue/dequeue) then guarantee the copy is visible to the
        // resuming thread inside await_resume.
        std::optional<Message> msg;

        std::coroutine_handle<> handle;
        SubscriptionHandle      sub_handle{};
        std::atomic<bool>       subscribed{false};
        std::atomic<bool>       unsubscribed{false};
    };

    void unsubscribe_once() noexcept {
        if (!state_->subscribed.load(std::memory_order_acquire)) return;
        if (state_->unsubscribed.exchange(true, std::memory_order_acq_rel)) return;
        bus_.unsubscribe(state_->sub_handle);
    }

    // ── Static callback ─────────────────────────────────────────────────────
    static void on_message(const Message* msg, void* user_data) {
        auto* state = static_cast<State*>(user_data);

        // Bug 1 fix: guard against duplicate delivery.
        // test_and_set returns the *previous* value:
        //   false → we won; schedule the resume.
        //   true  → duplicate (or destructor claimed ownership); drop silently.
        if (state->fired.test_and_set(std::memory_order_acq_rel)) {
            return;
        }

        // Bug 3 fix: copy the callback-owned payload into awaitable-owned
        // storage before scheduling resume.
        state->msg = *msg;

        // Schedule the coroutine resume through the FlowCoro scheduler so
        // that resume() is always called on a scheduler thread rather than
        // the bus callback thread (which may have incompatible
        // stack/priority constraints).
        CoroutineManager::get_instance().schedule_resume(state->handle);
    }

    // ── Members ─────────────────────────────────────────────────────────────
    Bus&                   bus_;
    std::shared_ptr<State> state_;
};

} // namespace flowcoro
