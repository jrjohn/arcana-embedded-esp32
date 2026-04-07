#include <gtest/gtest.h>
#include "Observable.hpp"
#include <cstdint>

using namespace Arcana;

struct TestEvent {
    int value;
};

// ── Sync Observable basic tests ─────────────────────────────────────────────

TEST(ObservableTest, DefaultConstructorIsSynchronous) {
    Observable<TestEvent> obs;
    EXPECT_FALSE(obs.IsAsync());
    EXPECT_EQ(obs.GetName(), nullptr);
}

TEST(ObservableTest, NoSubscribersInitially) {
    Observable<TestEvent> obs;
    EXPECT_FALSE(obs.HasSubscribers());
    EXPECT_EQ(obs.GetSubscriberCount(), 0u);
}

TEST(ObservableTest, SubscribeReturnsValidId) {
    Observable<TestEvent> obs;
    auto id = obs.Subscribe([](const TestEvent&) {});
    EXPECT_GT(id, 0u);
    EXPECT_TRUE(obs.HasSubscribers());
    EXPECT_EQ(obs.GetSubscriberCount(), 1u);
}

TEST(ObservableTest, NotifyDispatchesToSubscriber) {
    Observable<TestEvent> obs;
    int received = 0;
    obs.Subscribe([&received](const TestEvent& e) { received = e.value; });

    obs.Notify(TestEvent{42});
    EXPECT_EQ(received, 42);
}

TEST(ObservableTest, NotifyDispatchesToMultipleSubscribers) {
    Observable<TestEvent> obs;
    int a = 0, b = 0, c = 0;
    obs.Subscribe([&a](const TestEvent& e) { a = e.value; });
    obs.Subscribe([&b](const TestEvent& e) { b = e.value * 2; });
    obs.Subscribe([&c](const TestEvent& e) { c = e.value * 3; });

    obs.Notify(TestEvent{10});
    EXPECT_EQ(a, 10);
    EXPECT_EQ(b, 20);
    EXPECT_EQ(c, 30);
}

TEST(ObservableTest, UnsubscribeRemovesSubscriber) {
    Observable<TestEvent> obs;
    int counter = 0;
    auto id = obs.Subscribe([&counter](const TestEvent&) { counter++; });

    obs.Notify(TestEvent{1});
    EXPECT_EQ(counter, 1);

    EXPECT_TRUE(obs.Unsubscribe(id));
    EXPECT_EQ(obs.GetSubscriberCount(), 0u);

    obs.Notify(TestEvent{2});
    EXPECT_EQ(counter, 1);  // unchanged
}

TEST(ObservableTest, UnsubscribeInvalidIdReturnsFalse) {
    Observable<TestEvent> obs;
    obs.Subscribe([](const TestEvent&) {});
    EXPECT_FALSE(obs.Unsubscribe(999));
}

TEST(ObservableTest, ClearRemovesAllSubscribers) {
    Observable<TestEvent> obs;
    obs.Subscribe([](const TestEvent&) {});
    obs.Subscribe([](const TestEvent&) {});
    obs.Subscribe([](const TestEvent&) {});
    EXPECT_EQ(obs.GetSubscriberCount(), 3u);

    obs.Clear();
    EXPECT_EQ(obs.GetSubscriberCount(), 0u);
    EXPECT_FALSE(obs.HasSubscribers());
}

TEST(ObservableTest, OperatorPlusEqualsSubscribesLambda) {
    Observable<TestEvent> obs;
    int received = 0;
    obs += [&received](const TestEvent& e) { received = e.value; };

    obs.Notify(TestEvent{99});
    EXPECT_EQ(received, 99);
}

// ── Bounded Observable tests ────────────────────────────────────────────────

TEST(ObservableTest, BoundedObservableEnforcesLimit) {
    Observable<TestEvent, 2> obs;  // Max 2 subscribers
    auto id1 = obs.Subscribe([](const TestEvent&) {});
    auto id2 = obs.Subscribe([](const TestEvent&) {});
    auto id3 = obs.Subscribe([](const TestEvent&) {});

    EXPECT_GT(id1, 0u);
    EXPECT_GT(id2, 0u);
    EXPECT_EQ(id3, 0u);  // Rejected
    EXPECT_EQ(obs.GetSubscriberCount(), 2u);
}

TEST(ObservableTest, BoundedObservableIsFullReturnsTrue) {
    Observable<TestEvent, 1> obs;
    EXPECT_FALSE(obs.IsFull());
    obs.Subscribe([](const TestEvent&) {});
    EXPECT_TRUE(obs.IsFull());
}

TEST(ObservableTest, UnboundedObservableIsNeverFull) {
    Observable<TestEvent> obs;
    EXPECT_FALSE(obs.IsFull());
    for (int i = 0; i < 10; i++) {
        obs.Subscribe([](const TestEvent&) {});
    }
    EXPECT_FALSE(obs.IsFull());
}

// ── Notify with no subscribers (no-op) ──────────────────────────────────────

TEST(ObservableTest, NotifyNoSubscribersIsNoOp) {
    Observable<TestEvent> obs;
    // Should not crash
    obs.Notify(TestEvent{123});
    SUCCEED();
}

// ── Async (named) Observable construction + destruction ───────────────────
//
// The named ctor creates a FreeRTOS queue + task. Our esp_stubs xTaskCreate
// returns success without actually running the task body, but the queue is a
// real fake (std::vector backed). Notify() goes through the async path and
// pushes to the queue; ~Observable() tears down the queue + task handle.

TEST(ObservableTest, AsyncObservableConstructsAndDestructs) {
    Observable<TestEvent> obs("AsyncTest");
    EXPECT_TRUE(obs.IsAsync());
    EXPECT_NE(obs.GetName(), nullptr);
}

TEST(ObservableTest, AsyncObservableNotifyEnqueues) {
    Observable<TestEvent> obs("AsyncEnqueue", /*queueDepth=*/10);
    // Should push into the fake queue without crashing
    obs.Notify(TestEvent{1});
    obs.Notify(TestEvent{2});
    obs.Notify(TestEvent{3});
    SUCCEED();
}

TEST(ObservableTest, AsyncObservableQueueOverflowDrops) {
    // Tiny queue depth so we can fill it
    Observable<TestEvent> obs("AsyncOverflow", /*queueDepth=*/3);
    for (int i = 0; i < 100; i++) {
        obs.Notify(TestEvent{i});  // queue fills, excess dropped (no crash)
    }
    SUCCEED();
}

// ── Subscription RAII guard ────────────────────────────────────────────────

TEST(ObservableTest, SubscriptionAutoUnsubscribesOnScopeExit) {
    Observable<TestEvent> obs;
    auto id = obs.Subscribe([](const TestEvent&) {});
    EXPECT_EQ(obs.GetSubscriberCount(), 1u);
    {
        Subscription<TestEvent> sub(obs, id);
        EXPECT_TRUE(sub.IsActive());
    }
    // sub destructor calls Unsubscribe → count drops
    EXPECT_EQ(obs.GetSubscriberCount(), 0u);
}

TEST(ObservableTest, SubscriptionMoveTransfersOwnership) {
    Observable<TestEvent> obs;
    auto id = obs.Subscribe([](const TestEvent&) {});
    Subscription<TestEvent> sub1(obs, id);
    Subscription<TestEvent> sub2(std::move(sub1));
    EXPECT_FALSE(sub1.IsActive());
    EXPECT_TRUE(sub2.IsActive());
    EXPECT_EQ(obs.GetSubscriberCount(), 1u);
}

TEST(ObservableTest, DefaultSubscriptionIsInactive) {
    Subscription<TestEvent> sub;
    EXPECT_FALSE(sub.IsActive());
    sub.Unsubscribe();  // safe no-op
    SUCCEED();
}

// ── ProcessOneAsyncEvent: drives the AsyncTaskLoop body without a real task

TEST(ObservableTest, ProcessOneAsyncEventDispatchesQueuedEvents) {
    Observable<TestEvent> obs("AsyncProcessOne", /*queueDepth=*/8);
    int count = 0;
    int lastValue = 0;
    obs.Subscribe([&](const TestEvent& e) {
        count++;
        lastValue = e.value;
    });

    obs.Notify(TestEvent{11});
    obs.Notify(TestEvent{22});
    obs.Notify(TestEvent{33});

    // Drain via the test-public hook
    while (obs.ProcessOneAsyncEvent()) {}

    EXPECT_EQ(count, 3);
    EXPECT_EQ(lastValue, 33);
}

TEST(ObservableTest, ProcessOneAsyncEventOnEmptyQueueReturnsFalse) {
    Observable<TestEvent> obs("AsyncEmpty");
    EXPECT_FALSE(obs.ProcessOneAsyncEvent());
}

TEST(ObservableTest, ProcessOneAsyncEventOnSyncObservableReturnsFalse) {
    Observable<TestEvent> obs;  // sync, no queue
    EXPECT_FALSE(obs.ProcessOneAsyncEvent());
}

// ── EventQueue::ProcessOneEvent — drives the TaskLoop body ─────────────────

TEST(EventQueueTest, ProcessOneEventDispatchesQueuedItem) {
    EventQueue<int, 8> eq;
    int sum = 0;
    ASSERT_TRUE(eq.Start([&](int v) { sum += v; }, 4096, 5));

    eq.Post(10);
    eq.Post(20);
    eq.Post(30);

    while (eq.ProcessOneEvent()) {}

    EXPECT_EQ(sum, 60);
    eq.Stop();
}

TEST(EventQueueTest, ProcessOneEventOnEmptyQueueReturnsFalse) {
    EventQueue<int, 4> eq;
    eq.Start([](int){}, 4096, 5);
    EXPECT_FALSE(eq.ProcessOneEvent());
    eq.Stop();
}

TEST(EventQueueTest, ProcessOneEventBeforeStartReturnsFalse) {
    EventQueue<int, 4> eq;
    EXPECT_FALSE(eq.ProcessOneEvent());
}

TEST(EventQueueTest, IsRunningAndPendingCount) {
    EventQueue<int, 4> eq;
    EXPECT_FALSE(eq.IsRunning());
    eq.Start([](int){}, 4096, 5);
    EXPECT_TRUE(eq.IsRunning());
    eq.Post(1);
    eq.Post(2);
    EXPECT_EQ(eq.GetPendingCount(), 2u);
    eq.Stop();
    EXPECT_FALSE(eq.IsRunning());
}
