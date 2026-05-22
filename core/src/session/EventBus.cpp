#include "EventBus.h"

#include <algorithm>

namespace onekey::session {

int EventBus::Subscribe(Callback cb) {
    std::lock_guard<std::mutex> lk(mu_);
    int t = next_token_++;
    subs_.emplace_back(t, std::move(cb));
    return t;
}

void EventBus::Unsubscribe(int token) {
    std::lock_guard<std::mutex> lk(mu_);
    subs_.erase(std::remove_if(subs_.begin(), subs_.end(),
                               [token](const auto& p){ return p.first == token; }),
                subs_.end());
}

void EventBus::Publish(const PhaseEvent& ev) {
    std::vector<Callback> snapshot;
    {
        std::lock_guard<std::mutex> lk(mu_);
        snapshot.reserve(subs_.size());
        for (auto& [_, cb] : subs_) snapshot.push_back(cb);
    }
    for (auto& cb : snapshot) cb(ev);
}

const char* PhaseName(Phase p) {
    switch (p) {
        case Phase::Idle:            return "Idle";
        case Phase::Recording:       return "Recording";
        case Phase::StickyRecording: return "StickyRecording";
        case Phase::Recognizing:     return "Recognizing";
        case Phase::Polishing:       return "Polishing";
        case Phase::Injecting:       return "Injecting";
        case Phase::Done:            return "Done";
        case Phase::Error:           return "Error";
    }
    return "?";
}

}  // namespace onekey::session
