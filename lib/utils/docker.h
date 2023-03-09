#ifndef LUMINE_DOCKER_H
#define LUMINE_DOCKER_H


#include "base/common.h"

namespace faas::docker_utils {

// cgroup_fs root by default is /sys/fs/cgroup
    void SetCgroupFsRoot(std::string_view path);

    constexpr size_t kContainerIdLength = 64;
    extern const std::string kInvalidContainerId;

// Get container ID of the running process
// Will return kInvalidContainerId if failed
    std::string GetSelfContainerId();

    struct ContainerStat {
        int64_t timestamp;      // in ns
        int64_t cpu_usage;      // in ns, from cpuacct.usage
        int32_t cpu_stat_user;  // in tick, from cpuacct.stat
        int32_t cpu_stat_sys;   // in tick, from cpuacct.stat
    };

    bool ReadContainerStat(std::string_view container_id, ContainerStat *stat);

}  // namespace faas
#endif //LUMINE_DOCKER_H