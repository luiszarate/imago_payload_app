// Imago Payload Control for Windows and Linux: the operator screen in Qt Quick over the shared
// C++ core, video decoded with FFmpeg, controllers through SDL.

#include "app/AppModel.h"
#include "app/SystemIntegration.h"

#include <QCommandLineParser>
#include <QDebug>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>

#include <memory>

namespace {

void loadFonts() {
    const char* files[] = {"manrope_400", "manrope_500", "manrope_600", "manrope_700",
                           "space_grotesk_500", "space_grotesk_600", "space_grotesk_700"};
    for (const char* file : files) {
        if (QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/%1.ttf").arg(QLatin1String(file))) < 0) {
            qWarning() << "Cannot load font" << file;
        }
    }
    QFontDatabase::addApplicationFont(QStringLiteral(":/qt/qml/Imago/resources/fonts/MaterialIcons-Regular.ttf"));
}

/// The view source for --view; -1 if unknown.
int viewSource(const QString& name) {
    const QString n = name.toLower();
    if (n == QLatin1String("eo-ir")) return IMAGO_VIEW_EO_IR;
    if (n == QLatin1String("eo")) return IMAGO_VIEW_EO;
    if (n == QLatin1String("ir")) return IMAGO_VIEW_IR;
    if (n == QLatin1String("ir-eo")) return IMAGO_VIEW_IR_EO;
    if (n == QLatin1String("sync")) return IMAGO_VIEW_SYNC;
    if (n == QLatin1String("side-by-side")) return IMAGO_VIEW_SIDE_BY_SIDE;
    return -1;
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setOrganizationName(QStringLiteral("Imago Aerospace"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("imago.aero"));
    QGuiApplication::setApplicationName(QStringLiteral("Imago Payload Control"));
    QGuiApplication::setApplicationVersion(QStringLiteral(IMAGO_APP_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("aero.imago.PayloadControl"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/images/icon/imago-payload-control.png")));

    // Development aids: screenshots of the UI without touching it.
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption screenshot(QStringLiteral("screenshot"), QStringLiteral("Save the window to <file> and quit."),
                                        QStringLiteral("file"));
    const QCommandLineOption delay(QStringLiteral("delay"), QStringLiteral("Wait <ms> before the screenshot."),
                                   QStringLiteral("ms"), QStringLiteral("1500"));
    const QCommandLineOption profile(QStringLiteral("profile"), QStringLiteral("Select the profile named <name>."),
                                     QStringLiteral("name"));
    const QCommandLineOption connectNow(QStringLiteral("connect"), QStringLiteral("Connect with the selected profile."));
    const QCommandLineOption open(QStringLiteral("open"), QStringLiteral("Open settings, connections or hud."),
                                  QStringLiteral("page"));
    const QCommandLineOption view(QStringLiteral("view"),
                                  QStringLiteral("Once the payload is up, switch to <view>: eo-ir, eo, ir, ir-eo, sync or "
                                                 "side-by-side."),
                                  QStringLiteral("view"));
    parser.addOptions({screenshot, delay, profile, connectNow, open, view});
    parser.process(app);
    const int startView = parser.isSet(view) ? viewSource(parser.value(view)) : -1;
    if (parser.isSet(view) && startView < 0) {
        qWarning() << "Unknown view" << parser.value(view);
        return 1;
    }

    SystemIntegration::configureTimers();
    loadFonts();
    QGuiApplication::setFont(QFont(QStringLiteral("Manrope"), 10));
    QQuickStyle::setStyle(QStringLiteral("Material"));

    AppModel model(nullptr);
    AppModel::setInstance(&model);

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Imago", "Main");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (window != nullptr) {
        model.attachWindow(window);
    }

    if (parser.isSet(profile)) {
        for (const QVariant& p : model.profiles()) {
            if (p.toMap().value(QStringLiteral("name")).toString() == parser.value(profile)) {
                model.selectProfile(p.toMap().value(QStringLiteral("id")).toString());
            }
        }
    }
    if (startView >= 0) {
        // Once, on the first link up of the session.
        auto once = std::make_shared<QMetaObject::Connection>();
        *once = QObject::connect(&model, &AppModel::linkChanged, &model, [&model, startView, once] {
            if (model.link().up) {
                QObject::disconnect(*once);
                model.setView(startView);
            }
        });
    }
    if (parser.isSet(connectNow)) {
        model.connectToPayload();
    }
    if (parser.isSet(open)) {
        const QString page = parser.value(open);
        model.setProperty(page == QLatin1String("hud")           ? "showHud"
                          : page == QLatin1String("connections") ? "showConnections"
                                                                 : "showSettings",
                          true);
    }
    if (parser.isSet(screenshot) && window != nullptr) {
        const QString file = parser.value(screenshot);
        QTimer::singleShot(parser.value(delay).toInt(), &app, [window, file, &model] {
            const bool saved = window->grabWindow().save(file);
            // The software renderer of headless runs does not draw video: the decoded picture too.
            if (const QImage picture = model.videoSnapshot(); !picture.isNull()) {
                picture.save(QFileInfo(file).path() + QStringLiteral("/") + QFileInfo(file).completeBaseName() +
                             QStringLiteral("-video.png"));
            }
            qInfo() << (saved ? "Saved" : "Could not save") << file << "connected:" << model.isConnected()
                    << "link:" << model.link().up << "video:" << model.videoActive() << model.videoWidth()
                    << "x" << model.videoHeight() << model.connectError();
            QCoreApplication::exit(saved ? 0 : 1);
        });
    }

    const int result = QGuiApplication::exec();
    model.disconnectFromPayload();
    return result;
}
