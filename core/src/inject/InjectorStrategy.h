#pragma once
#include "IInjector.h"
#include "../config/Config.h"
#include <memory>
#include <vector>

namespace onekey::inject {

// Picks an injector based on config + per-call target, with automatic
// fallback when the primary returns FullFail.
//
// Slice 2:
//   mode="sendinput"  -> primary: SendInputUnicode, fallback: Clipboard
//   mode="clipboard"  -> Clipboard only
//   mode="auto"       -> same as "sendinput" (default for new installs)
class InjectorStrategy {
public:
    explicit InjectorStrategy(const config::InjectConfig& cfg);
    ~InjectorStrategy();

    InjectResult InjectChunk(std::wstring_view text, const InjectTarget& tgt);
    void Commit(const InjectTarget& tgt);

    // For logging / diagnostics.
    const std::string& PrimaryName() const { return primary_name_; }

private:
    std::unique_ptr<IInjector>              primary_;
    std::unique_ptr<IInjector>              fallback_;
    std::string                             primary_name_;
    std::string                             fallback_name_;
};

}  // namespace onekey::inject
