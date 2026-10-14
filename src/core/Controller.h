#pragma once
// GUI-thread orchestrator: session decisions -> probes -> "готово к показу" -> window (spec: "Показ", "Сессия").
#include "Keys.h"
#include "Session.h"
#include "Types.h"
#include <QObject>
#include <QProcessEnvironment>
#include <QRectF>
#include <memory>

class QQmlApplicationEngine;
class QQuickWindow;

namespace ql {

class PreviewerService;

class Controller : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString kind READ kind NOTIFY contentChanged)                // kindName(); "" when nothing is shown
    Q_PROPERTY(QRectF contentRect READ contentRect NOTIFY contentChanged)   // content rect inside the window
    Q_PROPERTY(QString imageSource READ imageSource NOTIFY imageChanged)    // decoded image or animation frame
    Q_PROPERTY(QString placeholderSource READ placeholderSource NOTIFY contentChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY contentChanged)                   // office conversion running
    Q_PROPERTY(double scale READ scale NOTIFY contentChanged)               // monitor scale (1.25): QML sourceSize = rect * scale
    Q_PROPERTY(QString cardName READ cardName NOTIFY contentChanged)        // up to 2 lines joined by '\n' (2nd elided in the middle)
    Q_PROPERTY(QString cardType READ cardType NOTIFY contentChanged)
    Q_PROPERTY(QString cardDetails READ cardDetails NOTIFY contentChanged) // "1,2 MB · 14.10.2026" or items
    Q_PROPERTY(QString cardReason READ cardReason NOTIFY contentChanged)
    Q_PROPERTY(QString cardIcon READ cardIcon NOTIFY contentChanged)       // image://ql/... of the 96 px theme icon
    Q_PROPERTY(QRectF cardRect READ cardRect NOTIFY contentChanged)        // card rect inside the window
    Q_PROPERTY(QString audioTitle READ audioTitle NOTIFY contentChanged)
    Q_PROPERTY(QString audioSubtitle READ audioSubtitle NOTIFY contentChanged)  // "artist — album"
    Q_PROPERTY(QString coverSource READ coverSource NOTIFY contentChanged)
    Q_PROPERTY(QObject *pdf READ pdfObject CONSTANT)
    Q_PROPERTY(QObject *text READ textObject CONSTANT)
    Q_PROPERTY(QObject *media READ mediaObject CONSTANT)
    Q_PROPERTY(QObject *theme READ themeObject CONSTANT)
    Q_PROPERTY(bool fullScreen READ fullScreen NOTIFY fullScreenChanged)
    Q_PROPERTY(QString hint READ hint NOTIFY hintChanged)                  // transient line ("no app"), 2 s

public:
    Controller(PreviewerService *service, Session *session, const QProcessEnvironment &launchEnv,
               QObject *parent = nullptr);
    ~Controller() override;

    void registerImageProviders(QQmlApplicationEngine *engine);  // "ql" -> ImageStore, "pdf" -> PdfPageProvider
    void attachWindow(QQuickWindow *window);
        // Qt::Window | FramelessWindowHint, _q_showWithoutActivating = true, KeyRouter event filter,
        // windowStateChanged -> fullscreen flag, width/height changes -> external size handling
    QQuickWindow *window() const;
    void warmUp();      // idle after registration: image formats, KSH definitions, one media player, palette, killOrphans

    QString kind() const;
    QRectF contentRect() const;
    double scale() const;
    QString imageSource() const;
    QString placeholderSource() const;
    bool busy() const;
    QString cardName() const;
    QString cardType() const;
    QString cardDetails() const;
    QString cardReason() const;
    QString cardIcon() const;
    QRectF cardRect() const;
    QString audioTitle() const;
    QString audioSubtitle() const;
    QString coverSource() const;
    QObject *pdfObject() const;
    QObject *textObject() const;
    QObject *mediaObject() const;
    QObject *themeObject() const;
    bool fullScreen() const;
    QString hint() const;

    Q_INVOKABLE void setVideoSink(QObject *videoSink);   // QML: VideoOutput.videoSink
    Q_INVOKABLE void openCurrent();                      // Enter, double click
    Q_INVOKABLE void togglePlay();                       // K/P, click on video

    // tests
    void setLauncherProgram(const QString &program);   // replaces "uwsm-app"
    quint64 shownGeneration() const;
    Layout currentLayout() const;
    bool windowShown() const;

Q_SIGNALS:
    void contentChanged();
    void imageChanged();
    void fullScreenChanged();
    void hintChanged();
    void scrollRequested(int action);     // int(KeyAction): PageUp, PageDown, Home, End
    void shown(quint64 generation);       // a "готово к показу" pass was applied for this generation

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace ql
