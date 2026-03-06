#pragma once

#include <stdint.h>
#include "proc/process.hpp"

namespace scheduler {

bool IsAutoScheduleEnabled();
void SetAutoScheduleEnabled(bool enabled);
bool GetNextAutoScheduledProcess(proc::Info* out_info);

}  // namespace scheduler
