#include "canvas/vpcanvas.h"
#include "core/scenerenderer.h"
#ifdef VP_WITH_AUXILIARY_OVERLAY
#include "canvas/auxiliaryoverlayitem.h"
#endif
#include <QGuiApplication>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutexLocker>
#include <QEventLoop>
#include <QHoverEvent>
#include <atomic>
#include <algorithm>
#include <cmath>

class MeasuredPlaneCanvas final : public VpCanvas
{
public:
    using VpCanvas::VpCanvas;
    std::atomic<bool> measuring{false};
    QMutex mutex;
    QVector<double> paintTimes;
    void paint(QPainter *painter) override {
        QElapsedTimer elapsed; elapsed.start();
        VpCanvas::paint(painter);
        if (measuring.load()) {
            QMutexLocker lock(&mutex);
            paintTimes.append(elapsed.nsecsElapsed() / 1e6);
        }
    }
protected:
    void mousePressEvent(QMouseEvent *) override {}
    void mouseMoveEvent(QMouseEvent *) override {}
    void mouseReleaseEvent(QMouseEvent *) override {}
    void hoverMoveEvent(QHoverEvent *) override {}
    void hoverLeaveEvent(QHoverEvent *) override {}
    void keyPressEvent(QKeyEvent *) override {}
    void keyReleaseEvent(QKeyEvent *) override {}
};

static double percentile(QVector<double> values, double p)
{
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    return values[qMax(0, int(std::ceil(values.size() * p)) - 1)];
}

static PerspectivePlane parentPlane(int side)
{
    // A physically consistent tilted rectangle, with finite vanishing points.
    auto project = [side](double x, double y, double z) {
        return QPointF(side * (.5 + 1.5 * x / z), side * (.5 + 1.5 * y / z));
    };
    PerspectivePlane plane({{project(-120, -100, 1000), project(72, -100, 1144),
        project(14.4, 28, 1220.8), project(-177.6, 28, 1076.8)}},
        {{QPointF(), QPointF(side * .3, 0), QPointF(side * .3, side * .2), QPointF(0, side * .2)}});
    plane.setSurfaceGroupId(0);
    return plane;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (argc < 3) return 2; // output.json imageSide [functional]
    const QString output = QString::fromLocal8Bit(argv[1]);
    const int side = QByteArray(argv[2]).toInt();
    const bool functional = argc > 3;
    const QStringList names{"move", "resize_edge", "extrude", "rotate_drag", "angle_parameter"};
    VpController controller;
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(QColor("#606060"));
    QQuickWindow window;
    window.setFlags(Qt::Tool | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    window.setTitle("Vanishing point plane overlay check");
    window.resize(1600, 1000);
    auto *canvas = new MeasuredPlaneCanvas(window.contentItem());
    canvas->setSize(QSizeF(1600, 1000));
    canvas->setController(&controller);
    QElapsedTimer clock, phaseClock;
    clock.start();
    struct Sample { int phase; double input, submit; };
    QVector<Sample> samples;
    QVector<double> handlers;
    QJsonArray results, failures;
    bool failed = false;
    auto expect = [&](bool valid, const QString &name) {
        if (!valid) { failed = true; failures.append(name); }
    };
    int phase = -1, activeIndex = 0;
    qint64 inputId = 0, syncId = 0, submittedId = 0;
    Sample input{}, sync{};
    QPointF press, normal, seam, rotateVector, lastPoint;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    PerspectivePlane original, initialActive;
    auto requestGuides = [&] {
#ifdef VP_WITH_AUXILIARY_OVERLAY
        controller.guidesRepaintRequested();
#else
        controller.repaintRequested();
#endif
    };
    auto builds = [&]() -> quint64 {
#ifdef VP_WITH_AUXILIARY_OVERLAY
        return static_cast<AuxiliaryOverlayItem *>(canvas->childItems().first())->planeGeometryBuildCount();
#else
        return 0;
#endif
    };
    quint64 startingBuilds = 0;
    bool countingStarted = false;
    QObject::connect(&window, &QQuickWindow::beforeSynchronizing, &window, [&] {
        syncId = inputId; sync = input;
    }, Qt::DirectConnection);
    QObject::connect(&window, &QQuickWindow::frameSwapped, &window, [&] {
        if (canvas->measuring.load() && syncId > submittedId) {
            sync.submit = clock.nsecsElapsed() / 1e6;
            QMutexLocker lock(&canvas->mutex);
            samples.append(sync);
        }
        submittedId = syncId;
    }, Qt::DirectConnection);
    auto settle = [&] {
        QEventLoop loop;
        QTimer::singleShot(70, &loop, &QEventLoop::quit);
        loop.exec();
    };
    auto imageToWidget = [&](const QPointF &point) {
        const qreal shown = side * canvas->zoom();
        return point * canvas->zoom() + QPointF(
            canvas->horizontalSize() >= 1 ? (canvas->width() - shown) / 2 : -canvas->horizontalPosition() * shown,
            canvas->verticalSize() >= 1 ? (canvas->height() - shown) / 2 : -canvas->verticalPosition() * shown);
    };
    auto whitePixels = [&](const QImage &shot, const QPointF &point) {
        const QPoint center = (imageToWidget(point) * window.devicePixelRatio()).toPoint();
        const int radius = int(4 * window.devicePixelRatio());
        int count = 0;
        for (int y = center.y() - radius; y <= center.y() + radius; ++y)
            for (int x = center.x() - radius; x <= center.x() + radius; ++x)
                if (shot.rect().contains(x, y)) {
                    const QColor color = shot.pixelColor(x, y);
                    if (color.red() > 210 && color.green() > 220 && color.blue() > 225) ++count;
                }
        return count;
    };
    auto gridVisible = [&](const QImage &shot, const PerspectivePlane &plane) {
        const auto &c = plane.quad().canvasCorners();
        const auto &s = plane.quad().surfaceCorners();
        const qreal safe = qMax(qreal(controller.gridSize()), 4.0 / canvas->zoom());
        const int divisions = int(qBound(1.0,
            (QLineF(c[0], c[1]).length() + QLineF(c[3], c[2]).length()) / (2 * safe), 256.0));
        if (divisions < 2) return false;
        const qreal t = 1.0 / divisions;
        const auto mapping = plane.quad().surfaceToCanvasTransform();
        QPointF a, b;
        if (!mapping.mapForward(s[0] + (s[1] - s[0]) * t, &a)
            || !mapping.mapForward(s[3] + (s[2] - s[3]) * t, &b)) return false;
        const QPoint p = (imageToWidget((a + b) / 2) * window.devicePixelRatio()).toPoint();
        const int radius = int(2 * window.devicePixelRatio());
        for (int y = p.y() - radius; y <= p.y() + radius; ++y)
            for (int x = p.x() - radius; x <= p.x() + radius; ++x)
                if (shot.rect().contains(x, y)) {
                    const QColor color = shot.pixelColor(x, y);
                    if (color.blue() > color.green() + 8 && color.green() > color.red() + 15) return true;
                }
        return false;
    };
    auto initialize = [&] {
        canvas->measuring.store(false);
        controller.cancelInteraction();
        expect(controller.document().setImage(image), "load-image");
        original = parentPlane(side);
        expect(controller.document().appendPlane(original) == 0, "append-parent");
        for (int i = 0; i < 3; ++i) {
            const qreal x = side * (.73 + i * .07), y = side * .15, size = side * .05;
            const PerspectivePlane::Corners c{{QPointF(x, y), QPointF(x + size, y),
                QPointF(x + size, y + size), QPointF(x, y + size)}};
            expect(controller.document().appendPlane(PerspectivePlane(c, c)) >= 0, "append-static-plane");
        }
        controller.document().setSelectedPlane(0);
        controller.document().resetHistory();
        controller.setTool(VpTools::EditPlane);
        controller.setGridSize(50);
        activeIndex = 0;
        modifiers = Qt::NoModifier;
        const auto &corners = original.quad().canvasCorners();
        press = original.quad().controlPoints()[4];
        expect(projectedNormalDirection(original, press, image.size(), &normal), "project-normal");
        if (phase >= 3) {
            controller.pointerPress(press, canvas->zoom(), Qt::ControlModifier);
            controller.pointerMove(press + normal * (side * .12), Qt::ControlModifier);
            controller.pointerRelease(press + normal * (side * .12), Qt::ControlModifier);
            expect(controller.document().planes().size() == 5, "create-child");
            if (controller.document().planes().size() != 5) return;
            activeIndex = 4;
            controller.document().setSelectedPlane(activeIndex);
            const auto &child = controller.document().planes()[activeIndex];
            expect(child.parentPlaneIndex() == 0 && child.isEdgeLocked(0)
                && controller.document().planes()[0].isEdgeLocked(0), "child-seam-locks");
            seam = (child.quad().canvasCorners()[0] + child.quad().canvasCorners()[1]) / 2;
            press = child.quad().controlPoints()[6];
            rotateVector = press - seam;
            modifiers = Qt::AltModifier;
        } else if (phase == 0) {
            press = (corners[0] + corners[1] + corners[2] + corners[3]) / 4;
        } else if (phase == 1) {
            press = original.quad().controlPoints()[6];
            normal = press - original.quad().controlPoints()[4];
            normal /= std::hypot(normal.x(), normal.y());
        } else {
            modifiers = Qt::ControlModifier;
        }
        requestGuides();
        initialActive = controller.document().planes()[activeIndex];
        if (phase != 4) controller.pointerPress(press, canvas->zoom(), modifiers);
        phaseClock.restart();
        countingStarted = false;
        handlers.clear();
        { QMutexLocker lock(&canvas->mutex); samples.clear(); canvas->paintTimes.clear(); }
    };
    QTimer timer;
    timer.setInterval(4); timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timer, &QTimer::timeout, &window, [&] {
        const qint64 elapsed = phaseClock.elapsed();
        if (elapsed >= 2500) {
            timer.stop(); canvas->measuring.store(false);
            QTimer::singleShot(100, &window, [&] {
                QVector<double> latencies, intervals, paints;
                QJsonArray raw;
                int longIntervals = 0;
                { QMutexLocker lock(&canvas->mutex);
                  paints = canvas->paintTimes;
                  for (int i = 0; i < samples.size(); ++i) {
                      const Sample &s = samples[i];
                      latencies.append(s.submit - s.input);
                      raw.append(QJsonObject{{"inputMs", s.input}, {"submitMs", s.submit}});
                      if (i) { const double interval = s.submit - samples[i - 1].submit;
                          intervals.append(interval); if (interval > 33.333333) ++longIntervals; }
                  }
                }
                expect(samples.size() >= 30, names[phase] + ":active-frames");
                QJsonObject result{{"operation", names[phase]}, {"previewFrames", samples.size()},
                    {"inputEvents", handlers.size()}, {"latencyP95Ms", percentile(latencies, .95)},
                    {"latencyP99Ms", percentile(latencies, .99)}, {"intervalP95Ms", percentile(intervals, .95)},
                    {"contentPaintCount", paints.size()}, {"contentPaintP95Ms", percentile(paints, .95)},
                    {"handlerP95Ms", percentile(handlers, .95)},
                    {"longIntervalPercent", intervals.isEmpty() ? 0 : 100.0 * longIntervals / intervals.size()},
                    {"planeBuilds", double(builds() - startingBuilds)}, {"samples", raw}};
#ifdef VP_WITH_AUXILIARY_OVERLAY
                expect(paints.isEmpty(), names[phase] + ":no-content-paint");
                expect(builds() - startingBuilds <= quint64(handlers.size() + 2), names[phase] + ":static-plane-cache");
#endif
                const QImage shot = window.grabWindow();
                shot.save(output + "." + names[phase] + ".png");
                if (phase == 2) {
                    const auto *preview = controller.extrudePreview();
                    expect(preview && preview->quad().isValid(), "valid-extrusion-preview");
                    expect(controller.document().planes()[0] == original, "extrusion-keeps-parent");
                } else {
                    const auto &active = controller.document().planes()[activeIndex];
                    expect(!(active == initialActive), names[phase] + ":geometry-changed");
                    if (functional) {
                        expect(gridVisible(shot, active), names[phase] + ":grid-visible");
                        const auto points = active.quad().controlPoints();
                        for (int i = 0; i < points.size(); ++i)
                            expect(active.controlPointEditable(i) ? whitePixels(shot, points[i]) > 0
                                : whitePixels(shot, points[i]) == 0, names[phase] + ":handle-" + QString::number(i));
                    }
                }
                if (phase != 4) controller.pointerRelease(lastPoint, modifiers);
                if (functional) {
                    const QVector<PerspectivePlane> finalPlanes = controller.document().planes();
                    controller.undo(); controller.redo();
                    expect(controller.document().planes() == finalPlanes, names[phase] + ":undo-redo");
                    expect(!controller.document().hasPaintContent(), names[phase] + ":pixels-unchanged");
                    const QImage result = controller.resultImage();
                    bool transparent = true;
                    for (int y = 0; y < result.height() && transparent; ++y) {
                        const auto *row = reinterpret_cast<const QRgb *>(result.constScanLine(y));
                        for (int x = 0; x < result.width(); ++x) if (qAlpha(row[x])) { transparent = false; break; }
                    }
                    expect(transparent, names[phase] + ":export-excludes-guides");
                    canvas->setZoom(.25); canvas->scrollTo(.3, .4); settle();
                    canvas->fitView(); settle();
                    if (phase == 0) {
                        controller.setGridSize(4); settle();
                        const QImage dense = window.grabWindow();
                        expect(gridVisible(dense, controller.document().planes()[activeIndex]), "dense-grid-visible");
                        dense.save(output + ".dense-grid.png");
                    }
                }
                results.append(result);
                ++phase;
                if (phase < names.size()) { initialize(); timer.start(); return; }
                if (functional) {
                    FloatingSelection floating;
                    floating.bitmap = QImage(int(side * .08), int(side * .08), QImage::Format_ARGB32_Premultiplied);
                    floating.bitmap.fill(QColor(250, 240, 220));
                    floating.placementOrigin = QPointF(side * .81, side * .56);
                    const int index = controller.document().addFloatingSelection(floating);
                    const QPointF center = floating.placementOrigin + QPointF(floating.bitmap.width() / 2.0, floating.bitmap.height() / 2.0);
                    const QPointF moved = center + QPointF(side * .04, -side * .05);
                    controller.pointerPress(center, canvas->zoom(), Qt::NoModifier);
                    controller.pointerMove(moved, Qt::NoModifier); settle();
                    expect(!(controller.document().floatingSelection(index) == floating), "floating-drag-started");
                    controller.setTool(VpTools::Hand); settle();
                    expect(controller.document().floatingSelection(index) == floating, "tool-switch-cancels-floating-drag");
                    const QPoint pixel = (imageToWidget(center) * window.devicePixelRatio()).toPoint();
                    const QImage restored = window.grabWindow();
                    expect(restored.rect().contains(pixel) && restored.pixelColor(pixel).red() > 220,
                           "tool-switch-refreshes-restored-content");
                }
                QJsonObject root{{"qt", QString(qVersion())}, {"imageSide", side},
                    {"dpr", window.devicePixelRatio()}, {"windowWidth", canvas->width()},
                    {"windowHeight", canvas->height()}, {"passed", !failed},
                    {"functionalChecked", functional}, {"failures", failures}, {"results", results}};
                QFile file(output);
                if (!file.open(QIODevice::WriteOnly)) { app.exit(4); return; }
                file.write(QJsonDocument(root).toJson());
                app.exit(failed ? 5 : 0);
            });
            return;
        }
        if (elapsed >= 500 && !countingStarted) {
            startingBuilds = builds(); countingStarted = true;
        }
        canvas->measuring.store(elapsed >= 500);
        const double t = elapsed / 1000.0;
        if (phase == 0) lastPoint = press + QPointF(side * .025 * std::sin(t * 3), side * .02 * std::cos(t * 4));
        if (phase == 1) lastPoint = press + normal * (side * .025 * std::sin(t * 3));
        if (phase == 2) lastPoint = press + normal * (side * (.12 + .025 * std::sin(t * 3)));
        if (phase == 3) { const double angle = .3 * std::sin(t * 3);
            lastPoint = seam + QPointF(rotateVector.x() * std::cos(angle) - rotateVector.y() * std::sin(angle),
                rotateVector.x() * std::sin(angle) + rotateVector.y() * std::cos(angle)); }
        input = {phase, clock.nsecsElapsed() / 1e6, 0}; ++inputId;
        QElapsedTimer elapsedHandler; elapsedHandler.start();
        if (phase == 4) controller.setPlaneAngle(90 + 18 * std::sin(t * 3));
        else controller.pointerMove(lastPoint, modifiers);
        if (canvas->measuring.load()) handlers.append(elapsedHandler.nsecsElapsed() / 1e6);
    });
    controller.document().setImage(image);
    window.show();
    QTimer::singleShot(1000, &window, [&] { phase = 0; initialize(); timer.start(); });
    QTimer::singleShot(40000, &app, [&] { app.exit(6); });
    return app.exec();
}
