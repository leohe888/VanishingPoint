#include <QCoreApplication>
#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QtTest>

#include "canvas/vpcanvas.h"
#include "canvas/vpcontroller.h"
#include "core/floatingselectionprojection.h"
#include "core/scenerenderer.h"
#include "testhelpers.h"
#include "tools/marqueetool.h"

namespace {

QVector<PerspectivePlane> connectedPlanes()
{
    PerspectivePlane right(
        {QPointF(110, 10), QPointF(230, 30), QPointF(230, 96), QPointF(110, 80)},
        {QPointF(100, 0), QPointF(200, 0), QPointF(200, 70), QPointF(100, 70)});
    right.setSurfaceGroupId(0);
    return {makeTestPlane(), right};
}

QPointF canvasPoint(const PerspectivePlane &plane, const QPointF &surface)
{
    QPointF result;
    plane.quad().surfaceToCanvasTransform().mapForward(surface, &result);
    return result;
}

QImage sampleImage(const QVector<PerspectivePlane> &planes)
{
    QImage image(260, 120, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < planes.size(); ++i) {
        painter.setBrush(i == 0 ? Qt::red : Qt::blue);
        painter.drawPolygon(planes[i].quad().canvasPolygon());
    }
    return image;
}

void createAcrossSeam(MarqueeTool &tool, const QVector<PerspectivePlane> &planes)
{
    // Test extraction geometry with a hard edge; the default is feather=1.
    tool.setFeather(0);
    tool.beginCreate(planes, canvasPoint(planes[0], QPointF(80, 10)));
    tool.update(canvasPoint(planes[1], QPointF(140, 50)), Qt::NoModifier, 50);
    tool.end();
}

PerspectivePlane identityPlane(qreal size = 200)
{
    const PerspectiveQuad::Corners corners{{{0,0},{size,0},{size,size},{0,size}}};
    return PerspectivePlane(corners, corners);
}

void setupMarquee(VpController &controller)
{
    QImage source(200,200,QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::red);
    { QPainter painter(&source); painter.fillRect(100,0,100,200,Qt::green); }
    controller.document().setImage(source);
    controller.document().appendPlane(identityPlane());
    controller.document().resetHistory();
    controller.setTool(VpTools::Marquee);
    controller.setMarqueeFeather(0);
    controller.setMarqueeHealMode(0);
}

void selectRect(VpController &controller)
{
    controller.pointerPress({20,20},1,Qt::NoModifier);
    controller.pointerRelease({60,60},Qt::NoModifier);
}

} // namespace

class MarqueeToolTest : public QObject
{
    Q_OBJECT
private slots:
    void createsInEveryDirectionAndRejectsSubpixelSize();
    void movementSnapsFromPressAndAllowsModifierChanges();
    void actionLifecycleAndOutlineCache();
    void filtersFacesAndRetainsDeletedGeometry();
    void fractionalPreviewPreservesPlacement_data();
    void fractionalPreviewPreservesPlacement();
    void extractionClipsSourceAndPreservesAlpha();
    void extractionLimits_data();
    void extractionLimits();
    void interruptedInteractions_data();
    void interruptedInteractions();
    void historyBoundaries_data();
    void historyBoundaries();
    void sourceFillCacheInvalidation_data();
    void sourceFillCacheInvalidation();
    void pasteCancelsFillPreview();
    void marqueePersistsAndClipsPaintingAcrossTools();
    void sourceAndDestinationMoveModes();
    void optionsControlFeatherOpacityAndHealing();
    void createsAndMovesWithShift();
    void keepsSurfaceSnapshotAcrossSeam();
    void copiesPixelsAcrossSeam();
    void rebuildsClonePreviewAndExtractsPixels();
    void clearsDegenerateAndCancelledSelection();
};

void MarqueeToolTest::createsAndMovesWithShift()
{
    MarqueeTool tool;
    QVERIFY(tool.beginCreate({makeTestPlane()}, QPointF(60, 45)));
    tool.update(QPointF(40, 35), Qt::ShiftModifier, 10);
    QCOMPARE(tool.rect(), QRectF(30, 15, 20, 20));
    tool.end();
    QVERIFY(!tool.active());
    QVERIFY(tool.beginMove(QPointF(50, 35)));
    tool.update(QPointF(67, 42), Qt::ShiftModifier, 10);
    QCOMPARE(tool.rect(), QRectF(50, 15, 20, 20));
    // 每次从按下时的选区算起，不累加上一帧位移。
    tool.update(QPointF(57, 52), Qt::ShiftModifier, 10);
    QCOMPARE(tool.rect(), QRectF(30, 35, 20, 20));
}

void MarqueeToolTest::keepsSurfaceSnapshotAcrossSeam()
{
    auto planes = connectedPlanes();
    MarqueeTool tool;
    createAcrossSeam(tool, planes);
    QVERIFY(qAbs(tool.rect().left() - 80) < 1e-6);
    QVERIFY(qAbs(tool.rect().width() - 60) < 1e-6);
    const QPointF point = canvasPoint(planes[1], QPointF(130, 30));
    QVERIFY(tool.contains(point));
    QVERIFY(tool.contains(QPointF(100, 40)));
    QVERIFY(!tool.contains(QPointF(40, 40)));
    // 修改文档里的几何不应改变正在使用的选区快照。
    planes[1].quad().setCanvasCorner(2, QPointF(250, 115));
    QPointF surface;
    QVERIFY(tool.mapToSurface(point, &surface));
    QVERIFY(QLineF(surface, QPointF(130, 30)).length() < 1e-6);
}

void MarqueeToolTest::copiesPixelsAcrossSeam()
{
    const auto planes = connectedPlanes();
    MarqueeTool tool;
    createAcrossSeam(tool, planes);
    const FloatingSelection image = tool.copy(sampleImage(planes),
                                         canvasPoint(planes[1], QPointF(130, 30)));
    QVERIFY(!image.bitmap.isNull());
    QVERIFY(image.surfaceAttached);
    QCOMPARE(image.hostQuadIndex, 1);
    QCOMPARE(image.surfaceQuads.size(), 2);
    QVERIFY(QLineF(image.placementOrigin, QPointF(80, 10)).length() < 1e-6);
    QCOMPARE(image.bitmap.pixelColor(10, 20), QColor(Qt::red));
    QCOMPARE(image.bitmap.pixelColor(40, 20), QColor(Qt::blue));
}

void MarqueeToolTest::rebuildsClonePreviewAndExtractsPixels()
{
    const auto planes = connectedPlanes();
    const QImage source = sampleImage(planes);
    QImage before(source.size(), source.format());
    before.fill(Qt::transparent);
    before.setPixelColor(5, 5, Qt::green);
    QImage preview = before;
    MarqueeTool tool;
    createAcrossSeam(tool, planes);
    const QPointF press = canvasPoint(planes[0], QPointF(90, 30));
    QVERIFY(tool.beginFill(press, source, before));
    const QPointF moved = canvasPoint(planes[1], QPointF(110, 30));
    const QRect dirty = tool.update(moved, Qt::ControlModifier, 50, &preview);
    // Filling now returns an independent selection preview instead of writing paint pixels.
    QVERIFY(dirty.isEmpty());
    QCOMPARE(preview, before);
    const auto renderPreview = [&] {
        QImage result = before;
        QPainter painter(&result);
        SceneRenderer::renderFloatingSelection(painter, tool.clone());
        return result;
    };
    preview = renderPreview();
    QCOMPARE(preview.pixelColor(95, 40), QColor(Qt::blue));
    QCOMPARE(preview.pixelColor(5, 5), QColor(Qt::green));
    const QImage firstPreview = preview;
    tool.update(press, Qt::ControlModifier, 50);
    preview = renderPreview();
    QCOMPARE(preview.pixelColor(95, 40), QColor(Qt::red));
    tool.update(moved, Qt::ControlModifier, 50);
    preview = renderPreview();
    QCOMPARE(preview, firstPreview);
    QCOMPARE(before.pixelColor(95, 40), QColor(Qt::transparent));

    const FloatingSelection image = tool.clone();
    QCOMPARE(image.bitmap.pixelColor(5, 20), preview.pixelColor(95, 40));
    QCOMPARE(image.bitmap.pixelColor(40, 20), QColor(Qt::blue));
    tool.end();
    QVERIFY(tool.clone().bitmap.isNull());
    QVERIFY(!tool.rect().isEmpty());
}

void MarqueeToolTest::clearsDegenerateAndCancelledSelection()
{
    MarqueeTool tool;
    QVERIFY(tool.beginCreate({makeTestPlane()}, QPointF(30, 30)));
    tool.update(QPointF(30, 60), Qt::NoModifier, 50);
    tool.end();
    QVERIFY(tool.rect().isEmpty());
    QVERIFY(!tool.active());
    createAcrossSeam(tool, connectedPlanes());
    tool.clear();
    QVERIFY(tool.outline().isEmpty());
    QVERIFY(tool.copy(QImage(), QPointF()).bitmap.isNull());
    QVERIFY(!tool.beginMove(QPointF(30, 30)));
    QVERIFY(!tool.beginCreate({makeTestPlane()}, QPointF(-10, -10)));
}

void MarqueeToolTest::optionsControlFeatherOpacityAndHealing()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    const QSize size(240, 120);
    QImage source(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
            source.setPixelColor(x, y, x < 110 ? QColor(170, 65 + y / 8, 40)
                                             : QColor(65, 140 + y / 8, 160));
    const PerspectivePlane plane(
        {{QPointF(0, 0), QPointF(240, 0), QPointF(240, 120), QPointF(0, 120)}},
        {{QPointF(0, 0), QPointF(240, 0), QPointF(240, 120), QPointF(0, 120)}});
    MarqueeTool tool;
    check(tool.feather() == 1 && tool.opacity() == 100 && tool.healMode() == 2, "defaults");
    tool.setFeather(99); tool.setOpacity(-1); tool.setHealMode(9);
    check(tool.feather() == 50 && tool.opacity() == 0 && tool.healMode() == 2, "clamped options");
    tool.setFeather(0); tool.setOpacity(100); tool.setHealMode(0);
    tool.beginCreate({plane}, QPointF(20, 20));
    tool.update(QPointF(80, 80), Qt::NoModifier, 50); tool.end();
    const auto hard = tool.copy(source);
    check(hard.bitmap.size() == QSize(60, 60) && hard.placementOrigin == QPointF(20, 20), "zero feather geometry");
    check(qAlpha(hard.bitmap.pixel(0, 30)) == 255, "zero feather hard edge");
    tool.setFeather(10); tool.setOpacity(50);
    const auto soft = tool.copy(source);
    check(soft.bitmap.size() == QSize(100, 100) && soft.placementOrigin == QPointF(0, 0), "feather expands support");
    check(qAlpha(soft.bitmap.pixel(19, 50)) > 0 && qAlpha(soft.bitmap.pixel(19, 50)) < 128,
          "feather extends outside selection");
    check(qAlpha(soft.bitmap.pixel(20, 50)) > 128 && qAlpha(soft.bitmap.pixel(20, 50)) < 255,
          "feather soft inner edge");
    check(qAlpha(soft.bitmap.pixel(50, 50)) == 255, "feather preserves center");
    check(soft.displaySize() == hard.displaySize()
          && soft.placementRect() == hard.placementRect(), "feather preserves logical size and position");
    check(soft.transformHandlePositions() == hard.transformHandlePositions(), "feather preserves control points");
    const auto softProjection = FloatingSelectionProjection::forSelection(soft);
    check(softProjection->canvasOutline().boundingRect() == QRectF(20, 20, 60, 60), "selection outline excludes feather padding");
    check(softProjection->canvasPaintOutline().boundingRect() == QRectF(0, 0, 100, 100), "render bounds retain outward feather pixels");
    check(!softProjection->hitTest(QPointF(10, 50)) && softProjection->hitTest(QPointF(30, 50)), "hit test uses logical selection");
    for (bool fromCenter : {false, true}) {
        FloatingSelectionTransformTool hardScale, softScale;
        hardScale.beginTransform(hard, QPointF(80, 80), 2, FloatingSelectionTransformTool::Mode::Scale);
        softScale.beginTransform(soft, QPointF(80, 80), 2, FloatingSelectionTransformTool::Mode::Scale);
        FloatingSelection scaledHard, scaledSoft;
        hardScale.update(QPointF(140, 140), true, fromCenter, &scaledHard);
        softScale.update(QPointF(140, 140), true, fromCenter, &scaledSoft);
        check(scaledHard.transformHandlePositions() == scaledSoft.transformHandlePositions(), "feather scale uses original rectangle");
    }
    FloatingSelectionTransformTool hardRotate, softRotate;
    hardRotate.beginTransform(hard, QPointF(98, 20), 1, FloatingSelectionTransformTool::Mode::Rotate);
    softRotate.beginTransform(soft, QPointF(98, 20), 1, FloatingSelectionTransformTool::Mode::Rotate);
    FloatingSelection rotatedHard, rotatedSoft;
    hardRotate.update(QPointF(80, 98), false, false, &rotatedHard);
    softRotate.update(QPointF(80, 98), false, false, &rotatedSoft);
    check(rotatedHard.transformHandlePositions() == rotatedSoft.transformHandlePositions(), "feather rotation uses original center");
    QImage rendered(size, QImage::Format_ARGB32_Premultiplied); rendered.fill(Qt::transparent);
    QPainter painter(&rendered); SceneRenderer::renderFloatingSelection(painter, soft); painter.end();
    check(qAbs(qAlpha(rendered.pixel(50, 50)) - 128) <= 1, "opacity applies once at rendering");
    const QImage paint = rendered;
    check(tool.beginFill(QPointF(50, 50), source, paint), "begin fill");
    tool.update(QPointF(150, 50), Qt::ControlModifier, 50);
    const auto preview = tool.clone(.5), full = tool.clone();
    check(full.opacity == 50 && preview.opacity == 50, "fill opacity preserved at both resolutions");
    check(full.placementOrigin == preview.placementOrigin && preview.scaleFactors.x() == 2, "fill preview geometry");
    check(full.placementRect() == preview.placementRect()
          && full.transformHandlePositions() == preview.transformHandlePositions(), "reduced feather preview preserves control geometry");
    tool.end();

    for (int mode = 0; mode < 3; ++mode) {
        VpController controller;
        check(controller.marqueeFeather() == 1 && controller.marqueeOpacity() == 100
              && controller.marqueeHealMode() == 2, "controller defaults");
        controller.document().setImage(source);
        controller.document().appendPlane(plane); controller.document().commitHistory();
        controller.setTool(VpTools::Marquee);
        controller.setMarqueeFeather(0); controller.setMarqueeOpacity(60); controller.setMarqueeHealMode(mode);
        controller.pointerPress(QPointF(20, 20), 1, Qt::NoModifier);
        controller.pointerMove(QPointF(80, 80), Qt::NoModifier);
        controller.pointerRelease(QPointF(80, 80), Qt::NoModifier);
        controller.pointerPress(QPointF(50, 50), 1, Qt::AltModifier);
        check(controller.document().floatingSelections().size() == 1, "alt creates floating selection");
        const auto original = controller.document().floatingSelection(0);
        controller.pointerMove(QPointF(170, 50), Qt::AltModifier);
        const auto moving = controller.document().floatingSelection(0);
        check(moving.bitmap == original.bitmap, "move preview unhealed");
        controller.pointerRelease(QPointF(170, 50), Qt::AltModifier);
        const auto moved = controller.document().floatingSelection(0);
        check(moved.opacity == 60 && moved.healMode == mode, "options survive append and move");
        check(mode == 0 ? moved.bitmap == original.bitmap : moved.bitmap != original.bitmap, "heal only after move release");
        check(mode != 1 || moved.bitmap.pixelColor(30, 30).red() > moved.bitmap.pixelColor(30, 30).green(), "luminance preserves hue");
        check(mode != 2 || moved.bitmap.pixelColor(30, 30).blue() > moved.bitmap.pixelColor(30, 30).red(), "on matches destination color");
        const auto result = controller.resultImage();
        controller.undo(); check(controller.document().floatingSelection(0) == original, "move undo is one step");
        controller.redo(); check(controller.resultImage() == result, "move redo restores result");
        controller.setMarqueeOpacity(0);
        check(qAlpha(controller.resultImage().pixel(170, 50)) == 0, "zero opacity supported");
        controller.undo();
        check(controller.marqueeOpacity() == 60 && controller.resultImage() == result, "opacity undo synchronizes control");
        if (mode) {
            controller.setMarqueeHealMode(0);
            check(controller.document().floatingSelection(0).bitmap == original.bitmap, "off restores unhealed pixels");
            controller.setMarqueeHealMode(mode);
            check(controller.resultImage() == result, "healing does not accumulate");
        }
    }
    // Feathered Alt movement must preserve the grab point despite the padded bitmap.
    VpController padded;
    padded.document().setImage(source); padded.document().appendPlane(plane); padded.document().commitHistory();
    padded.setTool(VpTools::Marquee); padded.setMarqueeFeather(10);
    padded.pointerPress(QPointF(20, 20), 1, Qt::NoModifier);
    padded.pointerRelease(QPointF(80, 80), Qt::NoModifier);
    padded.pointerPress(QPointF(50, 50), 1, Qt::AltModifier);
    padded.pointerRelease(QPointF(170, 50), Qt::AltModifier);
    check(padded.document().floatingSelection(0).placementOrigin == QPointF(120, 0), "feather grab offset preserved");
    // Ctrl-fill keeps the selection at its original destination and samples the
    // dragged-to area. Its reduced preview stays unhealed until release.
    for (int mode = 0; mode < 3; ++mode) {
        VpController fill;
        fill.document().setImage(source); fill.document().appendPlane(plane); fill.document().commitHistory();
        fill.setTool(VpTools::Marquee); fill.setMarqueeFeather(0);
        fill.setMarqueeOpacity(35); fill.setMarqueeHealMode(mode);
        fill.pointerPress(QPointF(20, 20), 1, Qt::NoModifier);
        fill.pointerRelease(QPointF(80, 80), Qt::NoModifier);
        fill.pointerPress(QPointF(50, 50), 1, Qt::ControlModifier);
        fill.pointerMove(QPointF(170, 50), Qt::ControlModifier);
        const auto preview = fill.selectionPreview();
        check(preview.opacity == 35 && preview.placementOrigin == QPointF(20, 20), "fill preview options and destination");
        check(preview.bitmap.pixelColor(30, 30).blue() > preview.bitmap.pixelColor(30, 30).red(), "fill preview source color");
        fill.pointerRelease(QPointF(170, 50), Qt::ControlModifier);
        const auto final = fill.document().floatingSelection(0);
        check(final.opacity == 35 && final.healMode == mode, "fill final options");
        check(mode != 2 || final.bitmap.pixelColor(30, 30).red() > final.bitmap.pixelColor(30, 30).blue(), "fill healed to destination");
        fill.undo(); check(fill.document().floatingSelections().isEmpty(), "fill creates one history entry");
        fill.redo(); check(fill.document().floatingSelection(0) == final, "fill redo restores options");
        fill.copySelection(); fill.pasteSelection();
        check(fill.document().floatingSelection(1) == final, "copy paste preserves selection options");
    }
    PerspectivePlane left(
        {{QPointF(0, 0), QPointF(120, 0), QPointF(120, 120), QPointF(0, 120)}},
        {{QPointF(0, 0), QPointF(120, 0), QPointF(120, 120), QPointF(0, 120)}});
    PerspectivePlane right(
        {{QPointF(120, 0), QPointF(220, 15), QPointF(230, 105), QPointF(120, 120)}},
        {{QPointF(120, 0), QPointF(240, 0), QPointF(240, 120), QPointF(120, 120)}});
    left.setSurfaceGroupId(7); right.setSurfaceGroupId(7);
    MarqueeTool joined;
    joined.setFeather(10); joined.beginCreate({left, right}, QPointF(80, 30));
    QPointF end; right.quad().surfaceToCanvasTransform().mapForward(QPointF(170, 90), &end);
    joined.update(end, Qt::NoModifier, 50); joined.end();
    QImage uniform(size, QImage::Format_ARGB32_Premultiplied); uniform.fill(Qt::white);
    const auto across = joined.copy(uniform);
    const int seamX = qRound(120 - across.placementOrigin.x());
    check(qAlpha(across.bitmap.pixel(seamX - 1, 50)) == 255
          && qAlpha(across.bitmap.pixel(seamX, 50)) == 255, "feather has no internal cross-plane seam");
    VpController adjustable;
    adjustable.document().setImage(source); adjustable.document().appendPlane(plane);
    adjustable.document().addFloatingSelection(soft); adjustable.setTool(VpTools::Marquee);
    check(adjustable.marqueeFeather()==10, "selecting floating image synchronizes feather");
    const auto originalRect = soft.placementRect();
    adjustable.setMarqueeFeather(0);
    const auto noFeather = adjustable.document().floatingSelection(0);
    check(qAlpha(noFeather.bitmap.pixel(20,50))==255, "zero feather restores original edge pixels");
    adjustable.setMarqueeFeather(50);
    const auto feathered = adjustable.document().floatingSelection(0);
    check(qAlpha(feathered.bitmap.pixel(20,50))<qAlpha(noFeather.bitmap.pixel(20,50)), "floating feather change softens edge");
    check(feathered.placementRect()==originalRect && feathered.scaleFactors==soft.scaleFactors
          && feathered.rotationDegrees==soft.rotationDegrees, "feather leaves bounds and transform unchanged");
    adjustable.setMarqueeFeather(0);
    check(adjustable.document().floatingSelection(0).bitmap==noFeather.bitmap, "feather adjustment is reversible without cumulative alpha loss");
    adjustable.undo();
    check(adjustable.marqueeFeather()==50 && adjustable.document().floatingSelection(0)==feathered, "feather undo restores pixels and option");
    adjustable.redo(); check(adjustable.document().floatingSelection(0).bitmap==noFeather.bitmap, "feather redo restores pixels");
    adjustable.setMarqueeHealMode(2);
    adjustable.setMarqueeFeather(10);
    adjustable.setMarqueeHealMode(0);
    FloatingSelection expectedFeather = soft; expectedFeather.healMode=0; expectedFeather.setFeather(10);
    check(adjustable.document().floatingSelection(0).bitmap==expectedFeather.bitmap,
          "feather regenerates unhealed pixels before healing and off restores them");
    adjustable.setTool(VpTools::Transform); adjustable.setFlippedHorizontally(true);
    adjustable.setTool(VpTools::Marquee); adjustable.setMarqueeFeather(0);
    check(adjustable.document().floatingSelection(0).bitmap==noFeather.bitmap.mirrored(true,false),
          "feather reset preserves horizontal flip");
    FloatingSelection imported; imported.bitmap=QImage(30,30,QImage::Format_ARGB32_Premultiplied);
    imported.bitmap.fill(QColor(90,120,180,120));
    const QImage importedOriginal=imported.bitmap;
    imported.setFeather(10); imported.setFeather(0);
    check(imported.bitmap==importedOriginal, "imported image retains original transparency after feather reset");
}

void MarqueeToolTest::sourceAndDestinationMoveModes()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    QImage source(400,200,QImage::Format_ARGB32_Premultiplied);
    for(int y=0;y<200;++y) for(int x=0;x<400;++x)
        source.setPixelColor(x,y,x<100 ? Qt::red : x<200 ? Qt::green : Qt::blue);
    VpController controller; controller.document().setImage(source);
    const PerspectiveQuad::Corners left{{QPointF(0,0),QPointF(200,0),QPointF(200,200),QPointF(0,200)}};
    const PerspectiveQuad::Corners right{{QPointF(200,0),QPointF(400,0),QPointF(400,200),QPointF(200,200)}};
    PerspectivePlane a(left,left),b(right,right); a.setSurfaceGroupId(9);b.setSurfaceGroupId(9);
    controller.document().appendPlane(a);controller.document().appendPlane(b);controller.document().commitHistory();
    controller.setTool(VpTools::Marquee);controller.setMarqueeFeather(0);
    controller.setMarqueeHealMode(0); // 原始采样像素断言需要关闭修复。
    check(!controller.marqueeMoveModeEnabled() && controller.marqueeMoveMode()==0,"no selection disables mode with destination default");
    const auto select=[&](const QPointF &a,const QPointF &b) {
        controller.pointerPress(a,1,Qt::NoModifier);controller.pointerRelease(b,Qt::NoModifier);
    };
    select(QPointF(20,20),QPointF(60,60));
    check(controller.marqueeMoveModeEnabled(),"ordinary selection enables mode");
    check(controller.interactionCursorResource(QPointF(40,40),1)=="cursor_selection_move.svg",
          "ordinary marquee interior uses move cursor");
    check(controller.interactionCursorResource(QPointF(100,100),1)=="image_precinct.svg",
          "outside ordinary marquee uses existing creation crosshair");
    controller.pointerPress(QPointF(40,40),1,Qt::NoModifier);controller.pointerRelease(QPointF(70,40),Qt::NoModifier);
    check(controller.selectionOutline().boundingRect()==QRectF(50,20,40,40),"destination drag moves marquee only");
    check(qAlpha(controller.document().paintLayer().pixel(60,40))==0,"destination mode does not copy pixels");
    select(QPointF(20,20),QPointF(60,60));
    const auto outline=controller.selectionOutline();
    controller.setMarqueeMoveMode(1);
    controller.pointerPress(QPointF(40,40),1,Qt::NoModifier);
    check(!controller.marqueeMoveModeEnabled(),"source mode locks combo at drag start");
    controller.setMarqueeMoveMode(0);
    check(controller.marqueeMoveMode()==1,"locked combo cannot switch mode");
    controller.pointerMove(QPointF(140,40),Qt::NoModifier);
    check(controller.selectionOutline()==outline && qAlpha(controller.document().paintLayer().pixel(30,40))==0,
          "source preview preserves destination and waits until release to write");
    controller.pointerRelease(QPointF(140,40),Qt::NoModifier);
    check(controller.document().paintLayer().pixelColor(30,40)==QColor(Qt::green),"source release writes sampled pixels");
    check(controller.document().floatingSelections().isEmpty() && controller.selectionOutline()==outline
          && !controller.marqueeMoveModeEnabled(),"release retains fixed marquee with disabled mode and no floating object");
    const QImage firstFill = controller.document().paintLayer();
    controller.setMarqueeOpacity(50);
    const QImage halfFill = controller.document().paintLayer();
    check(qAlpha(halfFill.pixel(30,40))>=126 && qAlpha(halfFill.pixel(30,40))<=128,
          "source opacity updates completed fill");
    controller.setMarqueeOpacity(100); controller.setMarqueeOpacity(50);
    check(controller.document().paintLayer()==halfFill, "source opacity is reapplied without accumulation");
    controller.setMarqueeOpacity(0);
    check(qAlpha(controller.document().paintLayer().pixel(30,40))==0, "zero source opacity restores pre-fill pixels");
    controller.setMarqueeOpacity(100);
    check(controller.document().paintLayer()==firstFill, "full source opacity restores original sample");
    controller.setMarqueeFeather(10);
    check(qAlpha(controller.document().paintLayer().pixel(20,40))<255, "source feather adjusts completed fill edge");
    controller.setMarqueeFeather(0);
    check(controller.document().paintLayer()==firstFill, "source feather reset restores original alpha");
    controller.setMarqueeHealMode(2);
    check(controller.document().paintLayer()!=firstFill, "source heal adjusts completed fill");
    controller.setMarqueeHealMode(0);
    check(controller.document().paintLayer()==firstFill, "source heal off restores original sampled pixels");
    controller.pointerPress(QPointF(40,40),1,Qt::NoModifier);controller.pointerRelease(QPointF(60,40),Qt::NoModifier);
    check(controller.document().paintLayer().pixelColor(30,40)==QColor(Qt::green)
          && controller.document().paintLayer().pixelColor(50,40)==QColor(Qt::red),
          "next source drag samples previous committed content");
    const QImage secondFill = controller.document().paintLayer();
    controller.setMarqueeOpacity(0);
    check(controller.document().paintLayer()==firstFill, "changing latest fill preserves previous completed drag");
    controller.setMarqueeOpacity(100);
    check(controller.document().paintLayer()==secondFill, "latest source sample is reusable after parameter reset");
    controller.pointerPress(QPointF(40,40),1,Qt::NoModifier);controller.pointerRelease(QPointF(240,40),Qt::NoModifier);
    check(controller.document().paintLayer().pixelColor(30,40)==QColor(Qt::blue),"repeated source drag takes new sample");
    controller.undo();
    check(controller.document().paintLayer().pixelColor(30,40)==QColor(Qt::green)
          && controller.document().paintLayer().pixelColor(50,40)==QColor(Qt::red),"each source drag is one undo step");
    controller.redo();check(controller.document().paintLayer().pixelColor(30,40)==QColor(Qt::blue),"redo restores source fill");
    select(QPointF(180,20),QPointF(220,60));
    check(controller.marqueeMoveModeEnabled() && controller.marqueeMoveMode()==0,"new selection resets to enabled destination");
    controller.setMarqueeMoveMode(1);
    controller.pointerPress(QPointF(200,40),1,Qt::NoModifier);controller.pointerRelease(QPointF(300,40),Qt::NoModifier);
    check(controller.document().paintLayer().pixelColor(185,40)==QColor(Qt::blue)
          && controller.document().paintLayer().pixelColor(215,40)==QColor(Qt::blue),"source fill supports selection across plane seam");
    select(QPointF(280,100),QPointF(320,140));
    FloatingSelection floating;floating.bitmap=QImage(10,10,QImage::Format_ARGB32_Premultiplied);floating.bitmap.fill(Qt::white);
    controller.document().addFloatingSelection(floating);
    check(!controller.marqueeMoveModeEnabled(),"floating selection does not enable move mode");
}

void MarqueeToolTest::marqueePersistsAndClipsPaintingAcrossTools()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    const auto setup = [](VpController &controller) {
        QImage image(200,200,QImage::Format_ARGB32_Premultiplied);
        for (int y=0;y<200;++y) for(int x=0;x<200;++x)
            image.setPixelColor(x,y,QColor(x<50 ? 230 : 30,60+y/2,90));
        controller.document().setImage(image);
        for (int x : {0,100}) {
            const PerspectiveQuad::Corners corners{{QPointF(x,0),QPointF(x+100,0),
                                                    QPointF(x+100,200),QPointF(x,200)}};
            PerspectivePlane plane(corners,corners); plane.setSurfaceGroupId(7);
            controller.document().appendPlane(plane);
        }
        controller.document().resetHistory();
        controller.setTool(VpTools::Marquee);
        controller.pointerPress(QPointF(60,60),1,Qt::NoModifier);
        controller.pointerRelease(QPointF(140,140),Qt::NoModifier);
    };
    const auto unchangedOutside = [](const QImage &before, const QImage &after, const QPainterPath &clip) {
        // Include Qt's antialiased clip coverage at polygon edges rather than
        // assuming a pixel-center containment rule shared by Qt 5 and Qt 6.
        QImage mask(after.size(), QImage::Format_ARGB32_Premultiplied);
        mask.fill(Qt::transparent);
        { QPainter painter(&mask); painter.setRenderHint(QPainter::Antialiasing); painter.setClipPath(clip); painter.fillRect(mask.rect(), Qt::white); }
        for(int y=0;y<after.height();++y) for(int x=0;x<after.width();++x)
            if (qAlpha(mask.pixel(x,y)) == 0 && after.pixel(x,y)!=before.pixel(x,y)) return false;
        return true;
    };
    for (const bool clone : {false,true}) for (int heal : {0,1,2}) {
        VpController controller; setup(controller);
        const QPainterPath selection=controller.selectionOutline();
        check(selection.contains(QPointF(80,100)) && selection.contains(QPointF(120,100)),
              "marquee spans both linked planes");
        controller.setTool(clone ? VpTools::CloneStamp : VpTools::Brush);
        if (clone) {
            controller.setCloneDiameter(100); controller.setCloneHardness(100);
            controller.setCloneOpacity(70); controller.setCloneHealMode(heal);
            check(controller.pickCloneSource(QPointF(30,30)),"clone source may be outside selection");
        } else {
            controller.setBrushDiameter(100); controller.setBrushHardness(100);
            controller.setBrushOpacity(70); controller.setBrushColor(Qt::magenta);
            controller.setBrushHealMode(heal);
        }
        check(controller.selectionOutline()==selection,"tool and parameter changes retain marquee");
        QImage preview(200,200,QImage::Format_ARGB32_Premultiplied); preview.fill(Qt::transparent);
        { QPainter painter(&preview);
          if(clone) controller.renderClonePreview(painter,QPointF(60,100));
          else controller.renderBrushPreview(painter,QPointF(60,100)); }
        QImage empty(preview.size(),preview.format()); empty.fill(Qt::transparent);
        check(unchangedOutside(empty,preview,selection) && preview!=empty,
              "brush and clone previews are clipped without disappearing");
        // Existing paint outside the marquee must survive both normal painting and healing replay.
        controller.document().paintLayer().fill(QColor(10,20,30,40));
        const QImage before=controller.document().paintLayer();
        controller.pointerPress(QPointF(30,100),1,Qt::NoModifier);
        controller.pointerMove(QPointF(100,100),Qt::NoModifier);
        check(unchangedOutside(before,controller.document().paintLayer(),selection),
              "live stroke leaves all outside pixels unchanged");
        controller.pointerRelease(QPointF(170,100),Qt::NoModifier);
        const QImage painted=controller.document().paintLayer();
        check(painted!=before && unchangedOutside(before,painted,selection),
              "completed and healed strokes paint only inside the marquee");
        check(controller.selectionOutline()==selection,"stroke release retains marquee");
        controller.setTool(VpTools::Hand); controller.setTool(VpTools::Zoom);
        controller.setTool(VpTools::Marquee);
        check(controller.selectionOutline()==selection,"navigation tool round trip retains marquee");
        controller.undo(); check(controller.document().paintLayer()==before,"clipped stroke is one undo step");
        controller.redo(); check(controller.document().paintLayer()==painted,"redo restores clipped stroke");
    }
    for (const bool clone : {false,true}) {
        VpController perspective;
        QImage image(200,200,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::cyan);
        perspective.document().setImage(image);
        const PerspectiveQuad::Corners surface{{QPointF(0,0),QPointF(200,0),QPointF(200,200),QPointF(0,200)}};
        const QTransform transform(1,.15,.0008, .2,.9,.0004, 10,8,1);
        auto corners=surface; for(auto &point:corners) point=transform.map(point);
        perspective.document().appendPlane(PerspectivePlane(corners,surface));
        perspective.setTool(VpTools::Marquee);
        perspective.pointerPress(transform.map(QPointF(50,50)),1,Qt::NoModifier);
        perspective.pointerRelease(transform.map(QPointF(130,130)),Qt::NoModifier);
        const auto clip=perspective.selectionOutline();
        perspective.setTool(clone ? VpTools::CloneStamp : VpTools::Brush);
        if(clone) {
            perspective.setCloneDiameter(100); perspective.setCloneHealMode(2);
            perspective.pickCloneSource(transform.map(QPointF(20,20)));
        } else {
            perspective.setBrushDiameter(100); perspective.setBrushHealMode(2);
        }
        const QImage before=perspective.document().paintLayer();
        perspective.pointerPress(transform.map(QPointF(40,90)),1,Qt::NoModifier);
        perspective.pointerRelease(transform.map(QPointF(140,90)),Qt::NoModifier);
        check(perspective.document().paintLayer()!=before
                  && unchangedOutside(before,perspective.document().paintLayer(),clip),
              "perspective strokes respect polygon outline rather than bounding rectangle");
    }
    VpController controller; setup(controller);
    const QPainterPath selection=controller.selectionOutline();
    VpCanvas canvas; canvas.setSize(QSizeF(120,120)); canvas.setController(&controller);
    for (const auto tool : {VpTools::EditPlane,VpTools::Brush,VpTools::CloneStamp,VpTools::Hand,
                           VpTools::Zoom,VpTools::Eyedropper,VpTools::CreatePlane,VpTools::Marquee}) {
        controller.document().setSelectedPlane(0);
        controller.setTool(tool);
        const bool clearsPlane=tool==VpTools::Marquee || tool==VpTools::Brush || tool==VpTools::CloneStamp;
        check(controller.document().selectedPlane()==(clearsPlane ? -1 : 0),
              "marquee brush and clone deselect plane while other tools retain selection");
        check(controller.document().planes().size()==2,"deselecting a plane preserves its geometry");
        check(controller.selectionOutline()==selection,"switching tools preserves completed marquee");
    }
    canvas.setZoom(2); canvas.scrollTo(.4,.3); canvas.zoomStep(false); canvas.fitView();
    check(controller.selectionOutline()==selection,"zoom pan and fit preserve selection in image coordinates");
    controller.setMarqueeFeather(12); controller.setMarqueeOpacity(40); controller.setMarqueeHealMode(2);
    check(controller.selectionOutline()==selection,"marquee options retain geometry");
    FloatingSelection floating; floating.bitmap=QImage(8,8,QImage::Format_ARGB32_Premultiplied);
    floating.bitmap.fill(Qt::red); floating.placementOrigin=QPointF(180,180);
    controller.document().addFloatingSelection(floating);
    controller.setTool(VpTools::Transform); controller.setFlippedHorizontally(true);
    check(controller.selectionOutline()==selection,"transform tool and parameters preserve ordinary marquee");
    controller.clearSelection();
    check(controller.selectionOutline().isEmpty() && controller.document().floatingSelections().size()==1,
          "Ctrl+D still clears only ordinary marquee");
    controller.document().setSelectedFloatingSelection(-1);
    controller.setTool(VpTools::Brush);
    controller.pointerPress(QPointF(30,30),1,Qt::NoModifier);
    controller.pointerRelease(QPointF(30,30),Qt::NoModifier);
    check(qAlpha(controller.document().paintLayer().pixel(30,30))>0,"clearing selection restores unrestricted painting");
}

void MarqueeToolTest::createsInEveryDirectionAndRejectsSubpixelSize()
{
    const auto surface = identityPlane().quad().surfaceCorners();
    const QTransform transform(1,.15,.0008, .2,.9,.0004, 10,8,1);
    auto canvas = surface;
    for (auto &point : canvas) point = transform.map(point);
    const PerspectivePlane plane(canvas,surface);
    for (int x : {-1,1}) for (int y : {-1,1}) for (bool shift : {false,true}) {
        MarqueeTool tool;
        QVERIFY(tool.beginCreate({plane},transform.map(QPointF(80,80))));
        QCOMPARE(tool.action(),MarqueeTool::Action::Create);
        tool.update(transform.map(QPointF(80+x*20.0,80+y*10.0)),
                    shift ? Qt::ShiftModifier : Qt::NoModifier,10);
        const QRectF expected(QPointF(80,80),QPointF(80+x*20,80+y*(shift?20:10)));
        QVERIFY(QLineF(tool.rect().topLeft(),expected.normalized().topLeft()).length()<1e-6);
        QVERIFY(qAbs(tool.rect().width()-20)<1e-6);
        QVERIFY(qAbs(tool.rect().height()-(shift?20:10))<1e-6);
        tool.end(); QVERIFY(!tool.active());
    }
    for (qreal width : {.999,1.0,1.001}) {
        MarqueeTool tool;
        QVERIFY(tool.beginCreate({identityPlane()},{20,20}));
        tool.update({20+width,22},Qt::NoModifier,1); tool.end();
        QCOMPARE(tool.rect().isEmpty(),width<1);
    }
}

void MarqueeToolTest::movementSnapsFromPressAndAllowsModifierChanges()
{
    MarqueeTool tool;
    QVERIFY(tool.beginCreate({identityPlane()},{40,40}));
    tool.update({80,80},Qt::NoModifier,10); tool.end();
    QVERIFY(tool.beginMove({60,60}));
    QCOMPARE(tool.action(),MarqueeTool::Action::Move);
    struct Step { QPointF delta; int grid; QPointF snapped; };
    // Qt 5 qRound resolves exact half-grid ties toward the positive grid point.
    const Step steps[] = {{{15,3},10,{20,0}},{{-15,-3},10,{-10,0}},
                          {{3,15},10,{0,20}},{{-3,-15},10,{0,-10}},
                          {{15,15},10,{20,0}},{{-15,-15},10,{-10,0}},
                          {{-16,-3},10,{-20,0}},{{-3,-16},10,{0,-20}},
                          {{2.6,1},0,{3,0}},{{1,2.6},-10,{0,3}}};
    for (const auto &step : steps) {
        tool.update(QPointF(60,60)+step.delta,Qt::ShiftModifier,step.grid);
        QCOMPARE(tool.rect(),QRectF(QPointF(40,40)+step.snapped,QSizeF(40,40)));
    }
    tool.update({75,63},Qt::NoModifier,10);
    QCOMPARE(tool.rect(),QRectF(55,43,40,40));
    tool.update({75,63},Qt::ShiftModifier,10);
    QCOMPARE(tool.rect(),QRectF(60,40,40,40));
    tool.update({60,60},Qt::NoModifier,10);
    QCOMPARE(tool.rect(),QRectF(40,40,40,40));
}

void MarqueeToolTest::actionLifecycleAndOutlineCache()
{
    MarqueeTool tool;
    QCOMPARE(tool.action(),MarqueeTool::Action::None);
    QVERIFY(!tool.beginMove({30,30}));
    QVERIFY(tool.beginCreate({identityPlane()},{20,20}));
    tool.update({60,60},Qt::NoModifier,1); tool.end();
    const auto outline=tool.outline(); const auto builds=tool.outlineBuildCount();
    QVERIFY(tool.contains({40,40})); QCOMPARE(tool.outline(),outline);
    QCOMPARE(tool.outlineBuildCount(),builds);
    QImage source(200,200,QImage::Format_ARGB32_Premultiplied); source.fill(Qt::red);
    QVERIFY(!tool.beginFill({40,40},{},source));
    QVERIFY(!tool.beginFill({40,40},source,{}));
    QVERIFY(!tool.beginMove({100,100}));
    QCOMPARE(tool.action(),MarqueeTool::Action::None); QCOMPARE(tool.outline(),outline);
    QVERIFY(tool.beginMove({40,40})); tool.update({50,40},Qt::NoModifier,1);
    QVERIFY(tool.outline()!=outline); QCOMPARE(tool.outlineBuildCount(),builds+1);
    QVERIFY(tool.contains({50,40})); QCOMPARE(tool.outlineBuildCount(),builds+1);
    tool.end(); QVERIFY(tool.beginFill({50,40},source,source));
    QCOMPARE(tool.action(),MarqueeTool::Action::Fill);
    QVERIFY(!tool.clone().bitmap.isNull()); tool.end();
    QVERIFY(tool.clone().bitmap.isNull()); QVERIFY(!tool.copy(source).bitmap.isNull());
    tool.setFeather(7); tool.setOpacity(42); tool.setHealMode(1); tool.clear();
    QVERIFY(tool.outline().isEmpty()); QVERIFY(tool.rect().isEmpty());
    QCOMPARE(tool.action(),MarqueeTool::Action::None);
    QCOMPARE(tool.feather(),7); QCOMPARE(tool.opacity(),42); QCOMPARE(tool.healMode(),1);
    QVERIFY(tool.beginCreate({identityPlane()},{30,30}));
    tool.update({70,70},Qt::NoModifier,1); tool.end();
    QCOMPARE(tool.outline().boundingRect(),QRectF(30,30,40,40));
    QVERIFY(!tool.beginCreate({}, {40,40})); QVERIFY(tool.outline().isEmpty());
}

void MarqueeToolTest::filtersFacesAndRetainsDeletedGeometry()
{
    auto planes=connectedPlanes();
    auto unrelated=identityPlane(); unrelated.setSurfaceGroupId(99);
    auto invalid=identityPlane(); invalid.setSurfaceGroupId(99); invalid.quad().setSurfaceCorners({});
    planes.append(unrelated); planes.append(invalid);
    MarqueeTool tool; tool.setFeather(0);
    // A nonprojectable topmost hit cannot supply a selection mapping.
    QVERIFY(!tool.beginCreate(planes,{40,40}));
    planes.swapItemsAt(2,3);
    // An overlapping topmost valid face selects its own surface group.
    QVERIFY(tool.beginCreate(planes,{40,40}));
    tool.update({60,60},Qt::NoModifier,1); tool.end();
    QImage source(260,200,QImage::Format_ARGB32_Premultiplied); source.fill(Qt::red);
    QCOMPARE(tool.copy(source).surfaceQuads.size(),1);
    planes.remove(2,2);
    createAcrossSeam(tool,planes);
    const auto outline=tool.outline();
    const auto extracted=tool.copy(source,canvasPoint(planes[1],{130,30}));
    QCOMPARE(extracted.surfaceQuads.size(),2); QCOMPARE(extracted.hostQuadIndex,1);
    planes.clear();
    QCOMPARE(tool.outline(),outline);
    QCOMPARE(tool.copy(source).bitmap,extracted.bitmap);
    QVERIFY(tool.contains({100,40}));
}

void MarqueeToolTest::fractionalPreviewPreservesPlacement_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::newRow("third") << qreal(.3);
    QTest::newRow("two-thirds") << qreal(.67);
    QTest::newRow("full") << qreal(1);
}

void MarqueeToolTest::fractionalPreviewPreservesPlacement()
{
    QFETCH(qreal,scale);
    MarqueeTool tool; tool.setFeather(3);
    QVERIFY(tool.beginCreate({identityPlane()},{20.25,25.75}));
    tool.update({63.5,57.25},Qt::NoModifier,1); tool.end();
    QImage source(200,200,QImage::Format_ARGB32_Premultiplied); source.fill(Qt::red);
    QVERIFY(tool.beginFill({40,40},source,source));
    tool.update({57,48},Qt::NoModifier,1);
    const auto full=tool.clone(); const auto preview=tool.clone(scale);
    QVERIFY(!preview.bitmap.isNull());
    QCOMPARE(preview.bitmap.size(),QSize(qCeil(full.bitmap.width()*scale),qCeil(full.bitmap.height()*scale)));
    QVERIFY(QLineF(preview.placementRect().topLeft(),full.placementRect().topLeft()).length()<1e-6);
    QVERIFY(qAbs(preview.displaySize().width()-full.displaySize().width())<1e-6);
    QVERIFY(qAbs(preview.displaySize().height()-full.displaySize().height())<1e-6);
    const auto expected=full.transformHandlePositions(),actual=preview.transformHandlePositions();
    QCOMPARE(actual.size(),expected.size());
    for (int i=0;i<actual.size();++i) QVERIFY(QLineF(actual[i],expected[i]).length()<1e-6);
    QCOMPARE(tool.clone().bitmap.size(),full.bitmap.size());
}

void MarqueeToolTest::extractionClipsSourceAndPreservesAlpha()
{
    QImage source(50,50,QImage::Format_ARGB32_Premultiplied); source.fill(QColor(60,120,180,128));
    const QImage original=source.copy();
    MarqueeTool tool; tool.setFeather(0); tool.setHealMode(0);
    QVERIFY(tool.beginCreate({identityPlane()},{30,30}));
    tool.update({70,70},Qt::NoModifier,1); tool.end();
    const auto partial=tool.copy(source);
    QCOMPARE(partial.bitmap.size(),QSize(40,40));
    QCOMPARE(partial.bitmap.format(),QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(partial.bitmap.pixel(5,5),source.pixel(35,35));
    QCOMPARE(qAlpha(partial.bitmap.pixel(30,30)),0);
    QVERIFY(tool.beginFill({40,40},source,source));
    tool.update({140,140},Qt::NoModifier,1);
    const auto outside=tool.clone(); QVERIFY(!outside.bitmap.isNull());
    QImage empty(outside.bitmap.size(),outside.bitmap.format()); empty.fill(Qt::transparent);
    QCOMPARE(outside.bitmap,empty); QCOMPARE(source,original);
    source.fill(Qt::transparent);
    QCOMPARE(tool.copy(source).bitmap,empty);
    tool.update({40,40},Qt::NoModifier,1);
    QCOMPARE(tool.clone().bitmap,partial.bitmap); // Fill owns a snapshot even when the caller replaces source pixels.
    tool.end();
    QVERIFY(tool.beginCreate({identityPlane()},{70,70}));
    tool.update({110,110},Qt::NoModifier,1); tool.end();
    QCOMPARE(tool.copy(original).bitmap,empty);
    QVERIFY(tool.copy({}).bitmap.isNull());
}

void MarqueeToolTest::extractionLimits_data()
{
    QTest::addColumn<QSizeF>("size"); QTest::addColumn<int>("feather"); QTest::addColumn<bool>("accepted");
    QTest::newRow("side-at-limit") << QSizeF(8192,2) << 0 << true;
    QTest::newRow("width-over-limit") << QSizeF(8192.1,2) << 0 << false;
    QTest::newRow("height-over-limit") << QSizeF(2,8192.1) << 0 << false;
    QTest::newRow("bytes-at-limit") << QSizeF(8192,2048) << 0 << true;
    QTest::newRow("bytes-over-limit") << QSizeF(8192,2048.1) << 0 << false;
    QTest::newRow("feather-at-limit") << QSizeF(8188,2) << 1 << true;
    QTest::newRow("feather-over-limit") << QSizeF(8189,2) << 1 << false;
    QTest::newRow("feather-exceeds-byte-limit") << QSizeF(4093,4092) << 1 << false;
    QTest::newRow("oversized-both-sides") << QSizeF(9800,9800) << 1 << false;
}

void MarqueeToolTest::extractionLimits()
{
    QFETCH(QSizeF,size); QFETCH(int,feather); QFETCH(bool,accepted);
    MarqueeTool tool; tool.setFeather(feather);
    QVERIFY(tool.beginCreate({identityPlane(12000)},{10,10}));
    tool.update(QPointF(10,10)+QPointF(size.width(),size.height()),Qt::NoModifier,1); tool.end();
    QImage source(32,32,QImage::Format_ARGB32_Premultiplied); source.fill(Qt::red);
    { const auto copy=tool.copy(source); QCOMPARE(!copy.bitmap.isNull(),accepted);
      if (accepted) QCOMPARE(copy.bitmap.size(),QSize(qCeil(size.width()+4*feather),qCeil(size.height()+4*feather))); }
    QVERIFY(tool.beginFill({11,11},source,source));
    QCOMPARE(!tool.clone(.01).bitmap.isNull(),accepted); // Preview scaling cannot bypass full extraction limits.
}

void MarqueeToolTest::interruptedInteractions_data()
{
    QTest::addColumn<int>("action"); QTest::addColumn<int>("interrupt");
    for(int action=0;action<4;++action) for(int interrupt=0;interrupt<3;++interrupt)
        QTest::newRow(qPrintable(QString("action-%1-interrupt-%2").arg(action).arg(interrupt))) << action << interrupt;
}

void MarqueeToolTest::interruptedInteractions()
{
    QFETCH(int,action); QFETCH(int,interrupt);
    VpController controller; setupMarquee(controller); auto &doc=controller.document();
    const QImage before=doc.paintLayer();
    if(action) selectRect(controller);
    if(action==3) controller.setMarqueeMoveMode(1);
    const auto modifiers=action==2 ? Qt::ControlModifier : Qt::NoModifier;
    controller.pointerPress(action ? QPointF(40,40) : QPointF(20,20),1,modifiers);
    controller.pointerMove({140,40},modifiers);
    if(action>=2) { QVERIFY(doc.editActive()); QVERIFY(!controller.selectionPreview().bitmap.isNull()); }
    if(interrupt==0) controller.cancelInteraction();
    else if(interrupt==1) controller.setTool(VpTools::Hand);
    else controller.undo();
    QVERIFY(!doc.editActive()); QVERIFY(controller.selectionPreview().bitmap.isNull());
    QVERIFY(controller.selectionOutline().isEmpty()); QCOMPARE(doc.paintLayer(),before);
    QVERIFY(doc.floatingSelections().isEmpty()); QVERIFY(!doc.canUndo());
    controller.setTool(VpTools::Marquee); selectRect(controller);
    QCOMPARE(controller.selectionOutline().boundingRect(),QRectF(20,20,40,40));
    QVERIFY(controller.marqueeMoveModeEnabled());
}

void MarqueeToolTest::historyBoundaries_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("ordinary") << 0; QTest::newRow("alt-copy") << 1;
    QTest::newRow("ctrl-fill") << 2; QTest::newRow("source-fill") << 3;
}

void MarqueeToolTest::historyBoundaries()
{
    QFETCH(int,mode);
    VpController controller; setupMarquee(controller); auto &doc=controller.document();
    const QImage before=doc.paintLayer(); selectRect(controller); QVERIFY(!doc.canUndo());
    if(mode==3) controller.setMarqueeMoveMode(1);
    const auto modifiers=mode==1 ? Qt::AltModifier : mode==2 ? Qt::ControlModifier : Qt::NoModifier;
    controller.pointerPress({40,40},1,modifiers);
    // Alt copies at the press; a stationary release isolates that single history entry.
    controller.pointerRelease(mode==1 ? QPointF(40,40) : QPointF(140,40),modifiers);
    const QImage after=doc.paintLayer();
    if(mode==0) { QCOMPARE(after,before); QVERIFY(!doc.canUndo()); return; }
    const int floatingCount=mode==3 ? 0 : 1;
    QCOMPARE(doc.floatingSelections().size(),floatingCount);
    const auto bitmap= floatingCount ? doc.floatingSelection(0).bitmap : QImage();
    const auto origin= floatingCount ? doc.floatingSelection(0).placementOrigin : QPointF();
    QVERIFY(doc.canUndo()); controller.undo();
    QCOMPARE(doc.paintLayer(),before); QVERIFY(doc.floatingSelections().isEmpty());
    QVERIFY(!doc.canUndo()); QVERIFY(doc.canRedo());
    // Geometry-only and failed gestures must leave the redo branch intact.
    selectRect(controller);
    controller.pointerPress({40,40},1,Qt::NoModifier); controller.pointerRelease({45,40},Qt::NoModifier);
    controller.pointerPress({250,250},1,Qt::NoModifier); controller.pointerRelease({260,260},Qt::NoModifier);
    QVERIFY(doc.canRedo()); controller.redo();
    QCOMPARE(doc.paintLayer(),after); QCOMPARE(doc.floatingSelections().size(),floatingCount);
    if(floatingCount) {
        QCOMPARE(doc.floatingSelection(0).bitmap,bitmap);
        QCOMPARE(doc.floatingSelection(0).placementOrigin,origin);
    }
    QVERIFY(!doc.canRedo());
}

void MarqueeToolTest::sourceFillCacheInvalidation_data()
{
    QTest::addColumn<int>("change");
    QTest::newRow("paint") << 0; QTest::newRow("undo-redo") << 1;
    QTest::newRow("new-selection") << 2; QTest::newRow("clear-selection") << 3;
}

void MarqueeToolTest::sourceFillCacheInvalidation()
{
    QFETCH(int,change);
    VpController controller; setupMarquee(controller); selectRect(controller);
    controller.setMarqueeMoveMode(1);
    controller.pointerPress({40,40},1,Qt::NoModifier); controller.pointerRelease({140,40},Qt::NoModifier);
    auto &doc=controller.document(); QVERIFY(doc.hasPaintContent());
    if(change==0) {
        controller.setTool(VpTools::Brush); controller.setBrushHealMode(0); controller.setBrushColor(Qt::blue);
        controller.pointerPress({40,40},1,Qt::NoModifier); controller.pointerRelease({40,40},Qt::NoModifier);
        controller.setTool(VpTools::Marquee);
        QVERIFY(doc.paintLayer().pixelColor(40,40)!=QColor(Qt::green));
    } else if(change==1) { controller.undo(); controller.redo(); selectRect(controller); }
    else if(change==2) {
        controller.pointerPress({70,70},1,Qt::NoModifier);
        controller.pointerRelease({90,90},Qt::NoModifier);
        QCOMPARE(controller.selectionOutline().boundingRect(),QRectF(70,70,20,20));
    }
    else controller.clearSelection();
    const QImage before=doc.paintLayer();
    controller.setMarqueeOpacity(25); controller.setMarqueeFeather(5); controller.setMarqueeHealMode(2);
    QCOMPARE(doc.paintLayer(),before); QVERIFY(!doc.editActive());
}

void MarqueeToolTest::pasteCancelsFillPreview()
{
    VpController controller; setupMarquee(controller); selectRect(controller); auto &doc=controller.document();
    const auto corners=doc.planes()[0].quad().canvasCorners();
    controller.pointerPress({40,40},1,Qt::ControlModifier); controller.pointerMove({45,40},Qt::ControlModifier);
    QVERIFY(!controller.selectionPreview().bitmap.isNull()); QVERIFY(!doc.hasPaintContent());
    QImage image(20,20,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::blue);
    QGuiApplication::clipboard()->setImage(image); controller.pasteSelection();
    QVERIFY(controller.selectionPreview().bitmap.isNull()); QVERIFY(!doc.editActive()); QVERIFY(!doc.hasPaintContent());
    QVERIFY(doc.planes()[0].quad().canvasCorners()==corners);
    QCOMPARE(doc.floatingSelections().size(),1);
    controller.undo(); QVERIFY(doc.floatingSelections().isEmpty()); QVERIFY(!doc.canUndo());
    controller.redo(); QCOMPARE(doc.floatingSelections().size(),1); QCOMPARE(doc.floatingSelection(0).bitmap,image);
}

QTEST_MAIN(MarqueeToolTest)
#include "tst_marqueetool.moc"
