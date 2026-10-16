#include "canvas/vpcanvas.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QHoverEvent>
#include <QElapsedTimer>
#include <QTimer>
#include <QPainter>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QMutexLocker>
#include <QEventLoop>
#include <algorithm>
#include <atomic>
#include <cmath>

struct Sample { int cornerCount; qint64 inputNs, submitNs; };

class MeasuredCanvas final : public VpCanvas
{
public:
    using VpCanvas::VpCanvas;
    void hover(const QPointF &point) {
        QHoverEvent event(QEvent::HoverMove, point, point);
        VpCanvas::hoverMoveEvent(&event);
    }
    void leave() {
        QHoverEvent event(QEvent::HoverLeave, QPointF(), QPointF());
        VpCanvas::hoverLeaveEvent(&event);
    }
    void paint(QPainter *painter) override {
        QElapsedTimer timer;
        timer.start();
        VpCanvas::paint(painter);
        if (measuring.load()) {
            QMutexLocker lock(&mutex);
            paintTimes.append(timer.nsecsElapsed());
        }
    }
    std::atomic<bool> measuring{false};
    QMutex mutex;
    QVector<qint64> paintTimes;
protected:
    // Replay inputs only; incidental desktop input must not alter the workload.
    void mousePressEvent(QMouseEvent *) override {}
    void mouseMoveEvent(QMouseEvent *) override {}
    void mouseReleaseEvent(QMouseEvent *) override {}
    void hoverMoveEvent(QHoverEvent *) override {}
    void hoverLeaveEvent(QHoverEvent *) override {}
    void keyPressEvent(QKeyEvent *) override {}
    void keyReleaseEvent(QKeyEvent *) override {}
};

static double percentile(QVector<double> values, double quantile)
{
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    return values[qMax(0, int(std::ceil(values.size() * quantile)) - 1)];
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 3) return 2; // output.json imageSide [screenshot.png] [functional]
    const QString output = QString::fromLocal8Bit(argv[1]);
    const int side = QByteArray(argv[2]).toInt();
    const bool functional = argc > 4;
    VpController controller;
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(QColor("#606060"));
    { QPainter painter(&image); painter.setPen(QPen(QColor("#808080"), 3));
      for (int x = 0; x < side; x += 64) painter.drawLine(x, 0, side - x, side); }
    if (!controller.document().setImage(image)) return 3;
    QQuickWindow window;
    // Occluded windows may stop rendering on Windows. Keep the short benchmark
    // visible without taking keyboard focus so every round measures active preview.
    window.setFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    window.setTitle("Vanishing point creation preview benchmark");
    window.resize(1600, 1000);
    auto *canvas = new MeasuredCanvas(window.contentItem());
    canvas->setSize(QSizeF(1600, 1000));
    canvas->setController(&controller);
    canvas->fitView();
    QElapsedTimer clock;
    clock.start();
    QVector<Sample> samples;
    QVector<double> eventTimes;
    qint64 inputNs = 0, inputId = 0, syncedId = 0, submittedId = 0;
    Sample synced{};
    int corners = 0;
    // Direct callbacks run at synchronization/swap, never via a delayed GUI slot.
    // GUI state is stable during beforeSynchronizing. Carry that input into its frame.
    QObject::connect(&window, &QQuickWindow::beforeSynchronizing, &window, [&] {
        syncedId = inputId;
        synced = {corners, inputNs, 0};
    }, Qt::DirectConnection);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&] {
        if (canvas->measuring.load() && syncedId > submittedId) {
            synced.submitNs = clock.nsecsElapsed();
            QMutexLocker lock(&canvas->mutex);
            samples.append(synced);
        }
        submittedId = syncedId;
    }, Qt::DirectConnection);
    window.show();
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(4);
    QElapsedTimer phaseClock;
    bool failed = false;
    QJsonArray failures;
    auto expect = [&](bool condition, const char *name) {
        if (!condition) { failed = true; failures.append(QString::fromLatin1(name)); }
    };
    auto press = [&](const QPointF &point) {
        controller.pointerPress(point, canvas->zoom(), Qt::NoModifier);
        controller.pointerRelease(point, Qt::NoModifier);
    };
    auto nextCorner = [&] {
        canvas->measuring.store(false);
        ++corners;
        const QPointF points[] = {QPointF(side * .2, side * .2), QPointF(side * .7, side * .2),
                                  QPointF(side * .7, side * .7)};
        press(points[corners - 1]);
        phaseClock.restart();
    };
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        const qint64 elapsed = phaseClock.elapsed();
        if (elapsed >= 2500) {
            canvas->measuring.store(false);
            if (corners < 3) { nextCorner(); return; }
            timer.stop();
            QTimer::singleShot(100, &window, [&] {
                const QImage screenshot = window.grabWindow();
                const int previewPointCount = controller.creationPoints().size();
                auto *previewState = canvas->childItems().isEmpty() ? nullptr : canvas->childItems().first();
                const QString previewDescription = previewState ? QString("visible=%1 size=%2x%3 children=%4 rasterVisible=%5 rasterSize=%6x%7")
                    .arg(previewState->isVisible()).arg(previewState->width()).arg(previewState->height())
                    .arg(previewState->childItems().size())
                    .arg(!previewState->childItems().isEmpty() && previewState->childItems().first()->isVisible())
                    .arg(previewState->childItems().isEmpty() ? 0 : previewState->childItems().first()->width())
                    .arg(previewState->childItems().isEmpty() ? 0 : previewState->childItems().first()->height()) : QString("missing");
                if (argc > 3) screenshot.save(QString::fromLocal8Bit(argv[3]));
                if (functional) {
                    auto settle = [&] {
                        QEventLoop loop;
                        QTimer::singleShot(60, &loop, &QEventLoop::quit);
                        loop.exec();
                    };
                    auto countColor = [&](const QImage &shot, const QPointF &point, bool border) {
                        int count = 0;
                        const QPoint center = (point * window.devicePixelRatio()).toPoint();
                        const int radius = int(6 * window.devicePixelRatio());
                        for (int y = center.y() - radius; y <= center.y() + radius; ++y)
                            for (int x = center.x() - radius; x <= center.x() + radius; ++x) {
                                if (!shot.rect().contains(x, y)) continue;
                                const QColor c = shot.pixelColor(x, y);
                                if (border ? (c.blue() > c.red() + 25 && c.green() > c.red() + 20 && c.blue() > c.green() + 8)
                                           : (c.red() < 110 && c.green() > 140 && c.blue() > 190)) ++count;
                            }
                        return count;
                    };
                    // Cover origin, leaving, zoom, cancellation, tool switch, completion,
                    // replacement and controller destruction without content repaints on hover.
                    auto *preview = canvas->childItems().isEmpty() ? nullptr : canvas->childItems().first();
                    expect(preview && preview->isVisible(), "preview-visible");
                    const QPointF first(800 - side * canvas->zoom() * .3, 500 - side * canvas->zoom() * .3);
                    expect(countColor(screenshot, first + QPointF(12, 0), false) > 0, "fixed-edge-visible");
                    const QPointF origin((1600 - side * canvas->zoom()) / 2, (1000 - side * canvas->zoom()) / 2);
                    canvas->hover(origin);
                    settle();
                    const QImage originShot = window.grabWindow();
                    expect(countColor(originShot, origin, true) >= 4, "image-origin-visible");
                    originShot.save(output + ".origin.png");
                    canvas->leave();
                    settle();
                    const QImage leaveShot = window.grabWindow();
                    expect(countColor(leaveShot, origin, true) == 0, "cursor-hidden-on-leave");
                    expect(countColor(leaveShot, first + QPointF(12, 0), false) > 0, "fixed-edge-kept-on-leave");
                    auto imageToWidget = [&](const QPointF &point) {
                        const qreal width = side * canvas->zoom(), height = width;
                        const QPointF offset(canvas->horizontalSize() >= 1 ? (canvas->width() - width) / 2
                            : -canvas->horizontalPosition() * width,
                            canvas->verticalSize() >= 1 ? (canvas->height() - height) / 2
                            : -canvas->verticalPosition() * height);
                        return offset + point * canvas->zoom();
                    };
                    const QPointF firstImagePoint(side * .2, side * .2);
                    canvas->setZoom(.25);
                    settle();
                    expect(countColor(window.grabWindow(), imageToWidget(firstImagePoint), true) >= 4, "zoom-keeps-corner-aligned");
                    canvas->scrollTo(.3, .4);
                    settle();
                    expect(countColor(window.grabWindow(), imageToWidget(firstImagePoint), true) >= 4, "scroll-keeps-corner-aligned");
                    window.resize(1280, 800);
                    canvas->setSize(QSizeF(1280, 800));
                    settle();
                    expect(countColor(window.grabWindow(), imageToWidget(firstImagePoint), true) >= 4, "resize-keeps-corner-aligned");
                    canvas->fitView();
                    controller.cancelInteraction();
                    expect(controller.creationPoints().isEmpty(), "cancel-clears-points");
                    press(QPointF(side * .2, side * .2));
                    controller.setTool(VpTools::Hand);
                    expect(preview && !preview->isVisible(), "tool-switch-hides-preview");
                    controller.setTool(VpTools::CreatePlane);
                    press(QPointF(side * .2, side * .2));
                    press(QPointF(side * .7, side * .2));
                    press(QPointF(side * .7, side * .7));
                    press(QPointF(side * .2, side * .7));
                    expect(controller.document().planes().size() == 1 && controller.creationPoints().isEmpty(), "four-click-plane-completion");
                    controller.undo(); controller.redo();
                    expect(controller.document().planes().size() == 1, "undo-redo");
                    controller.document().setImage(image);
                    expect(controller.creationPoints().isEmpty(), "document-replacement");
                    auto *temporary = new VpController;
                    temporary->document().setImage(image);
                    canvas->setController(temporary);
                    delete temporary;
                    expect(!canvas->controller() && preview && !preview->isVisible(), "controller-destruction");
                    canvas->setController(&controller);
                }
                QJsonObject root;
                root["qt"] = QString(qVersion());
                root["imageSide"] = side;
                root["windowWidth"] = window.width(); root["windowHeight"] = window.height();
                root["dpr"] = window.devicePixelRatio();
                root["graphicsApi"] = int(window.rendererInterface()->graphicsApi());
                root["functionalPassed"] = !failed;
                root["functionalChecked"] = functional;
                root["functionalFailures"] = failures;
                root["previewPointCount"] = previewPointCount;
                root["previewDescription"] = previewDescription;
                QJsonArray raw;
                QVector<double> latencies, intervals, paints;
                int longIntervals = 0;
                { QMutexLocker lock(&canvas->mutex);
                  for (int i = 0; i < samples.size(); ++i) {
                    const Sample &s = samples[i];
                    const double latency = (s.submitNs - s.inputNs) / 1e6;
                    latencies.append(latency);
                    raw.append(QJsonObject{{"corners", s.cornerCount}, {"inputMs", s.inputNs / 1e6},
                                           {"submitMs", s.submitNs / 1e6}, {"latencyMs", latency}});
                    if (i && samples[i - 1].cornerCount == s.cornerCount) {
                        double interval = (s.submitNs - samples[i - 1].submitNs) / 1e6;
                        intervals.append(interval);
                        if (interval > 33.333333) ++longIntervals;
                    }
                  }
                  for (qint64 ns : canvas->paintTimes) paints.append(ns / 1e6);
                }
                root["samples"] = raw; root["inputEvents"] = eventTimes.size();
                QJsonArray phaseCounts;
                for (int phase = 1; phase <= 3; ++phase) {
                    int count = 0;
                    for (const Sample &s : samples) if (s.cornerCount == phase) ++count;
                    phaseCounts.append(count);
                    expect(count >= 50, "insufficient-active-frames");
                }
                root["phaseFrameCounts"] = phaseCounts;
                root["functionalFailures"] = failures;
                root["functionalPassed"] = !failed;
                root["eventHandlerP95Ms"] = percentile(eventTimes, .95);
                root["previewFrames"] = samples.size(); root["contentPaintCount"] = paints.size();
                root["latencyP50Ms"] = percentile(latencies, .5);
                root["latencyP95Ms"] = percentile(latencies, .95);
                root["latencyP99Ms"] = percentile(latencies, .99);
                root["intervalP95Ms"] = percentile(intervals, .95);
                root["longIntervalPercent"] = intervals.isEmpty() ? 0 : 100.0 * longIntervals / intervals.size();
                root["contentPaintP95Ms"] = percentile(paints, .95);
                double paintTotal = 0; for (double t : paints) paintTotal += t;
                root["contentPaintTotalMs"] = paintTotal;
                QFile file(output);
                if (!file.open(QIODevice::WriteOnly)) { app.exit(4); return; }
                file.write(QJsonDocument(root).toJson());
                app.exit(failed ? 5 : 0);
            });
            return;
        }
        // Exclude the first half second after a click from each measurement phase.
        canvas->measuring.store(elapsed >= 500);
        const double t = elapsed / 1000.0;
        inputNs = clock.nsecsElapsed(); ++inputId;
        QElapsedTimer handler; handler.start();
        canvas->hover(QPointF(800 + 450 * std::sin(t * 3), 500 + 280 * std::cos(t * 4)));
        if (canvas->measuring.load()) eventTimes.append(handler.nsecsElapsed() / 1e6);
    });
    QTimer::singleShot(1000, &window, [&] { nextCorner(); timer.start(); });
    QTimer::singleShot(20000, &app, [&] { app.exit(6); });
    return app.exec();
}
