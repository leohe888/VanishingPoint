#include <QCoreApplication>
#include <QPainter>
#include <QtTest>

#include "canvas/vpcontroller.h"

class HealingBlendTest : public QObject
{
    Q_OBJECT
private slots:
    void brushAndCloneHealingPreservesCoverageAndHistory();
};

void HealingBlendTest::brushAndCloneHealingPreservesCoverageAndHistory()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    const QSize size(160, 100);
    const QPointF source(35, 50), target(115, 50);
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
            image.setPixelColor(x, y, x < 75 ? QColor(150 + (y - 50) / 2, 60, 40)
                                           : QColor(70, 140 + (y - 50) / 2, 160));
    // A small detail at the source must survive adaptation of the surrounding tone.
    image.setPixelColor(35, 50, QColor(175, 85, 65));
    QVector<PerspectivePlane> planes{PerspectivePlane(
        {{QPointF(0, 0), QPointF(160, 0), QPointF(160, 100), QPointF(0, 100)}},
        {{QPointF(0, 0), QPointF(160, 0), QPointF(160, 100), QPointF(0, 100)}})};
    QImage brushes[3], stamps[3];
    for (int mode = 0; mode < 3; ++mode) {
        brushes[mode] = QImage(size, QImage::Format_ARGB32_Premultiplied);
        brushes[mode].fill(Qt::transparent);
        BrushTool brush;
        brush.setDiameter(20); brush.setHardness(65); brush.setOpacity(55);
        brush.setColor(QColor(150, 60, 40)); brush.setHealMode(mode);
        QImage preview = brushes[mode];
        QPainter brushPainter(&preview);
        brush.renderPreview(brushPainter, planes, size, target);
        brushPainter.end();
        brush.begin(brushes[mode], planes, size, target, {}, image);
        check(preview == brushes[mode], "brush preview matches dab");
        if (mode > 0) {
            QImage raw(size, QImage::Format_ARGB32_Premultiplied); raw.fill(Qt::transparent);
            BrushTool plain; plain.setDiameter(20); plain.setHardness(65); plain.setOpacity(55);
            plain.setColor(QColor(150, 60, 40)); plain.begin(raw, planes, size, target);
            check(raw == brushes[mode], "brush press is not healed");
            plain.move(raw, target + QPointF(3, 0));
            brush.move(brushes[mode], target + QPointF(3, 0));
            check(raw == brushes[mode], "brush drag is not healed");
        } else {
            brush.move(brushes[mode], target + QPointF(3, 0));
        }
        const QImage beforeBrushRelease = brushes[mode];
        brush.end(&brushes[mode]);
        check(mode == 0 ? beforeBrushRelease == brushes[mode] : beforeBrushRelease != brushes[mode],
              "brush healing occurs only on release");
        check(!brush.drawing(), "brush ends");

        stamps[mode] = QImage(size, QImage::Format_ARGB32_Premultiplied);
        stamps[mode].fill(Qt::transparent);
        CloneTool stamp;
        stamp.setDiameter(20); stamp.setHardness(65); stamp.setOpacity(55); stamp.setHealMode(mode);
        check(stamp.pickSource(planes, size, source), "clone source picked");
        preview = stamps[mode];
        QPainter stampPainter(&preview);
        stamp.renderPreview(stampPainter, image, planes, size, target);
        stampPainter.end();
        stamp.begin(stamps[mode], image, planes, size, target);
        check(preview == stamps[mode], "clone preview matches dab");
        if (mode > 0) {
            QImage raw(size, QImage::Format_ARGB32_Premultiplied); raw.fill(Qt::transparent);
            CloneTool plain; plain.setDiameter(20); plain.setHardness(65); plain.setOpacity(55);
            plain.pickSource(planes, size, source); plain.begin(raw, image, planes, size, target);
            check(raw == stamps[mode], "clone press is not healed");
            plain.move(raw, target + QPointF(3, 0));
            stamp.move(stamps[mode], target + QPointF(3, 0));
            check(raw == stamps[mode], "clone drag is not healed");
        } else {
            stamp.move(stamps[mode], target + QPointF(3, 0));
        }
        const QImage beforeStampRelease = stamps[mode];
        stamp.end(&stamps[mode]);
        check(mode == 0 ? beforeStampRelease == stamps[mode] : beforeStampRelease != stamps[mode],
              "clone healing occurs only on release");
        check(stamps[mode].pixelColor(115, 50).red() > stamps[mode].pixelColor(118, 50).red() + 10,
              "clone source detail retained");

        // The cursor overlay is a small texture with a translated/scaled painter,
        // not an image-sized paint device. Far-away image coordinates must show.
        for (qreal scale : {.25, 1.0, 2.0}) {
            QImage cursor(48, 48, QImage::Format_ARGB32_Premultiplied); cursor.fill(Qt::transparent);
            QPainter cursorPainter(&cursor);
            cursorPainter.translate(24 - target.x() * scale, 24 - target.y() * scale);
            cursorPainter.scale(scale, scale);
            brush.renderPreview(cursorPainter, planes, size, target);
            cursorPainter.end();
            check(qAlpha(cursor.pixel(24, 24)) > 0, "local texture brush preview visible at zoom");
        }
    }
    for (int mode = 1; mode < 3; ++mode) {
        check(brushes[mode] != brushes[0] && stamps[mode] != stamps[0], "healing changes pixels");
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x)
                check(qAlpha(brushes[mode].pixel(x, y)) == qAlpha(brushes[0].pixel(x, y))
                      && qAlpha(stamps[mode].pixel(x, y)) == qAlpha(stamps[0].pixel(x, y)),
                      "healing preserves coverage");
    }
    check(brushes[1].pixelColor(115, 50).red() > brushes[1].pixelColor(115, 50).green(),
          "luminance retains brush hue");
    check(brushes[2].pixelColor(115, 50).blue() > brushes[2].pixelColor(115, 50).red(),
          "on adopts destination color");
    check(stamps[1].pixelColor(115, 50).red() > stamps[1].pixelColor(115, 50).green(),
          "luminance retains clone hue");
    check(stamps[2].pixelColor(115, 50).blue() > stamps[2].pixelColor(115, 50).red(),
          "clone on adopts destination color");

    VpController controller;
    controller.document().setImage(image);
    controller.document().appendPlane(planes[0]);
    controller.document().commitHistory();
    controller.setBrushHealMode(1); controller.setCloneHealMode(2);
    controller.setBrushHealMode(99);
    check(controller.brushHealMode() == 1 && controller.cloneHealMode() == 2, "independent valid modes");
    controller.beginBrush(target); controller.moveBrush(target + QPointF(5, 0)); controller.endBrush();
    const QImage painted = controller.resultImage();
    controller.undo();
    check(controller.resultImage() != painted, "heal stroke undo");
    controller.redo();
    check(controller.resultImage() == painted, "heal stroke redo");
    check(controller.pickCloneSource(source), "controller clone source");
    controller.beginClone(target + QPointF(0, 15)); controller.endClone();
    const QImage cloned = controller.resultImage();
    controller.undo();
    check(controller.resultImage() == painted, "heal clone undo");
    controller.redo();
    check(controller.resultImage() == cloned, "heal clone redo");
    // Finalization must preserve paint under/around the new translucent stroke.
    const QImage priorPaint = controller.document().paintLayer();
    controller.setBrushOpacity(40);
    controller.beginBrush(target + QPointF(-20, 0));
    controller.moveBrush(target + QPointF(10, 5));
    controller.endBrush();
    check(controller.document().paintLayer().pixel(0, 0) == priorPaint.pixel(0, 0), "older paint outside stroke preserved");
    controller.undo();
    check(controller.document().paintLayer() == priorPaint, "one undo restores complete older paint");

    QVector<PerspectivePlane> skewed{PerspectivePlane(
        {{QPointF(5, 5), QPointF(150, 15), QPointF(140, 90), QPointF(15, 95)}},
        {{QPointF(0, 0), QPointF(160, 0), QPointF(160, 100), QPointF(0, 100)}})};
    QPointF skewedSource, skewedTarget;
    const auto skewedMapping = skewed[0].quad().surfaceToCanvasTransform();
    skewedMapping.mapForward(source, &skewedSource);
    skewedMapping.mapForward(target, &skewedTarget);
    for (int mode = 1; mode < 3; ++mode) {
        QImage actual(size, QImage::Format_ARGB32_Premultiplied); actual.fill(Qt::transparent);
        QImage preview = actual;
        BrushTool brush; brush.setHealMode(mode); brush.setDiameter(20);
        QPainter brushPainter(&preview);
        brush.renderPreview(brushPainter, skewed, size, skewedTarget);
        brushPainter.end();
        check(!brush.begin(actual, skewed, size, skewedTarget, {}, image).isEmpty(), "perspective brush dab");
        check(actual == preview, "perspective brush preview");
        actual.fill(Qt::transparent); preview.fill(Qt::transparent);
        CloneTool stamp; stamp.setHealMode(mode); stamp.setDiameter(20);
        check(stamp.pickSource(skewed, size, skewedSource), "perspective clone source");
        QPainter stampPainter(&preview);
        stamp.renderPreview(stampPainter, image, skewed, size, skewedTarget);
        stampPainter.end();
        check(!stamp.begin(actual, image, skewed, size, skewedTarget).isEmpty(), "perspective clone dab");
        check(actual == preview, "perspective clone preview");
    }
    // Missing/transparent destination must safely fall back to ordinary painting.
    QImage transparent(size, QImage::Format_ARGB32_Premultiplied); transparent.fill(Qt::transparent);
    const auto mapping = planes[0].quad().surfaceToCanvasTransform();
    HealingBlend empty(HealingBlend::On, transparent, mapping, target, 10, QColor(Qt::red));
    check(empty.blend(qRgb(150, 60, 40), target) == qRgb(150, 60, 40), "transparent reference fallback");
}

QTEST_MAIN(HealingBlendTest)
#include "tst_healingblend.moc"
