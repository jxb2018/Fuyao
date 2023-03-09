#ifndef LUMINE_PROCFS_H
#define LUMINE_PROCFS_H

#include "base/common.h"

namespace faas::procfs_utils {

    struct ThreadStat {
        int64_t timestamp;      // in ns
        int32_t cpu_stat_user;  // in tick, from /proc/[tid]/stat utime
        int32_t cpu_stat_sys;   // in tick, from /proc/[tid]/stat stime
        int32_t voluntary_ctxt_switches;     // from /proc/[tid]/status
        int32_t nonvoluntary_ctxt_switches;  // from /proc/[tid]/status
    };

    bool ReadThreadStat(int tid, ThreadStat *stat);

}  // namespace faas

#endif //LUMINE_PROCFS_H