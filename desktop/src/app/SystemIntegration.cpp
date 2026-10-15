#include "app/SystemIntegration.h"

#if defined(_WIN32)
#include <windows.h>
#include <timeapi.h>
#elif defined(IMAGO_HAVE_DBUS)
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif

void SystemIntegration::configureTimers() {
#if defined(_WIN32)
    timeBeginPeriod(1);
#endif
}

void SystemIntegration::setKeepAwake(bool on) {
#if defined(_WIN32)
    SetThreadExecutionState(on ? ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED : ES_CONTINUOUS);
#elif defined(IMAGO_HAVE_DBUS)
    // The freedesktop screen saver interface: GNOME, KDE and most other desktops.
    static uint cookie = 0;
    QDBusInterface saver(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/org/freedesktop/ScreenSaver"),
                         QStringLiteral("org.freedesktop.ScreenSaver"), QDBusConnection::sessionBus());
    if (!saver.isValid()) {
        return;
    }
    if (on && cookie == 0) {
        const QDBusReply<uint> reply = saver.call(QStringLiteral("Inhibit"), QCoreApplication::applicationName(),
                                                  QStringLiteral("Connected to a payload"));
        if (reply.isValid()) {
            cookie = reply.value();
        }
    } else if (!on && cookie != 0) {
        saver.call(QStringLiteral("UnInhibit"), cookie);
        cookie = 0;
    }
#else
    (void)on;
#endif
}
