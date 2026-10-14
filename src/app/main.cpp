#include "Controller.h"
#include "PreviewerService.h"
#include "Session.h"
#include <QDBusConnection>
#include <QGuiApplication>
#include <QImageReader>
#include <QLoggingCategory>
#include <QProcessEnvironment>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>
#include <libheif/heif.h>

// spec "Процесс": one process, one window, the name org.gnome.NautilusPreviewer; never exits on its own.
int main(int argc, char **argv)
{
    const QProcessEnvironment launchEnv = QProcessEnvironment::systemEnvironment();   // for child programs
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", ",");   // software decoding: NVIDIA stays asleep
    qputenv("QT_FFMPEG_ENCODING_HW_DEVICE_TYPES", ",");
    qputenv("QT_AUDIO_BACKEND", "pulseaudio");            // reconnects after a sound stack restart
    qputenv("QSG_RENDER_LOOP", "basic");                  // frame and new size in one commit
    heif_init(nullptr);

    QGuiApplication app(argc, argv);
    QGuiApplication::setDesktopFileName(QStringLiteral("omarchy-quicklook"));
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QImageReader::setAllocationLimit(1024);
    QLoggingCategory::setFilterRules(QStringLiteral("qt.multimedia.ffmpeg.mediadataholder.info=false\n"
                                                    "qt.multimedia.*.info=false"));   // FFmpeg "Input #0" dumps

    ql::Session session;
    ql::PreviewerService service(&session, QDBusConnection::sessionBus());
    ql::Controller controller(&service, &session, launchEnv);
    QQmlApplicationEngine engine;
    controller.registerImageProviders(&engine);
    engine.rootContext()->setContextProperty(QStringLiteral("ql"), &controller);
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().value(0));
    if (!window) {
        qCritical("omarchy-quicklook: qrc:/qml/Main.qml did not load");
        return 1;
    }
    controller.attachWindow(window);
    if (!service.registerOnBus()) {
        qCritical("omarchy-quicklook: cannot own org.gnome.NautilusPreviewer (taken?)");
        return 1;
    }
    QTimer::singleShot(0, &controller, &ql::Controller::warmUp);
    return app.exec();
}
