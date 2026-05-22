#include <gtest/gtest.h>

#include "session/EventBus.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

using namespace onekey::session;

TEST(EventBus, SubscribeReceivesPublishedEvent) {
    EventBus bus;
    Phase got = Phase::Idle;
    int token = bus.Subscribe([&](const PhaseEvent& ev){ got = ev.phase; });
    EXPECT_GT(token, 0);

    bus.Publish(PhaseEvent{Phase::Recording, L"go"});
    EXPECT_EQ(got, Phase::Recording);
}

TEST(EventBus, MultipleSubscribersAllReceive) {
    EventBus bus;
    int a = 0, b = 0;
    bus.Subscribe([&](const PhaseEvent&){ ++a; });
    bus.Subscribe([&](const PhaseEvent&){ ++b; });

    bus.Publish(PhaseEvent{Phase::Polishing, {}});
    bus.Publish(PhaseEvent{Phase::Polishing, {}});

    EXPECT_EQ(a, 2);
    EXPECT_EQ(b, 2);
}

TEST(EventBus, UnsubscribedCallbackNotInvoked) {
    EventBus bus;
    int hits = 0;
    int t = bus.Subscribe([&](const PhaseEvent&){ ++hits; });
    bus.Publish(PhaseEvent{Phase::Idle, {}});
    EXPECT_EQ(hits, 1);

    bus.Unsubscribe(t);
    bus.Publish(PhaseEvent{Phase::Idle, {}});
    EXPECT_EQ(hits, 1);
}

TEST(EventBus, UnsubscribeUnknownTokenIsNoop) {
    EventBus bus;
    EXPECT_NO_THROW(bus.Unsubscribe(9999));
    EXPECT_NO_THROW(bus.Unsubscribe(-1));
}

TEST(EventBus, TokensAreUnique) {
    EventBus bus;
    auto t1 = bus.Subscribe([](const PhaseEvent&){});
    auto t2 = bus.Subscribe([](const PhaseEvent&){});
    auto t3 = bus.Subscribe([](const PhaseEvent&){});
    EXPECT_NE(t1, t2);
    EXPECT_NE(t2, t3);
    EXPECT_NE(t1, t3);
}

TEST(EventBus, DetailPropagates) {
    EventBus bus;
    std::wstring got;
    bus.Subscribe([&](const PhaseEvent& ev){ got = ev.detail; });
    bus.Publish(PhaseEvent{Phase::Recognizing, L"partial text"});
    EXPECT_EQ(got, L"partial text");
}

TEST(EventBus, ConcurrentPublishIsSafe) {
    // Sanity: many threads publishing while a subscriber counts. We only
    // check we don't crash / data-race trip TSAN; exact count == thread×N.
    EventBus bus;
    std::atomic<int> count{0};
    bus.Subscribe([&](const PhaseEvent&){ count.fetch_add(1, std::memory_order_relaxed); });

    constexpr int kThreads = 4;
    constexpr int kPerThread = 200;
    std::vector<std::thread> ts;
    ts.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        ts.emplace_back([&]{
            for (int j = 0; j < kPerThread; ++j) {
                bus.Publish(PhaseEvent{Phase::Injecting, {}});
            }
        });
    }
    for (auto& t : ts) t.join();
    EXPECT_EQ(count.load(), kThreads * kPerThread);
}

TEST(PhaseName, AllPhasesNamed) {
    EXPECT_STREQ(PhaseName(Phase::Idle),             "Idle");
    EXPECT_STREQ(PhaseName(Phase::Recording),        "Recording");
    EXPECT_STREQ(PhaseName(Phase::StickyRecording),  "StickyRecording");
    EXPECT_STREQ(PhaseName(Phase::Recognizing),      "Recognizing");
    EXPECT_STREQ(PhaseName(Phase::Polishing),        "Polishing");
    EXPECT_STREQ(PhaseName(Phase::Injecting),        "Injecting");
    EXPECT_STREQ(PhaseName(Phase::Done),             "Done");
    EXPECT_STREQ(PhaseName(Phase::Error),            "Error");
}
