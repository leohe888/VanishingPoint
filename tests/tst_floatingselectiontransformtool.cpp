#include <limits>
#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QtTest>

#include "canvas/vpcanvas.h"
#include "canvas/vpcontroller.h"
#include "cursorhelpers.h"
#include "tools/floatingselectiontransformtool.h"

class FloatingSelectionTransformToolTest : public QObject
{
    Q_OBJECT
private slots:
    void clearSelectionPreservesFloatingContentAndDrag();
    void transformToolRotatesAroundCenter();
    void transformToolScalesFromCorner();
    void interactionCursorsFollowHandlesAndRotation();
    void flipsPreserveGeometryAndHistory();
};

void FloatingSelectionTransformToolTest::clearSelectionPreservesFloatingContentAndDrag()
{
    VpController controller; QImage background(400,400,QImage::Format_ARGB32); background.fill(Qt::white);
    QVERIFY(controller.document().setImage(background));
    FloatingSelection floating; floating.bitmap=QImage(20,20,QImage::Format_ARGB32_Premultiplied); floating.bitmap.fill(Qt::red);
    controller.document().addFloatingSelection(floating); controller.setTool(VpTools::Transform);
    controller.clearSelection(); QVERIFY(controller.document().floatingSelection(0)==floating);
    QCOMPARE(controller.document().selectedFloatingSelection(),0);
    controller.pointerPress({5,5},1,Qt::NoModifier); controller.clearSelection(); controller.pointerRelease({15,5},Qt::NoModifier);
    QCOMPARE(controller.document().floatingSelections().size(),1);
    QCOMPARE(controller.document().floatingSelection(0).placementOrigin,QPointF(10,0));
}

void FloatingSelectionTransformToolTest::flipsPreserveGeometryAndHistory()
{
    VpController controller;
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    QImage background(64, 64, QImage::Format_ARGB32_Premultiplied);
    background.fill(Qt::transparent);
    controller.document().setImage(background);
    FloatingSelection original;
    original.bitmap = QImage(3, 2, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            original.bitmap.setPixelColor(x, y, QColor(30 + x * 70, y * 120, 90, 180));
    original.placementOrigin = QPointF(20, 20);
    original.scaleFactors = QPointF(2, 3);
    original.rotationDegrees = 25;
    controller.document().addFloatingSelection(original);
    controller.setTool(VpTools::Transform);
    int notifications = 0;
    QObject::connect(&controller, &VpController::transformOptionsChanged,
                     [&] { ++notifications; });
    controller.setFlippedHorizontally(true);
    const auto horizontal = controller.document().floatingSelection(0);
    check(horizontal.bitmap == original.bitmap.mirrored(true, false), "horizontal pixels");
    check(horizontal.transformHandlePositions() == original.transformHandlePositions(), "geometry preserved");
    check(controller.flippedHorizontally() && !controller.flippedVertically(), "horizontal state");
    const QImage horizontalResult = controller.resultImage();
    controller.setFlippedVertically(true);
    check(controller.document().floatingSelection(0).bitmap == original.bitmap.mirrored(true, true), "combined pixels");
    check(controller.flippedHorizontally() && controller.flippedVertically(), "independent state");
    controller.undo();
    check(controller.document().floatingSelection(0) == horizontal, "undo vertical");
    check(controller.resultImage() == horizontalResult, "undo rendered result");
    controller.undo();
    check(controller.document().floatingSelection(0) == original, "undo horizontal");
    controller.redo();
    check(controller.document().floatingSelection(0) == horizontal, "redo horizontal");
    controller.setFlippedHorizontally(false);
    check(controller.document().floatingSelection(0) == original, "toggle restores original");
    controller.setFlippedHorizontally(false);
    controller.undo();
    check(controller.document().floatingSelection(0) == horizontal, "unchanged toggle adds no history");
    controller.document().addFloatingSelection(original);
    check(!controller.flippedHorizontally(), "new selection state");
    controller.document().setSelectedFloatingSelection(0);
    check(controller.flippedHorizontally(), "selection switch state");
    controller.document().setSelectedFloatingSelection(-1);
    check(!controller.transformEditable() && !controller.flippedHorizontally(), "no selection state");
    controller.setFlippedVertically(true);
    check(!controller.document().floatingSelection(0).flippedVertically, "no selection ignored");
    check(notifications > 0, "option notifications");
    FloatingSelection attached = original;
    attached.surfaceAttached = true;
    attached.hostQuadIndex = 0;
    attached.surfaceQuads.append(PerspectiveQuad(
        {{QPointF(5, 5), QPointF(60, 10), QPointF(55, 60), QPointF(10, 55)}},
        {{QPointF(0, 0), QPointF(64, 0), QPointF(64, 64), QPointF(0, 64)}}));
    controller.document().addFloatingSelection(attached);
    const auto handles = attached.transformHandlePositions();
    controller.setFlippedVertically(true);
    const auto flippedAttached = controller.document().floatingSelection(2);
    check(flippedAttached.bitmap == attached.bitmap.mirrored(false, true), "surface pixels");
    check(flippedAttached.surfaceQuads == attached.surfaceQuads
          && flippedAttached.hostQuadIndex == attached.hostQuadIndex
          && flippedAttached.transformHandlePositions() == handles, "surface geometry preserved");
    const QImage surfaceResult = controller.resultImage();
    controller.undo();
    check(controller.document().floatingSelection(2) == attached, "surface undo");
    check(controller.resultImage() != surfaceResult, "surface rendering changes");
    controller.redo();
    check(controller.resultImage() == surfaceResult, "surface redo rendering");
}

void FloatingSelectionTransformToolTest::interactionCursorsFollowHandlesAndRotation()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    VpController tools;
    QImage image(256, 256, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::white);
    tools.document().setImage(image);
    FloatingSelection selection; selection.bitmap = image.copy(0, 0, 80, 80);
    selection.placementOrigin = QPointF(80, 80);
    tools.document().addFloatingSelection(selection);
    tools.setTool(VpTools::Transform);
    const auto handles = selection.transformHandlePositions();
    const char *expected[] = {"cut_oblique_back.svg", "cut_oblique.svg", "cut_oblique_back.svg",
                              "cut_oblique.svg", "cut_vertical.svg", "cut_horizontal.svg",
                              "cut_vertical.svg", "cut_horizontal.svg"};
    for (int i = 0; i < 8; ++i)
        check(tools.interactionCursorResource(handles[i], 1) == QLatin1String(expected[i]),
              "all eight resize handles use correct cursor direction");
    check(tools.interactionCursorResource(QPointF(68,68), 1) == "image_cursor_rotate.svg",
          "outside corner uses rotation cursor");
    check(tools.interactionCursorResource(QPointF(120,120), 1) == "cursor_selection_move.svg",
          "floating interior uses move cursor");
    check(tools.interactionCursorResource(QPointF(10,10), 1).isEmpty(), "outside has no interaction cursor");
    for (const QPointF factors : {QPointF(2.5,.4), QPointF(.4,2.5)}) {
        selection.scaleFactors = factors;
        for (const int rotation : {0,90}) {
            selection.rotationDegrees = rotation;
            tools.document().setFloatingSelection(0, selection);
            const auto corners = selection.transformHandlePositions();
            for (int i = 0; i < 4; ++i)
                check(tools.interactionCursorResource(corners[i], 1)
                          == QLatin1String(expected[(i + rotation / 90) % 4]),
                      "wide and tall corners retain both diagonal cursor directions after rotation");
        }
    }
    selection.scaleFactors = QPointF(1,1);
    selection.rotationDegrees = 90;
    tools.document().setFloatingSelection(0, selection);
    check(tools.interactionCursorResource(selection.transformHandlePositions()[4], 1) == "cut_horizontal.svg",
          "rotated resize handles follow screen direction");
    selection.rotationDegrees = 0; tools.document().setFloatingSelection(0, selection);
    CursorCanvas canvas; canvas.setWidth(256); canvas.setHeight(256); canvas.setController(&tools);
    canvas.fitView();
    canvas.setZoomCursorDirectory(QUrl::fromLocalFile(QStringLiteral(VP_CURSOR_DIR) + "/"));
    const auto widget = [&](QPointF point) { return point * canvas.zoom() + QPointF(16,16); };
    QImage previousHandle;
    for (int i = 0; i < 8; ++i) {
        QHoverEvent cornerHover(QEvent::HoverMove, widget(handles[i]), QPointF());
        canvas.hoverMoveEvent(&cornerHover);
        const QString resource = tools.interactionCursorResource(handles[i], 1);
        checkCursorSize(canvas.cursor(), resource == "cut_oblique.svg" || resource == "cut_oblique_back.svg");
        const QImage current = canvas.cursor().pixmap().toImage();
        check(i == 0 || current != previousHandle,"adjacent resize handles update cursor pixels");
        previousHandle = current;
    }
    const QPointF outside[] = {QPointF(-12,-12), QPointF(12,-12), QPointF(12,12), QPointF(-12,12)};
    const int rotations[] = {-90,0,90,180};
    QImage previousRotation;
    for (int i = 0; i < 4; ++i) {
        const QPointF point = handles[i] + outside[i];
        check(tools.interactionCursorResource(point,1) == "image_cursor_rotate.svg"
                  && tools.interactionCursorRotation(point,1) == rotations[i],
              "each corner selects its own rotation cursor orientation");
        QHoverEvent cornerHover(QEvent::HoverMove, widget(point), QPointF());
        canvas.hoverMoveEvent(&cornerHover);
        const QImage current = canvas.cursor().pixmap().toImage();
        checkCursorSize(canvas.cursor());
        check(!current.isNull() && (i == 0 || current != previousRotation),
              "rotation cursor pixels change when moving between corners");
        previousRotation = current;
    }
    QHoverEvent interior(QEvent::HoverMove, widget(QPointF(120,120)), QPointF());
    canvas.hoverMoveEvent(&interior);
    checkCursorSize(canvas.cursor());
    QHoverEvent hover(QEvent::HoverMove, widget(handles[4]), QPointF()); canvas.hoverMoveEvent(&hover);
    check(canvas.cursor().shape() == Qt::BitmapCursor, "hover loads existing resize artwork");
    const QImage resize = canvas.cursor().pixmap().toImage();
    QMouseEvent press(QEvent::MouseButtonPress, widget(handles[4]), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    canvas.mousePressEvent(&press);
    QMouseEvent move(QEvent::MouseMove, widget(QPointF(20,20)), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    canvas.mouseMoveEvent(&move);
    check(canvas.cursor().pixmap().toImage() == resize, "resize drag retains initial cursor away from handle");
    QMouseEvent release(QEvent::MouseButtonRelease, widget(QPointF(20,20)), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    canvas.mouseReleaseEvent(&release);
    const PerspectiveQuad::Corners plane{{QPointF(0,0),QPointF(256,0),QPointF(256,256),QPointF(0,256)}};
    tools.document().appendPlane(PerspectivePlane(plane,plane));
    tools.setTool(VpTools::Marquee);
    QHoverEvent create(QEvent::HoverMove, widget(QPointF(220,220)), QPointF());
    canvas.hoverMoveEvent(&create);
    checkCursorSize(canvas.cursor());
    tools.setTool(VpTools::Hand);
    check(canvas.cursor().shape() == Qt::OpenHandCursor, "hand overrides interaction cursor");

}

void FloatingSelectionTransformToolTest::transformToolScalesFromCorner()
{
    FloatingSelection image;
    image.bitmap = QImage(100, 50, QImage::Format_ARGB32);
    image.placementOrigin = QPointF(10, 20);

    FloatingSelectionTransformTool tool;
    QVERIFY(tool.beginTransform(image, QPointF(110, 70), 2,
                                FloatingSelectionTransformTool::Mode::Scale));
    FloatingSelection result;
    QVERIFY(tool.update(QPointF(210, 120), false, false, &result));
    QCOMPARE(result.placementOrigin, QPointF(10, 20));
    QCOMPARE(result.scaleFactors, QPointF(2, 2));
}

void FloatingSelectionTransformToolTest::transformToolRotatesAroundCenter()
{
    FloatingSelection image;
    image.bitmap = QImage(100, 50, QImage::Format_ARGB32);
    image.placementOrigin = QPointF(10, 20);

    FloatingSelectionTransformTool tool;
    QVERIFY(tool.beginTransform(image, QPointF(10, 20), 0,
                                FloatingSelectionTransformTool::Mode::Rotate));
    FloatingSelection result;
    QVERIFY(tool.update(QPointF(85, -5), false, false, &result));
    QVERIFY(qAbs(result.rotationDegrees - 90.0) < 1e-6);
}

QTEST_MAIN(FloatingSelectionTransformToolTest)
#include "tst_floatingselectiontransformtool.moc"
