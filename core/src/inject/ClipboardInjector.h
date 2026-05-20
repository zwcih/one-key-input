#pragma once
#include "IInjector.h"

namespace onekey::inject {

class ClipboardInjector : public IInjector {
public:
    InjectResult InjectChunk(std::wstring_view text, const InjectTarget& tgt) override;
};

}  // namespace onekey::inject
