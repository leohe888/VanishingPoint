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
#include <QtMath>

#include "canvas/vpcanvas.h"
#include "canvas/vpcontroller.h"
#include "cursorhelpers.h"
#include "tools/floatingselectiontransformtool.h"

namespace {
using TransformTool=FloatingSelectionTransformTool;
FloatingSelection transformImage(bool padded=false)
{
    FloatingSelection image; image.bitmap=QImage(padded ? 120 : 100,padded ? 80 : 60,QImage::Format_ARGB32_Premultiplied);
    image.bitmap.fill(Qt::red); { QPainter painter(&image.bitmap); painter.fillRect(image.bitmap.width()/2,0,image.bitmap.width()/2,image.bitmap.height(),Qt::blue); }
    image.placementOrigin=padded ? QPointF(90,110) : QPointF(100,120);
    if(padded) { image.selectionRectInBitmap={10,10,100,60}; image.setFeather(3); }
    return image;
}

QTransform rotation(const FloatingSelection &image)
{
    const QPointF center=image.placementRect().center(); QTransform transform;
    transform.translate(center.x(),center.y()); transform.rotate(image.rotationDegrees); transform.translate(-center.x(),-center.y());
    return transform;
}

QPointF polar(QPointF center,qreal angle,qreal radius=100)
{
    const qreal radians=qDegreesToRadians(angle); return center+QPointF(qCos(radians),qSin(radians))*radius;
}

void setupTransform(VpController &controller,const FloatingSelection &image=transformImage())
{
    QImage background(500,400,QImage::Format_ARGB32); background.fill(Qt::transparent);
    controller.document().setImage(background); controller.document().addFloatingSelection(image);
    controller.document().resetHistory(); controller.setTool(VpTools::Transform);
}
}

class FloatingSelectionTransformToolTest : public QObject
{
    Q_OBJECT
private slots:
    void scalesEveryHandle_data();
    void scalesEveryHandle();
    void scaleLimits_data();
    void scaleLimits();
    void modifiersAndGrabOffsetUseStartSnapshot();
    void rotationAnglesAndSnapping_data();
    void rotationAnglesAndSnapping();
    void lifecycleAndInvalidInputs();
    void attachedTransformsAndGeometrySnapshot_data();
    void attachedTransformsAndGeometrySnapshot();
    void flippedAndFeatheredContentKeepsGeometry();
    void gesturesAndHistory_data();
    void gesturesAndHistory();
    void interruptedGestures_data();
    void interruptedGestures();
    void noOpGesturesPreserveRedo();
    void hitTestingBoundaries_data();
    void hitTestingBoundaries();
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

void FloatingSelectionTransformToolTest::scalesEveryHandle_data()
{
    QTest::addColumn<int>("handle"); QTest::addColumn<bool>("constrain"); QTest::addColumn<bool>("center");
    QTest::addColumn<bool>("rotated"); QTest::addColumn<bool>("padded");
    for(int handle=0;handle<8;++handle) for(bool constrain : {false,true}) for(bool center : {false,true})
        for(bool rotated : {false,true}) for(bool padded : {false,true})
            QTest::newRow(qPrintable(QString("h%1-shift%2-alt%3-rotate%4-feather%5").arg(handle).arg(constrain).arg(center).arg(rotated).arg(padded)))
                << handle << constrain << center << rotated << padded;
}

void FloatingSelectionTransformToolTest::scalesEveryHandle()
{
    QFETCH(int,handle); QFETCH(bool,constrain); QFETCH(bool,center); QFETCH(bool,rotated); QFETCH(bool,padded);
    auto image=transformImage(padded); image.rotationDegrees=rotated ? 37 : 0; const auto original=image;
    const QPointF directions[]={{-1,-1},{1,-1},{1,1},{-1,1},{0,-1},{1,0},{0,1},{-1,0}};
    const int opposite[]={2,3,0,1,6,7,4,5};
    const auto handles=image.transformHandlePositions(); const auto transform=rotation(image);
    const QPointF localHandle=transform.inverted().map(handles[handle]);
    const QPointF delta(directions[handle].x()*40,directions[handle].y()*30);
    TransformTool tool; QVERIFY(tool.beginTransform(image,handles[handle],handle,TransformTool::Mode::Scale));
    FloatingSelection result; QVERIFY(tool.update(transform.map(localHandle+delta),constrain,center,&result));
    qreal width=100+(directions[handle].x()!=0 ? (center ? 80 : 40) : 0);
    qreal height=60+(directions[handle].y()!=0 ? (center ? 60 : 30) : 0);
    if(constrain) { const qreal factor=qMax(width/100,height/60); width=100*factor; height=60*factor; }
    QVERIFY(qAbs(result.displaySize().width()-width)<1e-8); QVERIFY(qAbs(result.displaySize().height()-height)<1e-8);
    if(center) QVERIFY(QLineF(result.placementRect().center(),image.placementRect().center()).length()<1e-8);
    else QVERIFY(QLineF(result.transformHandlePositions()[opposite[handle]],handles[opposite[handle]]).length()<1e-8);
    QCOMPARE(result.rotationDegrees,image.rotationDegrees); QCOMPARE(result.bitmap,image.bitmap); QVERIFY(image==original);
}

void FloatingSelectionTransformToolTest::scaleLimits_data()
{
    QTest::addColumn<bool>("large"); QTest::addColumn<bool>("constrain"); QTest::addColumn<bool>("center");
    for(bool large : {false,true}) for(bool constrain : {false,true}) for(bool center : {false,true})
        QTest::newRow(qPrintable(QString("large%1-shift%2-alt%3").arg(large).arg(constrain).arg(center))) << large << constrain << center;
}

void FloatingSelectionTransformToolTest::scaleLimits()
{
    QFETCH(bool,large); QFETCH(bool,constrain); QFETCH(bool,center);
    const auto image=transformImage(); TransformTool tool; const auto press=image.transformHandlePositions()[2];
    QVERIFY(tool.beginTransform(image,press,2,TransformTool::Mode::Scale)); FloatingSelection result;
    QVERIFY(tool.update(large ? QPointF(1e8,1e8) : QPointF(-100,-100),constrain,center,&result));
    const QSizeF size=result.displaySize(); QVERIFY(size.width()>=1 && size.height()>=1);
    QVERIFY(size.width()<=100000 && size.height()<=100000);
    if(constrain) QVERIFY(qAbs(size.width()/size.height()-100.0/60)<1e-8);
    if(large) QVERIFY(qAbs(qMax(size.width(),size.height())-100000)<1e-8);
    else QVERIFY(qAbs(qMin(size.width(),size.height())-1)<1e-8);
    QVERIFY(tool.update(press,false,false,&result)); QVERIFY(result==image);
}

void FloatingSelectionTransformToolTest::modifiersAndGrabOffsetUseStartSnapshot()
{
    auto image=transformImage(); image.rotationDegrees=30; const QTransform transform=rotation(image);
    const QPointF handle=image.transformHandlePositions()[2],grab=transform.map(QPointF(2.5,1.5))-transform.map(QPointF(0,0));
    TransformTool tool; QVERIFY(tool.beginTransform(image,handle+grab,2,TransformTool::Mode::Scale));
    FloatingSelection result; QVERIFY(tool.update(handle+grab,false,false,&result));
    QVERIFY(QLineF(result.placementOrigin,image.placementOrigin).length()<1e-8);
    const QPointF target=transform.map(image.placementRect().bottomRight()+QPointF(40,20))+grab;
    const bool shifts[]={false,true,true,false},centers[]={false,false,true,true};
    for(int i=0;i<4;++i) {
        QVERIFY(tool.update(target,shifts[i],centers[i],&result));
        TransformTool fresh; QVERIFY(fresh.beginTransform(image,handle+grab,2,TransformTool::Mode::Scale));
        FloatingSelection expected; QVERIFY(fresh.update(target,shifts[i],centers[i],&expected)); QVERIFY(result==expected);
        FloatingSelection again; QVERIFY(tool.update(target,shifts[i],centers[i],&again)); QVERIFY(again==result);
    }
    QVERIFY(tool.update(handle+grab,false,false,&result)); QVERIFY(QLineF(result.placementOrigin,image.placementOrigin).length()<1e-8);
    QVERIFY(qAbs(result.scaleFactors.x()-1)<1e-8 && qAbs(result.scaleFactors.y()-1)<1e-8);
}

void FloatingSelectionTransformToolTest::rotationAnglesAndSnapping_data()
{
    QTest::addColumn<qreal>("initial"); QTest::addColumn<qreal>("pressAngle"); QTest::addColumn<qreal>("currentAngle");
    QTest::addColumn<bool>("snap"); QTest::addColumn<qreal>("expected");
    QTest::newRow("counterclockwise") << qreal(0) << qreal(0) << qreal(-90) << false << qreal(-90);
    QTest::newRow("clockwise") << qreal(0) << qreal(0) << qreal(90) << false << qreal(90);
    QTest::newRow("cross-positive-180") << qreal(170) << qreal(170) << qreal(-170) << false << qreal(-170);
    QTest::newRow("cross-negative-180") << qreal(-170) << qreal(-170) << qreal(170) << false << qreal(170);
    QTest::newRow("existing-angle") << qreal(37) << qreal(0) << qreal(25) << false << qreal(62);
    QTest::newRow("snap-positive") << qreal(10) << qreal(0) << qreal(14) << true << qreal(30);
    QTest::newRow("snap-negative") << qreal(-10) << qreal(0) << qreal(-14) << true << qreal(-30);
}

void FloatingSelectionTransformToolTest::rotationAnglesAndSnapping()
{
    QFETCH(qreal,initial); QFETCH(qreal,pressAngle); QFETCH(qreal,currentAngle); QFETCH(bool,snap); QFETCH(qreal,expected);
    auto image=transformImage(); image.rotationDegrees=initial; const auto center=image.placementRect().center();
    TransformTool tool; QVERIFY(tool.beginTransform(image,polar(center,pressAngle),0,TransformTool::Mode::Rotate));
    FloatingSelection result; QVERIFY(tool.update(polar(center,currentAngle),snap,false,&result));
    QVERIFY(qAbs(result.rotationDegrees-expected)<1e-8); QCOMPARE(result.placementRect(),image.placementRect());
    const auto rotated=result; QVERIFY(tool.update(center,snap,false,&result)); QVERIFY(result==image);
    QVERIFY(tool.update(polar(center,currentAngle),snap,false,&result)); QVERIFY(result==rotated);
    QVERIFY(tool.update(polar(center,pressAngle),false,false,&result)); QVERIFY(qAbs(result.rotationDegrees-initial)<1e-8);
}

void FloatingSelectionTransformToolTest::lifecycleAndInvalidInputs()
{
    const auto image=transformImage(); const QPointF press=image.transformHandlePositions()[2];
    FloatingSelection result=image; TransformTool idle;
    QVERIFY(!idle.update(press,false,false,&result)); QVERIFY(!idle.isTransforming());
    for(int handle : {-1,8}) QVERIFY(!idle.beginTransform(image,press,handle,TransformTool::Mode::Scale));
    for(auto mode : {TransformTool::Mode::Idle,TransformTool::Mode::Move}) QVERIFY(!idle.beginTransform(image,press,2,mode));
    const qreal nan=std::numeric_limits<qreal>::quiet_NaN(),inf=std::numeric_limits<qreal>::infinity();
    const QPointF invalid[]={{nan,120},{120,nan},{inf,120},{120,-inf}};
    for(auto mode : {TransformTool::Mode::Move,TransformTool::Mode::Scale,TransformTool::Mode::Rotate}) {
        TransformTool tool;
        if(mode==TransformTool::Mode::Move) tool.beginMove(image,{20,10});
        else QVERIFY(tool.beginTransform(image,press,2,mode));
        QVERIFY(!tool.update(press,false,false,nullptr));
        for(const auto &point : invalid) { result=image; QVERIFY(!tool.update(point,false,false,&result)); QVERIFY(result==image); }
        QVERIFY(tool.update({240,220},false,false,&result));
        tool.reset(); QVERIFY(!tool.isTransforming()); QVERIFY(!tool.update(press,false,false,&result));
    }
    for(const auto &point : invalid) QVERIFY(!idle.beginTransform(image,point,2,TransformTool::Mode::Scale));
    QVERIFY(!idle.beginTransform({},press,2,TransformTool::Mode::Scale));
    TransformTool active; QVERIFY(active.beginTransform(image,press,2,TransformTool::Mode::Scale));
    auto invalidMapping=image; invalidMapping.surfaceAttached=true; invalidMapping.hostQuadIndex=0; invalidMapping.surfaceQuads={PerspectiveQuad()};
    QVERIFY(!active.beginTransform(invalidMapping,press,2,TransformTool::Mode::Scale));
    QVERIFY(active.startSelection()==image); QVERIFY(active.update(press+QPointF(20,20),false,false,&result));
    QCOMPARE(result.displaySize(),QSizeF(120,80));
    QVERIFY(idle.beginTransform(image,press,2,TransformTool::Mode::Scale)); idle.reset();
    idle.beginMove(image,{20,10}); QVERIFY(idle.update({150,160},false,false,&result)); QCOMPARE(result.placementOrigin,QPointF(130,150));
}

void FloatingSelectionTransformToolTest::attachedTransformsAndGeometrySnapshot_data()
{
    QTest::addColumn<int>("mode"); QTest::newRow("move") << 0; QTest::newRow("scale") << 1; QTest::newRow("rotate") << 2;
}

void FloatingSelectionTransformToolTest::attachedTransformsAndGeometrySnapshot()
{
    QFETCH(int,mode); auto image=transformImage();
    QVector<PerspectiveQuad> faces{
        PerspectiveQuad({{{10,10},{210,20},{205,250},{15,240}}},{{{0,0},{200,0},{200,300},{0,300}}}),
        PerspectiveQuad({{{210,20},{460,35},{450,270},{205,250}}},{{{200,0},{400,0},{400,300},{200,300}}})};
    image.surfaceAttached=true; image.surfaceQuads=faces; image.hostQuadIndex=0; const auto original=image;
    TransformTool tool; const auto center=image.placementRect().center();
    if(mode==0) tool.beginMove(image,{20,10});
    else { const QPointF press=mode==1 ? image.transformHandlePositions()[2] : polar(center,0,45);
           QVERIFY(tool.beginTransform(image,image.mapPlacementToCanvas(press),2,mode==1 ? TransformTool::Mode::Scale : TransformTool::Mode::Rotate)); }
    faces[0].setCanvasCorner(2,{250,280}); faces.clear();
    const QPointF target=mode==0 ? QPointF(250,150) : mode==1 ? QPointF(240,210) : polar(center,90,45);
    FloatingSelection result; QVERIFY(tool.update(image.mapPlacementToCanvas(target),false,false,&result));
    if(mode==0) QVERIFY(QLineF(result.placementOrigin,{230,140}).length()<1e-8);
    if(mode==1) { QVERIFY(qAbs(result.displaySize().width()-140)<1e-8); QVERIFY(qAbs(result.displaySize().height()-90)<1e-8); }
    if(mode==2) QVERIFY(qAbs(result.rotationDegrees-90)<1e-8);
    QVERIFY(result.surfaceQuads==original.surfaceQuads); QCOMPARE(result.hostQuadIndex,0); QVERIFY(result.surfaceAttached); QCOMPARE(result.bitmap,original.bitmap);
    // An attached move extrapolates outside its host face without detaching.
    tool.beginMove(image,{20,10}); const QPointF outside=image.surfaceQuads[0].surfaceToCanvasTransform().forward().map(QPointF(-50,150));
    QVERIFY(tool.update(outside,false,false,&result)); QVERIFY(QLineF(result.placementOrigin,{-70,140}).length()<1e-8); QVERIFY(result.surfaceAttached);
}

void FloatingSelectionTransformToolTest::flippedAndFeatheredContentKeepsGeometry()
{
    VpController controller; const auto original=transformImage(true); setupTransform(controller,original);
    controller.setFlippedHorizontally(true); controller.setFlippedVertically(true); const auto flipped=controller.document().floatingSelection(0);
    QCOMPARE(flipped.bitmap,original.bitmap.mirrored(true,true));
    TransformTool tool; QVERIFY(tool.beginTransform(flipped,flipped.transformHandlePositions()[2],2,TransformTool::Mode::Scale));
    FloatingSelection scaled; QVERIFY(tool.update({250,210},true,true,&scaled));
    QVERIFY(scaled.flippedHorizontally && scaled.flippedVertically); QCOMPARE(scaled.bitmap,flipped.bitmap);
    QCOMPARE(scaled.selectionRectInBitmap,original.selectionRectInBitmap); QCOMPARE(scaled.feather,3);
    QVERIFY(tool.beginTransform(scaled,polar(scaled.placementRect().center(),0),0,TransformTool::Mode::Rotate));
    FloatingSelection rotated; QVERIFY(tool.update(polar(scaled.placementRect().center(),90),false,false,&rotated));
    QCOMPARE(rotated.bitmap,flipped.bitmap); QVERIFY(rotated.flippedHorizontally && rotated.flippedVertically);
    QCOMPARE(rotated.displaySize(),scaled.displaySize()); QCOMPARE(rotated.rotationDegrees,qreal(90));
}

void FloatingSelectionTransformToolTest::gesturesAndHistory_data()
{
    QTest::addColumn<int>("mode"); QTest::newRow("move") << 0; QTest::newRow("scale") << 1; QTest::newRow("rotate") << 2;
}

void FloatingSelectionTransformToolTest::gesturesAndHistory()
{
    QFETCH(int,mode); VpController controller; const auto original=transformImage(); setupTransform(controller,original); auto &doc=controller.document();
    const QPointF start=mode==0 ? QPointF(150,150) : mode==1 ? QPointF(200,180) : QPointF(215,190);
    const QPointF finish=mode==0 ? QPointF(175,165) : mode==1 ? QPointF(240,210) : QPointF(110,215);
    controller.pointerPress(start,1,Qt::NoModifier); QVERIFY(doc.editActive());
    controller.pointerMove((start+finish)/2,Qt::NoModifier); controller.pointerRelease(finish,Qt::NoModifier);
    QVERIFY(!doc.editActive()); const auto after=doc.floatingSelection(0); QVERIFY(!(after==original));
    if(mode==0) QCOMPARE(after.placementOrigin,QPointF(125,135));
    if(mode==1) QCOMPARE(after.displaySize(),QSizeF(140,90));
    if(mode==2) QVERIFY(qAbs(after.rotationDegrees-90)<1e-8);
    const QImage rendered=controller.resultImage(); controller.undo(); QVERIFY(doc.floatingSelection(0)==original); QVERIFY(!doc.canUndo());
    controller.redo(); QVERIFY(doc.floatingSelection(0)==after); QCOMPARE(controller.resultImage(),rendered); QVERIFY(!doc.canRedo());
}

void FloatingSelectionTransformToolTest::interruptedGestures_data()
{
    QTest::addColumn<int>("mode"); QTest::addColumn<int>("interrupt");
    for(int mode=0;mode<3;++mode) for(int interrupt=0;interrupt<3;++interrupt)
        QTest::newRow(qPrintable(QString("mode%1-interrupt%2").arg(mode).arg(interrupt))) << mode << interrupt;
}

void FloatingSelectionTransformToolTest::interruptedGestures()
{
    QFETCH(int,mode); QFETCH(int,interrupt); VpController controller; const auto original=transformImage(); setupTransform(controller,original); auto &doc=controller.document();
    const QPointF start=mode==0 ? QPointF(150,150) : mode==1 ? QPointF(200,180) : QPointF(215,190);
    controller.pointerPress(start,1,Qt::NoModifier); controller.pointerMove(start+QPointF(30,20),Qt::NoModifier); QVERIFY(doc.editActive());
    if(interrupt==0) controller.cancelInteraction(); else if(interrupt==1) controller.setTool(VpTools::Hand); else controller.undo();
    QVERIFY(doc.floatingSelection(0)==original); QVERIFY(!doc.editActive()); QVERIFY(!doc.canUndo());
    controller.pointerMove({300,250},Qt::NoModifier); QVERIFY(doc.floatingSelection(0)==original);
    controller.setTool(VpTools::Transform); controller.pointerPress({200,180},1,Qt::NoModifier); controller.pointerRelease({220,190},Qt::NoModifier);
    QVERIFY(doc.canUndo()); controller.undo(); QVERIFY(doc.floatingSelection(0)==original);
}

void FloatingSelectionTransformToolTest::noOpGesturesPreserveRedo()
{
    VpController controller; const auto image=transformImage(); setupTransform(controller,image); auto &doc=controller.document();
    controller.setFlippedHorizontally(true); controller.undo(); QVERIFY(doc.canRedo());
    for(const QPointF point : {QPointF(150,150),QPointF(200,180),QPointF(215,190)}) {
        controller.pointerPress(point,1,Qt::NoModifier); controller.pointerRelease(point,Qt::NoModifier);
        QVERIFY(doc.floatingSelection(0)==image); QVERIFY(!doc.canUndo()); QVERIFY(doc.canRedo());
    }
    controller.redo(); QVERIFY(doc.floatingSelection(0).flippedHorizontally);
}

void FloatingSelectionTransformToolTest::hitTestingBoundaries_data()
{
    QTest::addColumn<qreal>("zoom"); for(qreal zoom : {.5,1.0,2.0}) QTest::newRow(qPrintable(QString::number(zoom))) << zoom;
}

void FloatingSelectionTransformToolTest::hitTestingBoundaries()
{
    QFETCH(qreal,zoom); const auto image=transformImage(); const auto handles=image.transformHandlePositions();
    for(int i=0;i<8;++i) QCOMPARE(TransformTool::handleAt(image,handles[i],zoom),i);
    const QPointF corner=handles[0];
    QCOMPARE(TransformTool::handleAt(image,corner-QPointF(8/zoom,0),zoom),0);
    QCOMPARE(TransformTool::handleAt(image,corner-QPointF(8.01/zoom,0),zoom),-1);
    QCOMPARE(TransformTool::rotationCornerAt(image,corner-QPointF(8/zoom,0),zoom),-1);
    QCOMPARE(TransformTool::rotationCornerAt(image,corner-QPointF(8.01/zoom,0),zoom),0);
    QCOMPARE(TransformTool::rotationCornerAt(image,corner-QPointF(24/zoom,0),zoom),0);
    QCOMPARE(TransformTool::rotationCornerAt(image,corner-QPointF(24.01/zoom,0),zoom),-1);
    QCOMPARE(TransformTool::rotationCornerAt(image,corner+QPointF(10/zoom,10/zoom),zoom),-1);
}

QTEST_MAIN(FloatingSelectionTransformToolTest)
#include "tst_floatingselectiontransformtool.moc"
