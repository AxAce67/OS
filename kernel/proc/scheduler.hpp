#pragma once

#include <stdint.h>
#include "proc/process.hpp"

namespace scheduler {

bool IsAutoScheduleEnabled();
void SetAutoScheduleEnabled(bool enabled);
bool DequeueAutoScheduledProcessForTick(uint64_t now_tick, proc::Info* out_info);

}  // namespace scheduler
