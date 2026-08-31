#pragma once

#include <pthread.h>
#include <sched.h>

namespace abt::util {

inline bool pinThread(int core) noexcept {
    if (core < 0) {
        return true;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(static_cast<unsigned>(core), &set);
    return pthread_setaffinity_np(pthread_self(), sizeof set, &set) == 0;
}

}
