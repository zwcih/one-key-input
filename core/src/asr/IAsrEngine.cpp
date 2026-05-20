#include "IAsrEngine.h"

namespace onekey::asr {

const char* AsrEventKindName(AsrEventKind k) {
    switch (k) {
        case AsrEventKind::Partial:      return "Partial";
        case AsrEventKind::SegmentFinal: return "SegmentFinal";
        case AsrEventKind::SessionFinal: return "SessionFinal";
        case AsrEventKind::Error:        return "Error";
    }
    return "?";
}

}  // namespace onekey::asr
