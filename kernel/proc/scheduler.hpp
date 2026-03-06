#pragma once

#include <stdint.h>
#include "proc/process.hpp"

namespace scheduler {

bool IsAutoScheduleEnabled();
void SetAutoScheduleEnabled(bool enabled);
bool RunAutoScheduledProcess(proc::BootFileLookup lookup, proc::Info* out_info, int64_t* out_wait_status);

}  // namespace scheduler
