#include "platform/thread.h"

#if defined(_WIN32)
#include <windows.h>
#include <string>
#else
#include <pthread.h>
#endif

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
#elif defined(_WIN32)
    // Names show in debuggers and profilers. The names are ASCII.
    const std::string narrow(name);
    const std::wstring wide(narrow.begin(), narrow.end());
    SetThreadDescription(GetCurrentThread(), wide.c_str());
    // Above the UI threads, below real-time audio.
    const int priority = role == ThreadRole::Network ? THREAD_PRIORITY_HIGHEST
                         : role == ThreadRole::Video ? THREAD_PRIORITY_ABOVE_NORMAL
                                                     : THREAD_PRIORITY_BELOW_NORMAL;
    SetThreadPriority(GetCurrentThread(), priority);
#else
    (void)role;
    (void)name;
#endif
}

} // namespace imago
