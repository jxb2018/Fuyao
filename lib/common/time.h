#ifndef LUMINE_TIME_H
#define LUMINE_TIME_H

#include "base/common.h"
#include "base/logging.h"

namespace faas {

    inline int64_t TimeSpecToMicro(struct timespec *tp) {
        int64_t ret = 0;
        ret += int64_t{tp->tv_sec} * 1000000;
        ret += int64_t{tp->tv_nsec} / 1000;
        return ret;
    }

    inline int64_t GetMonotonicMicroTimestamp() {
        struct timespec tp;
        PCHECK(clock_gettime(CLOCK_MONOTONIC, &tp) == 0) << "clock_gettime failed";
        return TimeSpecToMicro(&tp);
    }

    inline int64_t GetMilliTimestamp() {
        struct timespec tp;
        PCHECK(clock_gettime(CLOCK_MONOTONIC, &tp) == 0) << "clock_gettime failed";
        return (tp.tv_sec * 1000 + tp.tv_nsec / 1000000);
    }

    inline int64_t GetRealtimeMicroTimestamp() {
        struct timespec tp;
        PCHECK(clock_gettime(CLOCK_REALTIME, &tp) == 0) << "clock_gettime failed";
        return TimeSpecToMicro(&tp);
    }

    inline int64_t TimeSpecToNano(struct timespec *tp) {
        int64_t ret = 0;
        ret += int64_t{tp->tv_sec} * 1000000000;
        ret += int64_t{tp->tv_nsec};
        return ret;
    }

    inline int64_t GetMonotonicNanoTimestamp() {
        struct timespec tp;
        PCHECK(clock_gettime(CLOCK_MONOTONIC, &tp) == 0) << "clock_gettime failed";
        return TimeSpecToNano(&tp);
    }

    inline int64_t GetRealtimeNanoTimestamp() {
        struct timespec tp;
        PCHECK(clock_gettime(CLOCK_REALTIME, &tp) == 0) << "clock_gettime failed";
        return TimeSpecToNano(&tp);
    }

}  // namespace faas
#endif //LUMINE_TIME_H