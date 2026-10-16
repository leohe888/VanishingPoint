#include <limits>
#include <QCursor>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QtTest>

#include "canvas/vpcanvas.h"
#include "cursorhelpers.h"

class EyedropperToolTest : public QObject
{
    Q_OBJECT
private slots:
    void cursorArtworkPreservesSamplingHotspot();
    void samplesCompositeWithoutEditing();
};

void EyedropperToolTest::samplesCompositeWithoutEditing()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    VpController controller;
    check(!controller.isToolEnabled(VpTools::Eyedropper), "no image disables eyedropper");
    QImage background(64, 64, QImage::Format_ARGB32_Premultiplied);
    background.fill(QColor(30, 80, 150));
    background.setPixelColor(0, 0, Qt::transparent);
    controller.document().setImage(background);
    check(controller.isToolEnabled(VpTools::Eyedropper), "image enables eyedropper without plane");
    controller.setTool(VpTools::Eyedropper);
    controller.pointerPress(QPointF(1.2, 1.8), 1, Qt::NoModifier);
    check(controller.brushColor() == QColor(30, 80, 150), "click updates brush color");
    const auto samples = controller.sampleBuildCount();
    controller.pointerMove(QPointF(2, 2), Qt::NoModifier);
    check(controller.sampleBuildCount() == samples, "drag reuses sampled composite");
    check(!controller.document().canUndo(), "color picking adds no document history");
    const QColor picked = controller.brushColor();
    controller.pointerPress(QPointF(-.1, 2), 1, Qt::NoModifier);
    controller.pointerPress(QPointF(64, 2), 1, Qt::NoModifier);
    controller.pointerPress(QPointF(0, 0), 1, Qt::NoModifier);
    check(!controller.pickBrushColor(QPointF(std::numeric_limits<qreal>::quiet_NaN(), 1)), "invalid point rejected");
    check(controller.brushColor() == picked, "outside and transparent pixels preserve color");

    { QPainter painter(&controller.document().paintLayer()); painter.fillRect(QRect(10, 10, 8, 8), Qt::green); }
    controller.document().commitHistory();
    const auto paintBefore = controller.document().paintLayer();
    FloatingSelection floating;
    floating.bitmap = QImage(10, 10, QImage::Format_ARGB32_Premultiplied);
    floating.bitmap.fill(Qt::red); floating.placementOrigin = QPointF(25, 25); floating.opacity = 50;
    controller.document().addFloatingSelection(floating);
    const bool canUndo = controller.canUndo();
    int changed = 0;
    QObject::connect(&controller, &VpController::brushChanged, [&] { ++changed; });
    controller.pointerPress(QPointF(12, 12), 1, Qt::NoModifier);
    check(controller.brushColor() == QColor(Qt::green), "samples paint layer");
    QImage composite = background;
    { QPainter painter(&composite); painter.setOpacity(.5); painter.fillRect(QRect(25, 25, 10, 10), Qt::red); }
    QColor expected = composite.pixelColor(28, 28); expected.setAlpha(255);
    controller.pointerMove(QPointF(28, 28), Qt::NoModifier);
    check(controller.brushColor() == expected, "samples translucent floating composite");
    controller.pointerRelease(QPointF(2, 2), Qt::NoModifier);
    check(controller.brushColor() == QColor(30, 80, 150), "release samples final position");
    check(controller.document().floatingSelections().size() == 1
          && controller.document().floatingSelection(0) == floating
          && controller.document().selectedFloatingSelection() == 0, "sampling does not move bake or deselect floating content");
    check(controller.document().paintLayer() == paintBefore && controller.canUndo() == canUndo,
          "sampling preserves paint and history");
    controller.undo();
    check(controller.document().floatingSelections().isEmpty(), "sampling consumes no undo step");
    check(changed >= 3, "color change notifies shared options");

}

void EyedropperToolTest::cursorArtworkPreservesSamplingHotspot()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    VpController controller;
    QImage background(64,64,QImage::Format_ARGB32_Premultiplied);
    background.fill(Qt::white);
    QVERIFY(controller.document().setImage(background));
    controller.setTool(VpTools::Eyedropper);
    VpCanvas canvas;
    canvas.setController(&controller);
    canvas.setEyedropperCursorSource(QUrl::fromLocalFile(QStringLiteral(VP_EYEDROPPER_CURSOR)));
    const QPixmap source(QStringLiteral(VP_EYEDROPPER_CURSOR));
    check(!source.isNull(), "eyedropper cursor artwork loads");
    const qreal ratio = QGuiApplication::primaryScreen()->logicalDotsPerInchX() / 96.0;
    const int padding = qMax(1,qRound(ratio));
    check(canvas.cursor().shape() == Qt::BitmapCursor
              && canvas.cursor().hotSpot().x() <= padding + 1
              && canvas.cursor().hotSpot().y() >= canvas.cursor().pixmap().height() - padding - 2,
          "normalized eyedropper retains lower-left sampling hotspot");
    checkCursorSize(canvas.cursor());
    controller.setTool(VpTools::Hand);
    check(canvas.cursor().shape() == Qt::OpenHandCursor, "switching tool restores hand cursor");
    controller.setTool(VpTools::Eyedropper);
    check(canvas.cursor().shape() == Qt::BitmapCursor, "switching back restores eyedropper cursor");

}

QTEST_MAIN(EyedropperToolTest)
#include "tst_eyedroppertool.moc"
