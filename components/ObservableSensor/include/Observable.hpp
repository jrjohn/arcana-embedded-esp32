/*
 * Observable Pattern Template for ESP32
 *
 * Modern C++ implementation of the Observer/Observable pattern
 * Thread-safe with FreeRTOS mutex protection
 */

#pragma once

#include <functional>
#include <vector>
#include <algorithm>
#include <memory>
#include <cstdint>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

namespace Arcana {

// Forward declarations
template<typename T, size_t QueueSize> class EventQueue;
template<typename T, size_t MaxSubscribers> class Observable;

/**
 * @brief Subscription handle for unsubscribing
 */
using SubscriptionId = uint32_t;

/**
 * @brief Observer callback type
 * @tparam T Event data type
 */
template<typename T>
using Observer = std::function<void(const T&)>;

/**
 * @brief Thread-safe Observable implementation
 *
 * Implements the Observable pattern with:
 * - Type-safe event data
 * - Thread-safe subscription management
 * - Automatic subscription cleanup via RAII
 * - Configurable maximum subscriber limit
 *
 * @tparam T Event data type
 * @tparam MaxSubscribers Maximum number of subscribers (0 = unlimited)
 */
template<typename T, size_t MaxSubscribers = 0>
class Observable {
public:
    static constexpr size_t kMaxSubscribers = MaxSubscribers;

    /**
     * @brief Default constructor - synchronous Notify()
     */
    Observable() : mNextId(1), mName(nullptr), mQueue(nullptr), mTaskHandle(nullptr) {
        mMutex = xSemaphoreCreateMutex();
        configASSERT(mMutex != nullptr);
    }

    /**
     * @brief Named constructor - creates FreeRTOS task + queue for async dispatch
     *
     * Each named Observable runs its own task that dequeues events
     * and dispatches to subscribers asynchronously.
     *
     * @param Name Task name (used for FreeRTOS task identification)
     * @param QueueDepth Number of events the queue can hold
     * @param StackSize Task stack size in bytes
     * @param Priority Task priority
     */
    explicit Observable(const char* Name, size_t QueueDepth = 20,
                        uint32_t StackSize = 2048, uint8_t Priority = 5)
        : mNextId(1), mName(Name), mQueue(nullptr), mTaskHandle(nullptr)
    {
        mMutex = xSemaphoreCreateMutex();
        configASSERT(mMutex != nullptr);

        mQueue = xQueueCreate(QueueDepth, sizeof(T));
        configASSERT(mQueue != nullptr);

        BaseType_t ret = xTaskCreate(AsyncTaskEntry, Name, StackSize, this, Priority, &mTaskHandle);
        configASSERT(ret == pdPASS);
    }

    ~Observable() {
        // Stop async task if running
        if (mTaskHandle) {
            vTaskDelete(mTaskHandle);
            mTaskHandle = nullptr;
        }
        if (mQueue) {
            vQueueDelete(mQueue);
            mQueue = nullptr;
        }
        if (mMutex) {
            vSemaphoreDelete(mMutex);
        }
    }

    // Non-copyable
    Observable(const Observable&) = delete;
    Observable& operator=(const Observable&) = delete;

    // Movable
    Observable(Observable&& Other) noexcept
        : mObservers(std::move(Other.mObservers))
        , mMutex(Other.mMutex)
        , mNextId(Other.mNextId) {
        Other.mMutex = nullptr;
    }

    Observable& operator=(Observable&& Other) noexcept {
        if (this != &Other) {
            if (mMutex) {
                vSemaphoreDelete(mMutex);
            }
            mObservers = std::move(Other.mObservers);
            mMutex = Other.mMutex;
            mNextId = Other.mNextId;
            Other.mMutex = nullptr;
        }
        return *this;
    }

    /**
     * @brief Subscribe to events
     *
     * @param ObserverCallback Callback function to receive events
     * @return SubscriptionId Handle for unsubscribing, 0 if limit reached
     */
    SubscriptionId Subscribe(Observer<T> ObserverCallback) {
        Lock lock(mMutex);

        // Check max subscriber limit (0 = unlimited)
        if constexpr (MaxSubscribers > 0) {
            if (mObservers.size() >= MaxSubscribers) {
                return 0;  // Subscription rejected
            }
        }

        SubscriptionId Id = mNextId++;
        mObservers.emplace_back(Id, std::move(ObserverCallback));
        return Id;
    }

    /**
     * @brief Check if subscription limit is reached
     */
    bool IsFull() const {
        if constexpr (MaxSubscribers == 0) {
            return false;  // Unlimited
        } else {
            Lock lock(mMutex);
            return mObservers.size() >= MaxSubscribers;
        }
    }

    /**
     * @brief Subscribe with lambda (convenience)
     */
    template<typename F>
    SubscriptionId operator+=(F&& ObserverCallback) {
        return Subscribe(std::forward<F>(ObserverCallback));
    }

    /**
     * @brief Unsubscribe by ID
     *
     * @param Id Subscription ID returned from Subscribe()
     * @return true if successfully unsubscribed
     */
    bool Unsubscribe(SubscriptionId Id) {
        Lock lock(mMutex);
        auto It = std::find_if(mObservers.begin(), mObservers.end(),
            [Id](const auto& Entry) { return Entry.first == Id; });

        if (It != mObservers.end()) {
            mObservers.erase(It);
            return true;
        }
        return false;
    }

    /**
     * @brief Check if there are any subscribers
     */
    bool HasSubscribers() const {
        Lock lock(mMutex);
        return !mObservers.empty();
    }

    /**
     * @brief Notify all observers with event data
     *
     * If this Observable was created with a name (task-backed), events are
     * posted to the queue and dispatched asynchronously by the internal task.
     * Otherwise, subscribers are called synchronously in the caller's context.
     *
     * @param Data Event data to broadcast
     */
    void Notify(const T& Data) {
        if (mQueue) {
            // Async mode: post to queue, task will dispatch
            if (xQueueSend(mQueue, &Data, 0) != pdTRUE) {
                ESP_LOGW("Observable", "Event dropped: queue full (%s)",
                         mName ? mName : "unnamed");
            }
            return;
        }

        // Synchronous mode: dispatch directly
        NotifySync(Data);
    }

    /**
     * @brief Notify with move semantics
     */
    void Notify(T&& Data) {
        Notify(static_cast<const T&>(Data));
    }

    /**
     * @brief Get the task name (nullptr if synchronous)
     */
    const char* GetName() const { return mName; }

    /**
     * @brief Check if this Observable is task-backed (async)
     */
    bool IsAsync() const { return mQueue != nullptr; }

    /**
     * @brief Get number of subscribers
     */
    size_t GetSubscriberCount() const {
        Lock lock(mMutex);
        return mObservers.size();
    }

    /**
     * @brief Remove all subscribers
     */
    void Clear() {
        Lock lock(mMutex);
        mObservers.clear();
    }

    // Note: For weak reference subscriptions, use the free function:
    //   SubscribeWeak(observable, weakPtr, callback)
    // See WeakObserver section below.

private:
    /**
     * @brief RAII mutex lock helper
     */
    class Lock {
    public:
        explicit Lock(SemaphoreHandle_t Mutex) : mMutex(Mutex) {
            if (mMutex) {
                xSemaphoreTake(mMutex, portMAX_DELAY);
            }
        }
        ~Lock() {
            if (mMutex) {
                xSemaphoreGive(mMutex);
            }
        }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    private:
        SemaphoreHandle_t mMutex;
    };

    /**
     * @brief Synchronous dispatch to all subscribers (used internally)
     */
    void NotifySync(const T& Data) {
        std::vector<Observer<T>> ObserversCopy;
        {
            Lock lock(mMutex);
            if (mObservers.empty()) return;

            ObserversCopy.reserve(mObservers.size());
            for (const auto& Entry : mObservers) {
                ObserversCopy.push_back(Entry.second);
            }
        }
        for (const auto& ObserverCallback : ObserversCopy) {
            if (ObserverCallback) {
                ObserverCallback(Data);
            }
        }
    }

public:
    /**
     * @brief Async task entry point. Public so tests can invoke it via the
     *        same call-site shape as xTaskCreate. Production code uses it
     *        only as the function pointer passed to xTaskCreate.
     */
    static void AsyncTaskEntry(void* Arg) {
        auto* Self = static_cast<Observable*>(Arg);
        Self->AsyncTaskLoop();
    }

    /**
     * @brief Process at most one queued async event. Public for tests so
     *        the loop body can be exercised without spawning a real task.
     */
    bool ProcessOneAsyncEvent() {
        if (!mQueue) return false;
        T Event;
        if (xQueueReceive(mQueue, &Event, portMAX_DELAY) == pdTRUE) {
            NotifySync(Event);
            return true;
        }
        return false;
    }

    /**
     * @brief Async task loop - dequeues events and dispatches to subscribers.
     *        Public so tests can drive it (the host xQueueReceive stub
     *        supports a longjmp escape via test_set_xqueue_escape_after).
     */
    void AsyncTaskLoop() {
        while (true) {
            ProcessOneAsyncEvent();
        }
    }

private:

    std::vector<std::pair<SubscriptionId, Observer<T>>> mObservers;
    mutable SemaphoreHandle_t mMutex;
    SubscriptionId mNextId;

    // Async task support (active when constructed with name)
    const char* mName;
    QueueHandle_t mQueue;
    TaskHandle_t mTaskHandle;
};

/**
 * @brief RAII Subscription guard - auto-unsubscribes on destruction
 *
 * @tparam T Event data type
 * @tparam MaxSubscribers Maximum subscribers limit (must match Observable)
 */
template<typename T, size_t MaxSubscribers = 0>
class Subscription {
public:
    using ObservableType = Observable<T, MaxSubscribers>;

    Subscription() : mObservable(nullptr), mId(0) {}

    Subscription(ObservableType& ObservableRef, SubscriptionId Id)
        : mObservable(&ObservableRef), mId(Id) {}

    ~Subscription() {
        Unsubscribe();
    }

    // Non-copyable
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    // Movable
    Subscription(Subscription&& Other) noexcept
        : mObservable(Other.mObservable), mId(Other.mId) {
        Other.mObservable = nullptr;
        Other.mId = 0;
    }

    Subscription& operator=(Subscription&& Other) noexcept {
        if (this != &Other) {
            Unsubscribe();
            mObservable = Other.mObservable;
            mId = Other.mId;
            Other.mObservable = nullptr;
            Other.mId = 0;
        }
        return *this;
    }

    void Unsubscribe() {
        if (mObservable && mId != 0) {
            mObservable->Unsubscribe(mId);
            mObservable = nullptr;
            mId = 0;
        }
    }

    bool IsActive() const { return mObservable != nullptr && mId != 0; }

private:
    ObservableType* mObservable;
    SubscriptionId mId;
};

/*******************************************************************************
 * Event Queue - Decouples notification from callback execution
 ******************************************************************************/

/**
 * @brief FreeRTOS-based Event Queue for async event dispatch
 *
 * Separates event production from consumption:
 * - Producer (sensor task) queues events without blocking
 * - Consumer (dedicated task) processes events asynchronously
 *
 * Benefits:
 * - Slow callbacks don't block producer
 * - Events processed in dedicated task context
 * - Configurable queue depth for burst handling
 *
 * @tparam T Event data type (must be copyable)
 * @tparam QueueSize Maximum queued events
 *
 * Usage:
 * @code
 *   EventQueue<SensorData, 16> Queue;
 *   Queue.Start([](const SensorData& Data) {
 *       // Process event in queue task
 *   });
 *
 *   // From producer task:
 *   Queue.Post(data);  // Non-blocking
 * @endcode
 */
template<typename T, size_t QueueSize = 16>
class EventQueue {
public:
    static constexpr size_t kQueueSize = QueueSize;
    static constexpr const char* kTag = "EventQueue";

    EventQueue()
        : mQueue(nullptr)
        , mTaskHandle(nullptr)
        , mRunning(false)
    {}

    ~EventQueue() {
        Stop();
    }

    // Non-copyable, non-movable
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;
    EventQueue(EventQueue&&) = delete;
    EventQueue& operator=(EventQueue&&) = delete;

    /**
     * @brief Start the event queue with a handler
     *
     * @param Handler Callback to process each event
     * @param StackSize Task stack size (default 4096)
     * @param Priority Task priority (default 5)
     * @return true if started successfully
     */
    bool Start(Observer<T> Handler, uint32_t StackSize = 4096, uint8_t Priority = 5) {
        if (mRunning) {
            return false;
        }

        mHandler = std::move(Handler);
        mQueue = xQueueCreate(QueueSize, sizeof(T));
        if (!mQueue) {
            ESP_LOGE(kTag, "Failed to create queue");
            return false;
        }

        mRunning = true;
        BaseType_t Ret = xTaskCreate(
            TaskEntry,
            "evt_queue",
            StackSize,
            this,
            Priority,
            &mTaskHandle
        );

        if (Ret != pdPASS) {
            mRunning = false;
            vQueueDelete(mQueue);
            mQueue = nullptr;
            ESP_LOGE(kTag, "Failed to create task");
            return false;
        }

        return true;
    }

    /**
     * @brief Stop the event queue
     */
    void Stop() {
        if (!mRunning) {
            return;
        }

        mRunning = false;

        // Wait for task to finish
        if (mTaskHandle) {
            // Send a dummy item to unblock the task
            T Dummy{};
            xQueueSend(mQueue, &Dummy, 0);

            // Give task time to exit
            vTaskDelay(pdMS_TO_TICKS(100));
            mTaskHandle = nullptr;
        }

        if (mQueue) {
            vQueueDelete(mQueue);
            mQueue = nullptr;
        }
    }

    /**
     * @brief Post an event to the queue (non-blocking)
     *
     * @param Data Event data to queue
     * @return true if queued successfully, false if queue full
     */
    bool Post(const T& Data) {
        if (!mQueue) {
            return false;
        }
        return xQueueSend(mQueue, &Data, 0) == pdTRUE;
    }

    /**
     * @brief Post with timeout
     *
     * @param Data Event data
     * @param TimeoutMs Max wait time in ms
     * @return true if queued within timeout
     */
    bool PostWait(const T& Data, uint32_t TimeoutMs) {
        if (!mQueue) {
            return false;
        }
        return xQueueSend(mQueue, &Data, pdMS_TO_TICKS(TimeoutMs)) == pdTRUE;
    }

    /**
     * @brief Check if queue is running
     */
    bool IsRunning() const { return mRunning; }

    /**
     * @brief Get number of pending events
     */
    size_t GetPendingCount() const {
        if (!mQueue) return 0;
        return uxQueueMessagesWaiting(mQueue);
    }

    /**
     * @brief Check if queue is full
     */
    bool IsFull() const {
        return GetPendingCount() >= QueueSize;
    }

public:
    /**
     * @brief Process at most one queued event. Public for tests so the
     *        loop body can be exercised without spawning a real task.
     */
    bool ProcessOneEvent() {
        if (!mQueue) return false;
        T Event;
        if (xQueueReceive(mQueue, &Event, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (mRunning && mHandler) {
                mHandler(Event);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Task entry — public so tests can invoke it via the same call
     *        shape as xTaskCreate.
     */
    static void TaskEntry(void* Arg) {
        auto* Self = static_cast<EventQueue*>(Arg);
        Self->TaskLoop();
        vTaskDelete(nullptr);
    }

    /**
     * @brief Task loop — public so tests can drive it (the host
     *        xQueueReceive stub supports a longjmp escape).
     */
    void TaskLoop() {
        while (mRunning) {
            ProcessOneEvent();
        }
    }

private:

    QueueHandle_t mQueue;
    TaskHandle_t mTaskHandle;
    Observer<T> mHandler;
    volatile bool mRunning;
};

/*******************************************************************************
 * Weak Reference Subscription Support
 ******************************************************************************/

/**
 * @brief Weak subscription holder for preventing circular references
 *
 * Wraps a callback with weak_ptr to automatically skip
 * invocation if the referenced object has been destroyed.
 *
 * Usage:
 * @code
 *   class MyHandler : public std::enable_shared_from_this<MyHandler> {
 *   public:
 *       void Setup(Observable<SensorData>& Obs) {
 *           // Weak subscription - auto-invalidates when MyHandler is destroyed
 *           mSubId = Obs.SubscribeWeak(weak_from_this(),
 *               [](MyHandler& Self, const SensorData& Data) {
 *                   Self.HandleData(Data);
 *               });
 *       }
 *   private:
 *       SubscriptionId mSubId;
 *   };
 * @endcode
 */
template<typename T, typename Owner>
class WeakObserver {
public:
    using Callback = std::function<void(Owner&, const T&)>;

    WeakObserver(std::weak_ptr<Owner> WeakOwner, Callback Cb)
        : mWeakOwner(std::move(WeakOwner))
        , mCallback(std::move(Cb))
    {}

    void operator()(const T& Data) const {
        if (auto Shared = mWeakOwner.lock()) {
            if (mCallback) {
                mCallback(*Shared, Data);
            }
        }
        // If weak_ptr expired, silently skip (object destroyed)
    }

    bool IsExpired() const {
        return mWeakOwner.expired();
    }

private:
    std::weak_ptr<Owner> mWeakOwner;
    Callback mCallback;
};

/**
 * @brief Helper to create weak observer
 */
template<typename T, typename Owner>
Observer<T> MakeWeakObserver(
    std::weak_ptr<Owner> WeakOwner,
    typename WeakObserver<T, Owner>::Callback Cb
) {
    return WeakObserver<T, Owner>(std::move(WeakOwner), std::move(Cb));
}

/*******************************************************************************
 * Observable Extensions for Weak References
 ******************************************************************************/

// Note: SubscribeWeak is added as a free function to avoid modifying Observable template

/**
 * @brief Subscribe with weak reference (free function)
 *
 * @tparam T Event type
 * @tparam Owner Owner type (must inherit enable_shared_from_this)
 * @tparam MaxSubs Max subscribers (default 0)
 * @param Obs Observable to subscribe to
 * @param WeakOwner Weak pointer to owner
 * @param Cb Callback receiving (Owner&, const T&)
 * @return SubscriptionId
 */
template<typename T, typename Owner, size_t MaxSubs = 0>
SubscriptionId SubscribeWeak(
    Observable<T, MaxSubs>& Obs,
    std::weak_ptr<Owner> WeakOwner,
    typename WeakObserver<T, Owner>::Callback Cb
) {
    return Obs.Subscribe(MakeWeakObserver<T>(std::move(WeakOwner), std::move(Cb)));
}

/**
 * @brief Subject base class with multiple event types
 *
 * Convenience wrapper for components that emit multiple event types.
 * Uses unlimited subscribers (MaxSubscribers = 0) for all event types.
 */
template<typename... Events>
class Subject : public Observable<Events, 0>... {
public:
    template<typename E>
    SubscriptionId On(Observer<E> ObserverCallback) {
        return Observable<E, 0>::Subscribe(std::move(ObserverCallback));
    }

    template<typename E>
    void Emit(const E& Event) {
        Observable<E, 0>::Notify(Event);
    }

    template<typename E>
    bool HasSubscribers() const {
        return Observable<E, 0>::HasSubscribers();
    }
};

/*******************************************************************************
 * StaticObservable - Zero Heap Allocation Version
 ******************************************************************************/

/**
 * @brief Callback wrapper for static storage
 *
 * Uses function pointer + context instead of std::function
 * to avoid heap allocation.
 */
template<typename T>
struct StaticCallback {
    using FnPtr = void(*)(void* Context, const T& Data);

    FnPtr Function = nullptr;
    void* Context = nullptr;
    SubscriptionId Id = 0;

    bool IsValid() const { return Function != nullptr && Id != 0; }

    void Invoke(const T& Data) const {
        if (Function) {
            Function(Context, Data);
        }
    }
};

/**
 * @brief Zero-heap-allocation Observable for memory-constrained systems
 *
 * Uses fixed-size array instead of std::vector, and function pointers
 * instead of std::function. All storage is inline (stack/static).
 *
 * Trade-offs vs Observable<T>:
 * - ✅ Zero heap allocation
 * - ✅ Deterministic memory usage
 * - ✅ No fragmentation
 * - ❌ Fixed max subscribers (compile-time)
 * - ❌ No lambda captures (use Context pointer instead)
 * - ❌ Slightly more verbose subscription
 *
 * @tparam T Event data type
 * @tparam MaxSubscribers Maximum number of subscribers (required)
 *
 * Usage:
 * @code
 *   StaticObservable<SensorData, 4> observable;
 *
 *   // Subscribe with context
 *   struct MyContext { int id; };
 *   MyContext ctx{42};
 *
 *   auto id = observable.Subscribe(
 *       [](void* ctx, const SensorData& data) {
 *           auto* myCtx = static_cast<MyContext*>(ctx);
 *           printf("Sensor %d: %d\n", myCtx->id, data.Value);
 *       },
 *       &ctx
 *   );
 *
 *   // Or without context
 *   observable.Subscribe(
 *       [](void*, const SensorData& data) {
 *           printf("Value: %d\n", data.Value);
 *       }
 *   );
 *
 *   observable.Notify(data);
 * @endcode
 */
template<typename T, size_t MaxSubscribers>
class StaticObservable {
public:
    static_assert(MaxSubscribers > 0, "MaxSubscribers must be > 0 for StaticObservable");
    static constexpr size_t kMaxSubscribers = MaxSubscribers;

    using CallbackFn = typename StaticCallback<T>::FnPtr;

    StaticObservable() : mNextId(1), mCount(0) {
        mMutex = xSemaphoreCreateMutex();
        configASSERT(mMutex != nullptr);
    }

    ~StaticObservable() {
        if (mMutex) {
            vSemaphoreDelete(mMutex);
        }
    }

    // Non-copyable, non-movable (contains mutex)
    StaticObservable(const StaticObservable&) = delete;
    StaticObservable& operator=(const StaticObservable&) = delete;
    StaticObservable(StaticObservable&&) = delete;
    StaticObservable& operator=(StaticObservable&&) = delete;

    /**
     * @brief Subscribe with callback and optional context
     *
     * @param Fn Function pointer: void(*)(void* ctx, const T& data)
     * @param Context Optional context pointer (passed to callback)
     * @return SubscriptionId, or 0 if full
     */
    SubscriptionId Subscribe(CallbackFn Fn, void* Context = nullptr) {
        if (!Fn) return 0;

        Lock lock(mMutex);

        if (mCount >= MaxSubscribers) {
            return 0;  // Full
        }

        // Find empty slot
        for (size_t i = 0; i < MaxSubscribers; ++i) {
            if (!mCallbacks[i].IsValid()) {
                SubscriptionId Id = mNextId++;
                mCallbacks[i] = {Fn, Context, Id};
                ++mCount;
                return Id;
            }
        }

        return 0;  // Should not reach here
    }

    /**
     * @brief Unsubscribe by ID
     */
    bool Unsubscribe(SubscriptionId Id) {
        if (Id == 0) return false;

        Lock lock(mMutex);

        for (size_t i = 0; i < MaxSubscribers; ++i) {
            if (mCallbacks[i].Id == Id) {
                mCallbacks[i] = {};
                --mCount;
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Notify all subscribers
     */
    void Notify(const T& Data) {
        // Copy to local array to allow modifications during iteration
        StaticCallback<T> LocalCallbacks[MaxSubscribers];
        size_t LocalCount = 0;

        {
            Lock lock(mMutex);
            if (mCount == 0) return;  // Early exit

            for (size_t i = 0; i < MaxSubscribers; ++i) {
                if (mCallbacks[i].IsValid()) {
                    LocalCallbacks[LocalCount++] = mCallbacks[i];
                }
            }
        }

        // Invoke outside lock
        for (size_t i = 0; i < LocalCount; ++i) {
            LocalCallbacks[i].Invoke(Data);
        }
    }

    /**
     * @brief Check if there are subscribers
     */
    bool HasSubscribers() const {
        Lock lock(mMutex);
        return mCount > 0;
    }

    /**
     * @brief Get subscriber count
     */
    size_t GetSubscriberCount() const {
        Lock lock(mMutex);
        return mCount;
    }

    /**
     * @brief Check if full
     */
    bool IsFull() const {
        Lock lock(mMutex);
        return mCount >= MaxSubscribers;
    }

    /**
     * @brief Remove all subscribers
     */
    void Clear() {
        Lock lock(mMutex);
        for (size_t i = 0; i < MaxSubscribers; ++i) {
            mCallbacks[i] = {};
        }
        mCount = 0;
    }

private:
    class Lock {
    public:
        explicit Lock(SemaphoreHandle_t Mutex) : mMutex(Mutex) {
            if (mMutex) xSemaphoreTake(mMutex, portMAX_DELAY);
        }
        ~Lock() {
            if (mMutex) xSemaphoreGive(mMutex);
        }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    private:
        SemaphoreHandle_t mMutex;
    };

    StaticCallback<T> mCallbacks[MaxSubscribers];
    mutable SemaphoreHandle_t mMutex;
    SubscriptionId mNextId;
    size_t mCount;
};

/**
 * @brief RAII guard for StaticObservable subscriptions
 */
template<typename T, size_t MaxSubscribers>
class StaticSubscription {
public:
    using ObservableType = StaticObservable<T, MaxSubscribers>;

    StaticSubscription() : mObservable(nullptr), mId(0) {}

    StaticSubscription(ObservableType& Obs, SubscriptionId Id)
        : mObservable(&Obs), mId(Id) {}

    ~StaticSubscription() { Unsubscribe(); }

    // Non-copyable
    StaticSubscription(const StaticSubscription&) = delete;
    StaticSubscription& operator=(const StaticSubscription&) = delete;

    // Movable
    StaticSubscription(StaticSubscription&& Other) noexcept
        : mObservable(Other.mObservable), mId(Other.mId) {
        Other.mObservable = nullptr;
        Other.mId = 0;
    }

    StaticSubscription& operator=(StaticSubscription&& Other) noexcept {
        if (this != &Other) {
            Unsubscribe();
            mObservable = Other.mObservable;
            mId = Other.mId;
            Other.mObservable = nullptr;
            Other.mId = 0;
        }
        return *this;
    }

    void Unsubscribe() {
        if (mObservable && mId != 0) {
            mObservable->Unsubscribe(mId);
            mObservable = nullptr;
            mId = 0;
        }
    }

    bool IsActive() const { return mObservable != nullptr && mId != 0; }

private:
    ObservableType* mObservable;
    SubscriptionId mId;
};

} // namespace Arcana
