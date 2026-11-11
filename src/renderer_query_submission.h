#pragma once
#include <cstdint>
#include <optional>
namespace outrun {
// Limit extra submissions without restricting retries after failed submission.
struct QuerySubmissionBudget {
  std::optional<uint64_t> submitted_frame;
  template<class Submit>
  bool Try(uint64_t frame,bool safe,Submit&& submit) {
    if(!safe || (submitted_frame && *submitted_frame==frame))return false;
    if(!submit())return false;
    submitted_frame=frame;
    return true;
  }
};
}
