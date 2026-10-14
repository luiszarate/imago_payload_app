#include "platform/thread.h"

#include <pthread.h>

#if defined(__APPLE__)
#include <pthread/qos.h>
#elif defined(__linux__)
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace imago {

void configureCurrentThread(const char* name, ThreadRole role) {
#if defined(__APPLE__)
    pthread_setname_np(name);
    // User-interactive QoS keeps these threads on performance cores and ahead of background work.
    pthread_set_qos_class_self_np(
        role == ThreadRole::Background ? QOS_CLASS_UTILITY : QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name);
    // Lower nice value for this thread only. Unprivileged processes may be refused; that is fine.
    const auto tid = static_cast<id_t>(syscall(SYS_gettid));
    const int nice = role == ThreadRole::Network ? -6 : (role == ThreadRole::Video ? -4 : 5);
    setpriority(PRIO_PROCESS, tid, nice);
#else
    (void)role;
    (void)name;
#endif
}

} // namespace imago
