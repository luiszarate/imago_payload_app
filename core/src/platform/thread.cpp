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
    (void)role;
#if defined(__APPLE__)
    pthread_setname_np(name);
    // User-interactive QoS keeps these threads on performance cores and ahead of background work.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), name);
    // Lower nice value for this thread only. Unprivileged processes may be refused; that is fine.
    const auto tid = static_cast<id_t>(syscall(SYS_gettid));
    setpriority(PRIO_PROCESS, tid, role == ThreadRole::Network ? -6 : -4);
#else
    (void)name;
#endif
}

} // namespace imago
