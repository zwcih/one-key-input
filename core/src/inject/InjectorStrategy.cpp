#include "InjectorStrategy.h"
#include "ClipboardInjector.h"
#include "SendInputUnicodeInjector.h"

#include <spdlog/spdlog.h>
#include <stdexcept>

namespace onekey::inject {

InjectorStrategy::InjectorStrategy(const config::InjectConfig& cfg) {
    const std::string& m = cfg.mode;
    if (m == "clipboard") {
        primary_       = std::make_unique<ClipboardInjector>();
        primary_name_  = "clipboard";
    } else if (m == "sendinput" || m == "auto" || m.empty()) {
        primary_       = std::make_unique<SendInputUnicodeInjector>();
        primary_name_  = "sendinput";
        fallback_      = std::make_unique<ClipboardInjector>();
        fallback_name_ = "clipboard";
    } else {
        throw std::runtime_error("unknown inject mode: " + m);
    }
    spdlog::info("[inject.strategy] primary={} fallback={}",
                 primary_name_,
                 fallback_name_.empty() ? "(none)" : fallback_name_);
}

InjectorStrategy::~InjectorStrategy() = default;

InjectResult InjectorStrategy::InjectChunk(std::wstring_view text, const InjectTarget& tgt) {
    auto rc = primary_->InjectChunk(text, tgt);
    if (rc == InjectResult::FullFail && fallback_) {
        spdlog::warn("[inject.strategy] primary={} failed, falling back to {}",
                     primary_name_, fallback_name_);
        rc = fallback_->InjectChunk(text, tgt);
    }
    return rc;
}

void InjectorStrategy::Commit(const InjectTarget& tgt) {
    primary_->Commit(tgt);
}

}  // namespace onekey::inject
