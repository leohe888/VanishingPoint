#include <limits>
#include <QCoreApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QtMath>
#include <QtTest>

#include "canvas/auxiliaryoverlayitem.h"
#include "canvas/vpcanvas.h"
#include "core/scenerenderer.h"
#include "testhelpers.h"
#include "tools/planeedittool.h"

namespace {
// Keep every fixture edge away from the camera principal axes: otherwise the
// projected depth direction can lie on the shared edge and collapse an extrusion.
const QSize editCanvasSize(800,800);

QImage editBackground(QSize size = editCanvasSize)
{
    QImage image(size,QImage::Format_ARGB32);
    image.fill(Qt::white);
    return image;
}

PerspectivePlane editPlane()
{
    PerspectivePlane plane({QPointF(100,100),QPointF(200,100),QPointF(200,200),QPointF(100,200)},
                           {QPointF(0,0),QPointF(100,0),QPointF(100,100),QPointF(0,100)});
    plane.setSurfaceGroupId(0);
    return plane;
}

PerspectivePlane interactionPlane(bool perspective = false)
{
    const PerspectiveQuad::Corners canvas = perspective
        ? PerspectiveQuad::Corners{{{100,100},{320,115},{285,310},{115,290}}}
        : PerspectiveQuad::Corners{{{100,100},{300,100},{300,300},{100,300}}};
    PerspectivePlane plane(canvas,{{{0,0},{200,0},{200,200},{0,200}}});
    plane.setSurfaceGroupId(7);
    return plane;
}

QImage bitmap(QColor color)
{
    QImage image(20,20,QImage::Format_ARGB32);
    image.fill(color);
    return image;
}

class PlaneEditCanvas : public VpCanvas
{
public:
    void pointer(QEvent::Type type, QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier)
    {
        QMouseEvent event(type,point,point,
            type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
            type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,modifiers);
        if (type == QEvent::MouseButtonPress) mousePressEvent(&event);
        else if (type == QEvent::MouseMove) mouseMoveEvent(&event);
        else mouseReleaseEvent(&event);
    }
};

bool setupEditCanvas(PlaneEditCanvas &canvas, VpController &controller)
{
    canvas.setController(&controller);
    if (!controller.document().setImage(editBackground(QSize(400,400)))) return false;
    canvas.setSize(QSizeF(400,400));
    canvas.setZoom(1);
    return true;
}

QPointF polarPoint(QPointF center, qreal degrees, qreal radius = 120)
{
    return center + QPointF(qCos(qDegreesToRadians(degrees)),qSin(qDegreesToRadians(degrees)))*radius;
}

bool extrusionTarget(const PerspectivePlane &plane, int edge, QPointF *press, QPointF *target)
{
    *press = plane.quad().controlPoints()[4+edge];
    QPointF normal;
    if (!projectedNormalDirection(plane,*press,editCanvasSize,&normal)) return false;
    *target = *press + normal*70;
    return true;
}

enum EditAction { MovePlane, MoveCorner, ExtendEdge, ExtrudePlane, RotatePlane };
struct EditGesture {
    int index = 0;
    QPointF press;
    QPointF target;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
};

bool setupGesture(VpController &controller, int action, EditGesture *gesture)
{
    auto &doc = controller.document();
    if (!doc.setImage(editBackground())) return false;
    auto plane = interactionPlane();
    if (doc.appendPlane(plane) != 0) return false;
    gesture->index = 0;
    if (action == RotatePlane) {
        QPointF press, target;
        if (!extrusionTarget(plane,0,&press,&target)) return false;
        auto child = extrudePerpendicularPlane(plane,0,target,press,editCanvasSize);
        if (!child.quad().isProjectable()) return false;
        child.setParent(0,0);
        if (doc.appendPlane(child) != 1) return false;
        doc.lockPlaneEdge(0,0);
        gesture->index = 1;
        gesture->press = child.quad().controlPoints()[6];
        const auto pivot = child.quad().controlPoints()[4];
        const auto offset = gesture->press-pivot;
        const qreal angle = qRadiansToDegrees(qAtan2(offset.y(),offset.x()));
        gesture->target = polarPoint(pivot,angle+10,QLineF(pivot,gesture->press).length());
        gesture->modifiers = Qt::AltModifier;
    } else if (action == ExtrudePlane) {
        if (!extrusionTarget(plane,0,&gesture->press,&gesture->target)) return false;
        gesture->modifiers = Qt::ControlModifier;
    } else {
        gesture->press = action == MovePlane ? QPointF(200,200)
            : plane.quad().controlPoints()[action == MoveCorner ? 2 : 4];
        gesture->target = gesture->press + (action == ExtendEdge ? QPointF(0,-30) : QPointF(25,15));
    }
    doc.setSelectedPlane(gesture->index);
    controller.setTool(VpTools::EditPlane);
    doc.resetHistory();
    return true;
}

void addActionRows()
{
    QTest::addColumn<int>("action");
    QTest::newRow("move") << int(MovePlane);
    QTest::newRow("corner") << int(MoveCorner);
    QTest::newRow("edge") << int(ExtendEdge);
    QTest::newRow("extrude") << int(ExtrudePlane);
    QTest::newRow("rotate") << int(RotatePlane);
}
}

class PlaneEditToolTest : public QObject
{
    Q_OBJECT

private slots:
    void cornerEditsRestoreGeometryAndDiagnosticColor();
    void adjacentMidpointsExtendAfterExtrusion();
    void adjacentMidpointsExtendAfterExtrusion_data();
    void extrusionCanUseAdjacentUnlockedEdgeAndCannotRepeatSharedEdge();
    void perspectiveClickAndUnchangedAngleDoNotCreateHistory();
    void angleLockRejectsBothInputAndAltDrag();
    void sharedEdgeSurvivesSiblingRemovalAndRejectsMutation();
    void releaseMovesPlaneWithoutMoveEvent();
    void lockedCornerAndNoopPreserveHistory();
    void cyclesOverlappingPlanesAndPreservesDragActions();
    void dragsCorner();
    void resizesFromEdgeMidpoint();
    void movesPlaneFromInterior();
    void rejectsInvalidCornerDrag();
    void extrudesPerpendicularPlaneFromEdgeMidpoint();
    void extendsAdjacentEdgesWithoutBendingSeam();
    void rotationTracksDirectionAndWraps_data();
    void rotationTracksDirectionAndWraps();
    void dragCommitsOneHistoryEntry_data();
    void dragCommitsOneHistoryEntry();
    void cancelledDragsRestoreDocument_data();
    void cancelledDragsRestoreDocument();
    void repeatedUpdatesDoNotAccumulate_data();
    void repeatedUpdatesDoNotAccumulate();
    void invalidCornerUpdatePreservesOutputAndRecovers_data();
    void invalidCornerUpdatePreservesOutputAndRecovers();
    void allHandlesPreserveUneditedGeometry_data();
    void allHandlesPreserveUneditedGeometry();
    void lockedHandlesAndLinkedBodiesCannotMove_data();
    void lockedHandlesAndLinkedBodiesCannotMove();
    void extrusionPreviewAndCommitLifecycle_data();
    void extrusionPreviewAndCommitLifecycle();
    void invalidExtrusionDoesNotCommit_data();
    void invalidExtrusionDoesNotCommit();
    void ctrlDragThresholdUsesScreenDistance_data();
    void ctrlDragThresholdUsesScreenDistance();
    void handleHitToleranceScalesWithView_data();
    void handleHitToleranceScalesWithView();
    void unchangedGesturePreservesRedo_data();
    void unchangedGesturePreservesRedo();
    void toolReuseAndInvalidOutputs();
};

void PlaneEditToolTest::dragsCorner()
{
    const PerspectivePlane plane = makeTestPlane();
    PlaneEditTool tool;
    tool.begin(plane, plane.quad().canvasCorners()[0], 0, -1, false, QSize(1200, 800));
    PerspectivePlane result;
    QVERIFY(tool.update(QPointF(20, 20), &result));
    QCOMPARE(result.quad().canvasCorners()[0], QPointF(20, 20));
    QCOMPARE(result.quad().canvasCorners()[2], plane.quad().canvasCorners()[2]);
}

void PlaneEditToolTest::resizesFromEdgeMidpoint()
{
    const PerspectivePlane plane = makeTestPlane();
    const QPointF midpoint(60, 10);
    PlaneEditTool tool;
    tool.begin(plane, midpoint, 4, 0, false, QSize(1200, 800));
    PerspectivePlane result;
    QVERIFY(tool.update(QPointF(60, -10), &result));
    QVERIFY(result.quad().isValid());
    QVERIFY(result.quad().canvasCorners()[0] != plane.quad().canvasCorners()[0]);
    QVERIFY(result.quad().canvasCorners()[1] != plane.quad().canvasCorners()[1]);
    QCOMPARE(result.quad().canvasCorners()[2], plane.quad().canvasCorners()[2]);
    QCOMPARE(result.quad().canvasCorners()[3], plane.quad().canvasCorners()[3]);
}

void PlaneEditToolTest::movesPlaneFromInterior()
{
    const PerspectivePlane plane = makeTestPlane();
    PlaneEditTool tool;
    tool.begin(plane, QPointF(60, 45), -1, -1, false, QSize(1200, 800));
    PerspectivePlane result;
    QVERIFY(tool.update(QPointF(80, 60), &result));
    QCOMPARE(result.quad().canvasCorners()[0], QPointF(30, 25));
    QCOMPARE(result.quad().canvasCorners()[2], QPointF(130, 95));
    QCOMPARE(result.quad().surfaceCorners()[0], QPointF(20, 15));
}

void PlaneEditToolTest::rejectsInvalidCornerDrag()
{
    const PerspectivePlane plane = makeTestPlane();
    PlaneEditTool tool;
    tool.begin(plane, plane.quad().canvasCorners()[0], 0, -1, false, QSize(1200, 800));
    PerspectivePlane result;
    QVERIFY(!tool.update(plane.quad().canvasCorners()[2], &result));
}

// Ctrl + 拖动边中点：从这条边拉出与之垂直的新平面。
void PlaneEditToolTest::extrudesPerpendicularPlaneFromEdgeMidpoint()
{
    const PerspectivePlane plane = makeTestPlane();
    PlaneEditTool tool;
    tool.begin(plane, QPointF(60, 10), 4, 0, true, QSize(1200, 800));
    QVERIFY(tool.extruding());
    QCOMPARE(tool.edgeIndex(), 0);

    PerspectivePlane result;
    QVERIFY(tool.update(QPointF(60, -40), &result));
    QVERIFY(result.quad().isValid());
    QCOMPARE(result.quad().canvasCorners()[0], plane.quad().canvasCorners()[0]);       // 共用边原样继承
    QCOMPARE(result.quad().canvasCorners()[1], plane.quad().canvasCorners()[1]);
    QCOMPARE(result.quad().surfaceCorners()[0], plane.quad().surfaceCorners()[0]); // 接缝处曲面坐标相同
    QCOMPARE(result.quad().surfaceCorners()[1], plane.quad().surfaceCorners()[1]);
    QCOMPARE(int(result.lockedEdgeMask()), 1);              // 第 0 条边是与源平面共用的边
    QCOMPARE(result.surfaceGroupId(), plane.surfaceGroupId()); // 与源平面同属一个展开曲面
    QCOMPARE(result.angleToParentDegrees(), 90.0);              // 默认与源平面垂直
    QVERIFY(QLineF(result.quad().canvasCorners()[2], plane.quad().canvasCorners()[2]).length() > 1.0); // 外侧边已离开源平面

    PlaneEditTool tooShort;                            // 拖动距离过小时拉不出有效几何
    tooShort.begin(plane, QPointF(60, 10), 4, 0, true, QSize(1200, 800));
    QVERIFY(!tooShort.update(QPointF(60, 11), &result));
}

void PlaneEditToolTest::extendsAdjacentEdgesWithoutBendingSeam()
{
    for (int seam = 0; seam < 4; ++seam) {
        auto plane = makeTestPlane(); plane.setEdgeLocked(seam, true);
        for (int side : {1, 3}) {
            const int edge = (seam + side) % 4;
            const auto corners = plane.quad().canvasCorners();
            const QPointF press = (corners[edge] + corners[(edge+1)%4]) / 2;
            const QPointF axis = press - (corners[(edge+2)%4] + corners[(edge+3)%4]) / 2;
            PlaneEditTool tool;
            tool.begin(plane, press, 4 + edge, edge, false, QSize(1200, 800));
            PerspectivePlane result;
            QVERIFY(tool.update(press + axis / 5, &result));
            QVERIFY(result.quad().canvasCorners() != corners);
            QVERIFY(plane.preservesLockedEdges(result, true));
            QVERIFY(!plane.preservesLockedEdges(result));
            auto bent = result;
            const QPointF seamDirection = corners[(seam+1)%4] - corners[seam];
            bent.quad().setCanvasCorner(seam, result.quad().canvasCorners()[seam]
                + QPointF(-seamDirection.y(), seamDirection.x()) / 10);
            QVERIFY(!plane.preservesLockedEdges(bent, true));
        }
    }
}

void PlaneEditToolTest::cyclesOverlappingPlanesAndPreservesDragActions()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    QImage background(600,600,QImage::Format_ARGB32_Premultiplied); background.fill(Qt::white);
    const PerspectiveQuad::Corners surface{{QPointF(0,0), QPointF(200,0), QPointF(200,200), QPointF(0,200)}};
    const PerspectivePlane plane({QPointF(100,100), QPointF(300,100), QPointF(300,300), QPointF(100,300)},surface);
    VpController controller; controller.document().setImage(background);
    for (int i=0; i<3; ++i) controller.document().appendPlane(plane);
    controller.document().appendPlane(PerspectivePlane(
        {QPointF(350,350),QPointF(450,350),QPointF(450,450),QPointF(350,450)},surface));
    controller.document().setSelectedPlane(2); controller.document().commitHistory();
    controller.setTool(VpTools::EditPlane);
    check(controller.interactionCursorResource(QPointF(100,200),1)=="cut_horizontal.svg"
              && controller.interactionCursorResource(QPointF(300,200),1)=="cut_horizontal.svg",
          "left and right plane edge handles use horizontal extension cursor");
    check(controller.interactionCursorResource(QPointF(200,100),1)=="cut_vertical.svg"
              && controller.interactionCursorResource(QPointF(200,300),1)=="cut_vertical.svg",
          "top and bottom plane edge handles use vertical extension cursor");
    check(controller.interactionCursorResource(QPointF(100,100),1)=="image_choice.svg"
              && controller.interactionCursorResource(QPointF(200,200),1)=="image_choice.svg",
          "plane corners and body use project pointer cursor");
    const auto original = controller.document().planes();
    const bool undoBefore = controller.canUndo();
    const auto click = [&](const QPointF &point) {
        controller.pointerPress(point,1,Qt::ControlModifier);
        controller.pointerRelease(point,Qt::ControlModifier);
    };
    for (int expected : {1,0,2,1}) {
        click(QPointF(200,200));
        check(controller.document().selectedPlane()==expected, "Ctrl click cycles top to bottom and wraps");
    }
    check(controller.document().planes()==original && controller.canUndo()==undoBefore,
          "cycling changes neither geometry nor history");
    controller.document().setSelectedPlane(3);
    click(QPointF(200,200));
    check(controller.document().selectedPlane()==2, "unrelated selected plane starts at topmost candidate");
    click(QPointF(400,400));
    check(controller.document().selectedPlane()==3, "single candidate remains selectable");
    click(QPointF(500,500));
    check(controller.document().selectedPlane()==-1, "empty region clears selection");
    controller.document().setSelectedPlane(2);
    controller.pointerPress(QPointF(200,100),1,Qt::ControlModifier);
    controller.pointerMove(QPointF(202,100),Qt::ControlModifier);
    controller.pointerRelease(QPointF(202,100),Qt::ControlModifier);
    check(controller.document().selectedPlane()==1 && controller.document().planes()==original,
          "edge click with small jitter cycles without extrusion");
    controller.pointerPress(QPointF(200,200),1,Qt::ControlModifier);
    controller.cancelInteraction();
    controller.pointerRelease(QPointF(200,200),Qt::ControlModifier);
    check(controller.document().selectedPlane()==1, "cancel discards pending cycle");
    controller.pointerPress(QPointF(200,200),1,Qt::NoModifier);
    controller.pointerRelease(QPointF(200,200),Qt::NoModifier);
    check(controller.document().selectedPlane()==2, "normal click still selects topmost plane");
    controller.pointerPress(QPointF(200,200),1,Qt::ControlModifier);
    controller.pointerRelease(QPointF(240,200),Qt::ControlModifier);
    check(controller.document().selectedPlane()==2
          && controller.document().planes()[2].quad().canvasCorners()[0]==QPointF(140,100),
          "Ctrl body drag retains original movement rather than cycling");
    controller.undo();
    check(controller.document().planes()==original, "body drag remains one undo step");

    VpController extrusion; extrusion.document().setImage(background);
    extrusion.document().appendPlane(plane); extrusion.document().setSelectedPlane(0);
    extrusion.document().commitHistory(); extrusion.setTool(VpTools::EditPlane);
    const QPointF press(200,100);
    QPointF drag; PerspectivePlane expected;
    bool found = false;
    for (int dx : {-150,0,150}) for (int dy : {-150,0,150}) {
        if (found || (!dx && !dy)) continue;
        const QPointF point = press + QPointF(dx,dy);
        const auto candidate = extrudePerpendicularPlane(plane,0,point,press,background.size());
        if (candidate.quad().isProjectable()) { found=true; drag=point; expected=candidate; }
    }
    check(found, "fixture has valid perpendicular extrusion");
    if (found) {
        extrusion.pointerPress(press,1,Qt::ControlModifier);
        extrusion.pointerMove(drag,Qt::ControlModifier);
        check(extrusion.extrudePreview()!=nullptr, "Ctrl edge drag still creates extrusion preview");
        extrusion.pointerRelease(drag,Qt::ControlModifier);
        check(extrusion.document().planes().size()==2, "Ctrl edge drag still commits a child plane");
        if (extrusion.document().planes().size()==2)
            check(extrusion.document().planes()[1].quad()==expected.quad(), "extrusion geometry preserved");
        extrusion.undo(); check(extrusion.document().planes().size()==1, "extrusion undo is one step");
    }
}

void PlaneEditToolTest::lockedCornerAndNoopPreserveHistory()
{
    VpController testController; PlaneEditCanvas canvas; QVERIFY(setupEditCanvas(canvas, testController)); auto &doc=canvas.controller()->document(); auto p=editPlane();
    p.setEdgeLocked(0,true); doc.appendPlane(p); doc.resetHistory(); canvas.controller()->setTool(VpTools::EditPlane);
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseButtonRelease,{110,110});
    QCOMPARE(doc.planes()[0].quad().canvasCorners()[0],QPointF(100,100)); QVERIFY(!doc.canUndo());
    doc.addFloatingSelection(makeTestSelection(bitmap(Qt::red))); doc.undo(); QVERIFY(doc.canRedo());
    canvas.pointer(QEvent::MouseButtonPress,{150,150}); canvas.pointer(QEvent::MouseButtonRelease,{150,150});
    QVERIFY(doc.canRedo());
}

void PlaneEditToolTest::releaseMovesPlaneWithoutMoveEvent()
{
    VpController testController; PlaneEditCanvas canvas; QVERIFY(setupEditCanvas(canvas, testController)); auto &doc=canvas.controller()->document(); doc.appendPlane(editPlane()); doc.resetHistory(); canvas.controller()->setTool(VpTools::EditPlane);
    canvas.pointer(QEvent::MouseButtonPress,{150,150}); canvas.pointer(QEvent::MouseButtonRelease,{160,150});
    QCOMPARE(doc.planes()[0].quad().canvasCorners()[0],QPointF(110,100)); QVERIFY(doc.canUndo());
}

void PlaneEditToolTest::sharedEdgeSurvivesSiblingRemovalAndRejectsMutation()
{
    VpController testController; PlaneEditCanvas canvas; QVERIFY(setupEditCanvas(canvas, testController)); auto &doc=canvas.controller()->document(); auto parent=editPlane();
    parent.setEdgeLocked(0,true); doc.appendPlane(parent);
    auto child=editPlane(); child.setParent(0,0); child.setEdgeLocked(0,true);
    doc.appendPlane(child); doc.appendPlane(child); doc.resetHistory(); canvas.controller()->setTool(VpTools::EditPlane); doc.removePlane(1);
    QVERIFY(doc.planes()[0].isEdgeLocked(0));
    auto candidate=doc.planes()[0]; candidate.quad().setCanvasCorner(0,{110,110});
    QVERIFY(!doc.setPlane(0,candidate));
    doc.removePlane(1); QVERIFY(!doc.planes()[0].isEdgeLocked(0));
    canvas.undo(); QVERIFY(doc.planes()[0].isEdgeLocked(0));
}

void PlaneEditToolTest::angleLockRejectsBothInputAndAltDrag()
{
    VpController testController; PlaneEditCanvas canvas; QVERIFY(setupEditCanvas(canvas, testController)); auto *controller=canvas.controller(); auto &doc=controller->document();
    auto parent=editPlane(); doc.appendPlane(parent);
    auto child=editPlane(); child.setParent(0,0); child.setEdgeLocked(0,true); doc.appendPlane(child);
    auto grandchild=editPlane(); grandchild.quad().setCanvasCorners({QPointF(250,250),QPointF(350,250),QPointF(350,350),QPointF(250,350)});
    grandchild.setParent(1,2); grandchild.setHasCustomAngle(true); doc.appendPlane(grandchild);
    doc.setSelectedPlane(1); doc.resetHistory(); canvas.controller()->setTool(VpTools::EditPlane); QVERIFY(!controller->planeAngleEditable());
    controller->setPlaneAngle(120); QCOMPARE(doc.planes()[1].angleToParentDegrees(),qreal(90));
    controller->pointerPress({150,200},1,Qt::AltModifier); controller->pointerRelease({140,210},Qt::AltModifier);
    QVERIFY(!doc.canUndo()); QCOMPARE(doc.planes()[1].quad().canvasCorners(),child.quad().canvasCorners());
}

void PlaneEditToolTest::perspectiveClickAndUnchangedAngleDoNotCreateHistory()
{
    VpController testController; PlaneEditCanvas canvas; QVERIFY(setupEditCanvas(canvas, testController)); auto *controller=canvas.controller(); auto &doc=controller->document();
    auto p=editPlane(); p.quad().setCanvasCorners({QPointF(100,100),QPointF(240,100),QPointF(280,240),QPointF(80,240)});
    doc.appendPlane(p); auto child=p; child.setParent(0,0); doc.appendPlane(child); doc.setSelectedPlane(1); doc.resetHistory(); canvas.controller()->setTool(VpTools::EditPlane);
    controller->setPlaneAngle(90); QVERIFY(!doc.canUndo()); QVERIFY(!doc.planes()[1].hasCustomAngle());
    controller->pointerPress({180,240},1,Qt::AltModifier); controller->pointerRelease({180,240},Qt::AltModifier);
    QVERIFY(!doc.canUndo()); QVERIFY(doc.planes()[1]==child);
    doc.removePlane(1); doc.resetHistory(); canvas.controller()->setTool(VpTools::EditPlane);
    controller->pointerPress({170,170},1,Qt::NoModifier); controller->pointerRelease({170,170},Qt::NoModifier);
    QVERIFY(!doc.canUndo()); QVERIFY(doc.planes()[0]==p);
}

void PlaneEditToolTest::extrusionCanUseAdjacentUnlockedEdgeAndCannotRepeatSharedEdge()
{
    VpController testController; PlaneEditCanvas canvas; QVERIFY(setupEditCanvas(canvas, testController)); auto *controller=canvas.controller(); auto &doc=controller->document(); auto p=editPlane();
    p.quad().setCanvasCorners({QPointF(100,100),QPointF(220,100),QPointF(220,200),QPointF(100,200)});
    p.setEdgeLocked(0,true); doc.appendPlane(p); doc.resetHistory(); canvas.controller()->setTool(VpTools::EditPlane);
    controller->pointerPress({220,150},1,Qt::ControlModifier); controller->pointerRelease({240,100},Qt::ControlModifier);
    QCOMPARE(doc.planes().size(),2); QVERIFY(doc.planes()[0].isEdgeLocked(1));
    controller->pointerPress({220,150},1,Qt::ControlModifier); controller->pointerRelease({260,100},Qt::ControlModifier);
    QCOMPARE(doc.planes().size(),2); QCOMPARE(doc.planes()[0].quad().canvasCorners(),p.quad().canvasCorners());
}

void PlaneEditToolTest::adjacentMidpointsExtendAfterExtrusion_data()
{
    QTest::addColumn<int>("planeIndex"); QTest::addColumn<int>("side");
    QTest::newRow("parent-next") << 0 << 1;
    QTest::newRow("parent-previous") << 0 << 3;
    QTest::newRow("child-next") << 1 << 1;
    QTest::newRow("child-previous") << 1 << 3;
}

void PlaneEditToolTest::adjacentMidpointsExtendAfterExtrusion()
{
    QFETCH(int, planeIndex); QFETCH(int, side);
    VpController testController; PlaneEditCanvas canvas; QVERIFY(setupEditCanvas(canvas, testController)); auto *controller = canvas.controller();
    auto &doc = controller->document(); auto parent = editPlane();
    parent.quad().setCanvasCorners({QPointF(100,100), QPointF(220,100), QPointF(220,200), QPointF(100,200)});
    doc.appendPlane(parent); controller->setTool(VpTools::EditPlane);
    controller->pointerPress({220,150}, 1, Qt::ControlModifier);
    controller->pointerRelease({240,100}, Qt::ControlModifier);
    QCOMPARE(doc.planes().size(), 2);
    doc.resetHistory(); doc.setSelectedPlane(planeIndex);
    const auto original = doc.planes()[planeIndex];
    const auto other = doc.planes()[1-planeIndex];
    const int seam = planeIndex == 0 ? 1 : 0;
    const int edge = (seam + side) % 4;
    const auto corners = original.quad().canvasCorners();
    const QPointF midpoint = (corners[edge] + corners[(edge+1)%4]) / 2;
    QVERIFY(original.controlPointEditable(4 + edge));
    QVERIFY(!original.controlPointEditable(4 + seam));
    QVERIFY(!original.controlPointEditable(seam));
    QVERIFY(!original.controlPointEditable((seam+1)%4));

    QImage guides(400,400,QImage::Format_ARGB32); guides.fill(Qt::transparent);
    QPainter painter(&guides); SceneRenderer::Guides options; options.editHandlesVisible = true;
    SceneRenderer(doc).renderGuides(painter, 1, options); painter.end();
    QCOMPARE(guides.pixelColor(midpoint.toPoint()), QColor("#f3f8fa"));

    QPointF axis = midpoint - (corners[(edge+2)%4] + corners[(edge+3)%4]) / 2;
    axis /= QLineF(QPointF(), axis).length();
    controller->pointerPress(midpoint, 1, Qt::NoModifier);
    controller->pointerMove(midpoint + axis * 10, Qt::NoModifier);
    controller->pointerRelease(midpoint + axis * 20, Qt::NoModifier);
    const auto extended = doc.planes()[planeIndex];
    QVERIFY(extended.quad().canvasCorners() != corners);
    QVERIFY(extended.quad().isProjectable());
    QVERIFY(doc.planes()[1-planeIndex] == other);
    QCOMPARE(extended.lockedEdgeMask(), original.lockedEdgeMask());
    QCOMPARE(extended.parentPlaneIndex(), original.parentPlaneIndex());
    QCOMPARE(extended.parentEdgeIndex(), original.parentEdgeIndex());
    const QPointF seamVector = corners[(seam+1)%4] - corners[seam];
    for (int corner : {seam, (seam+1)%4}) {
        const QPointF offset = extended.quad().canvasCorners()[corner] - corners[seam];
        QVERIFY(qAbs(seamVector.x()*offset.y() - seamVector.y()*offset.x()) < 1e-6);
    }
    QVERIFY(doc.canUndo()); controller->undo();
    QVERIFY(doc.planes()[planeIndex] == original); QVERIFY(!doc.canUndo());
    controller->redo(); QVERIFY(doc.planes()[planeIndex] == extended);
}

void PlaneEditToolTest::cornerEditsRestoreGeometryAndDiagnosticColor()
{
    const auto check = [](bool passed, const char *message) { QVERIFY2(passed,message); };
    using Status = PerspectiveQuad::Status;
    const QSize size(300,300);
    const PerspectiveQuad::Corners surface{{{0,0},{200,0},{200,150},{0,150}}};
    const PerspectiveQuad rectangle({QPointF(50,50),QPointF(250,50),QPointF(250,250),QPointF(50,250)},surface);
    QImage background(size,QImage::Format_ARGB32_Premultiplied); background.fill(Qt::white);
    VpController controller; controller.document().setImage(background);
    for (const QPointF &point : rectangle.canvasCorners()) controller.pointerPress(point, 1, Qt::NoModifier);
    const auto original = controller.document().planes()[0].quad();
    controller.pointerPress(QPointF(250, 250), 1, Qt::NoModifier);
    check(controller.draggedPlaneControlPoint() == 2, "press identifies active corner");
    for (const auto &point : {QPointF(100, 100), QPointF(250, 50), QPointF(55, 50)}) {
        controller.pointerMove(point, Qt::NoModifier);
        check(controller.document().planes()[0].quad() == original, "invalid corner drag leaves geometry unchanged");
    }
    controller.pointerRelease(QPointF(100, 100), Qt::NoModifier);
    check(controller.draggedPlaneControlPoint() == -1, "release clears active corner");
    controller.pointerPress(QPointF(250, 250), 1, Qt::NoModifier);
    controller.pointerMove(QPointF(280, 250), Qt::NoModifier);
    const auto moved = controller.document().planes()[0].quad();
    check(moved.isValid() && moved.canvasCorners()[2] == QPointF(280, 250), "valid corner movement allowed");
    check(moved.status(size) != Status::Valid, "valid corner movement updates warning color");
    controller.pointerRelease(QPointF(280, 250), Qt::NoModifier);
    controller.undo();
    check(controller.document().planes()[0].quad() == original, "undo restores geometry and color");
    controller.redo();
    check(controller.document().planes()[0].quad() == moved, "redo restores geometry and color");
    controller.pointerPress(QPointF(280, 250), 1, Qt::NoModifier);
    controller.pointerMove(QPointF(240, 210), Qt::NoModifier);
    controller.cancelInteraction();
    check(controller.document().planes()[0].quad() == moved, "cancel restores prior geometry and color");
}

void PlaneEditToolTest::rotationTracksDirectionAndWraps_data()
{
    QTest::addColumn<int>("edge");
    QTest::addColumn<int>("direction");
    for (int edge = 0; edge < 4; ++edge) for (int direction : {-1,1}) {
        const auto row = QString("edge-%1-direction-%2").arg(edge).arg(direction).toLatin1();
        QTest::newRow(row.constData()) << edge << direction;
    }
}

void PlaneEditToolTest::rotationTracksDirectionAndWraps()
{
    QFETCH(int,edge);
    QFETCH(int,direction);
    auto source = interactionPlane();
    source.setEdgeLocked(edge,true);
    source.setParent(3,edge);
    const auto pivot = source.quad().controlPoints()[4+edge];
    const auto press = polarPoint(pivot,direction*170);
    PlaneEditTool tool;
    tool.begin(source,press,4+(edge+2)%4,(edge+2)%4,false,editCanvasSize,true,edge);
    QVERIFY(tool.rotating());
    QVERIFY(!tool.extruding());
    QVERIFY(!tool.resizing());
    PerspectivePlane result;
    for (int angle : {179,181,190}) {
        QVERIFY(tool.update(polarPoint(pivot,direction*angle),&result));
        QVERIFY(qAbs(result.angleToParentDegrees()-(90+direction*(angle-170))) < 1e-8);
        QCOMPARE(result.quad().canvasCorners()[edge],source.quad().canvasCorners()[edge]);
        QCOMPARE(result.quad().canvasCorners()[(edge+1)%4],source.quad().canvasCorners()[(edge+1)%4]);
        QCOMPARE(result.quad().surfaceCorners(),source.quad().surfaceCorners());
        QCOMPARE(result.surfaceGroupId(),source.surfaceGroupId());
        QCOMPARE(result.parentPlaneIndex(),source.parentPlaneIndex());
        QCOMPARE(result.parentEdgeIndex(),source.parentEdgeIndex());
        QCOMPARE(result.lockedEdgeMask(),source.lockedEdgeMask());
        QVERIFY(result.hasCustomAngle());
    }
    QVERIFY(!(result.quad() == source.quad()));
    const auto last = result;
    QVERIFY(tool.update(polarPoint(pivot,direction*190),&result));
    QVERIFY(result == last);
    for (int angle : {181,179,170})
        QVERIFY(tool.update(polarPoint(pivot,direction*angle),&result));
    QVERIFY(result == source);
}

void PlaneEditToolTest::dragCommitsOneHistoryEntry_data() { addActionRows(); }

void PlaneEditToolTest::dragCommitsOneHistoryEntry()
{
    QFETCH(int,action);
    VpController controller;
    EditGesture gesture;
    QVERIFY(setupGesture(controller,action,&gesture));
    auto &doc = controller.document();
    const auto before = doc.planes();
    controller.pointerPress(gesture.press,1,gesture.modifiers);
    controller.pointerMove((gesture.press+gesture.target)/2,gesture.modifiers);
    controller.pointerMove(gesture.target,gesture.modifiers);
    QVERIFY(doc.editActive());
    QVERIFY(!controller.canUndo());
    if (action == ExtrudePlane) {
        QVERIFY(doc.planes() == before);
        QVERIFY(controller.extrudePreview() != nullptr);
    }
    controller.pointerRelease(gesture.target,gesture.modifiers);
    const auto after = doc.planes();
    const int selectedAfter = doc.selectedPlane();
    QVERIFY(!(after == before));
    QCOMPARE(after.size(),before.size()+(action == ExtrudePlane ? 1 : 0));
    if (action != ExtrudePlane) {
        const auto &edited = after[gesture.index];
        const auto &original = before[gesture.index];
        QCOMPARE(edited.surfaceGroupId(),original.surfaceGroupId());
        QCOMPARE(edited.parentPlaneIndex(),original.parentPlaneIndex());
        QCOMPARE(edited.parentEdgeIndex(),original.parentEdgeIndex());
        QCOMPARE(edited.lockedEdgeMask(),original.lockedEdgeMask());
        if (action == RotatePlane) {
            QVERIFY(qAbs(edited.angleToParentDegrees()-original.angleToParentDegrees()-10) < 1e-8);
            QVERIFY(edited.hasCustomAngle());
            QCOMPARE(edited.quad().surfaceCorners(),original.quad().surfaceCorners());
            QCOMPARE(edited.quad().canvasCorners()[0],original.quad().canvasCorners()[0]);
            QCOMPARE(edited.quad().canvasCorners()[1],original.quad().canvasCorners()[1]);
        } else {
            QCOMPARE(edited.angleToParentDegrees(),original.angleToParentDegrees());
            QCOMPARE(edited.hasCustomAngle(),original.hasCustomAngle());
        }
    }
    QVERIFY(!doc.editActive());
    QVERIFY(controller.extrudePreview() == nullptr);
    QCOMPARE(controller.draggedPlaneControlPoint(),-1);
    QVERIFY(controller.canUndo());
    QVERIFY(!controller.canRedo());
    QVERIFY(!doc.hasPaintContent());
    controller.undo();
    QVERIFY(doc.planes() == before);
    QCOMPARE(doc.selectedPlane(),gesture.index);
    QVERIFY(!controller.canUndo());
    QVERIFY(controller.canRedo());
    controller.redo();
    QVERIFY(doc.planes() == after);
    QCOMPARE(doc.selectedPlane(),selectedAfter);
    QVERIFY(!controller.canRedo());
}

void PlaneEditToolTest::cancelledDragsRestoreDocument_data()
{
    QTest::addColumn<int>("action");
    QTest::addColumn<int>("nextTool");
    for (int action = MovePlane; action <= RotatePlane; ++action)
        for (int next : {-1,int(VpTools::Hand),int(VpTools::Zoom),int(VpTools::CreatePlane)}) {
            const auto row = QString("action-%1-tool-%2").arg(action).arg(next).toLatin1();
            QTest::newRow(row.constData()) << action << next;
        }
}

void PlaneEditToolTest::cancelledDragsRestoreDocument()
{
    QFETCH(int,action);
    QFETCH(int,nextTool);
    VpController controller;
    EditGesture gesture;
    QVERIFY(setupGesture(controller,action,&gesture));
    auto &doc = controller.document();
    const auto before = doc.planes();
    controller.pointerPress(gesture.press,1,gesture.modifiers);
    controller.pointerMove(gesture.target,gesture.modifiers);
    QVERIFY(doc.editActive());
    if (nextTool < 0) controller.cancelInteraction();
    else controller.setTool(static_cast<VpTools::Tool>(nextTool));
    QVERIFY(doc.planes() == before);
    QCOMPARE(doc.selectedPlane(),gesture.index);
    QVERIFY(!doc.editActive());
    QVERIFY(controller.extrudePreview() == nullptr);
    QCOMPARE(controller.draggedPlaneControlPoint(),-1);
    controller.pointerMove(gesture.target,gesture.modifiers);
    controller.pointerRelease(gesture.target,gesture.modifiers);
    QVERIFY(doc.planes() == before);
    QVERIFY(!controller.canUndo());
    QVERIFY(!controller.canRedo());
    controller.setTool(VpTools::EditPlane);
    controller.pointerPress(gesture.press,1,gesture.modifiers);
    controller.pointerRelease(gesture.target,gesture.modifiers);
    QVERIFY(!(doc.planes() == before));
    controller.undo();
    QVERIFY(doc.planes() == before);
    QVERIFY(!controller.canUndo());
}

void PlaneEditToolTest::repeatedUpdatesDoNotAccumulate_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<bool>("perspective");
    for (int mode = 0; mode < 3; ++mode) for (bool perspective : {false,true}) {
        const auto row = QString("mode-%1-perspective-%2").arg(mode).arg(perspective).toLatin1();
        QTest::newRow(row.constData()) << mode << perspective;
    }
}

void PlaneEditToolTest::repeatedUpdatesDoNotAccumulate()
{
    QFETCH(int,mode);
    QFETCH(bool,perspective);
    const auto source = interactionPlane(perspective);
    const int handle = mode == 0 ? -1 : mode == 1 ? 2 : 4;
    const auto press = handle < 0 ? QPointF(200,200) : source.quad().controlPoints()[handle];
    const auto delta = mode == 2 ? QPointF(0,-25) : QPointF(20,12);
    PlaneEditTool tool, direct;
    tool.begin(source,press,handle,mode == 2 ? 0 : -1,false,editCanvasSize);
    direct.begin(source,press,handle,mode == 2 ? 0 : -1,false,editCanvasSize);
    PerspectivePlane result, expected;
    QVERIFY(tool.update(press+delta/2,&result));
    QVERIFY(tool.update(press+delta,&result));
    QVERIFY(direct.update(press+delta,&expected));
    QVERIFY(result == expected);
    QVERIFY(!(result == source));
    QVERIFY(tool.update(press+delta,&result));
    QVERIFY(result == expected);
    QVERIFY(tool.update(press,&result));
    QVERIFY(result == source);
}

void PlaneEditToolTest::invalidCornerUpdatePreservesOutputAndRecovers_data()
{
    QTest::addColumn<int>("corner");
    QTest::addColumn<bool>("perspective");
    for (int corner = 0; corner < 4; ++corner) for (bool perspective : {false,true}) {
        const auto row = QString("corner-%1-perspective-%2").arg(corner).arg(perspective).toLatin1();
        QTest::newRow(row.constData()) << corner << perspective;
    }
}

void PlaneEditToolTest::invalidCornerUpdatePreservesOutputAndRecovers()
{
    QFETCH(int,corner);
    QFETCH(bool,perspective);
    const auto source = interactionPlane(perspective);
    const auto press = source.quad().canvasCorners()[corner];
    const auto delta = (press-QPointF(200,200))*.1;
    PlaneEditTool tool;
    tool.begin(source,press,corner,-1,false,editCanvasSize);
    PerspectivePlane result;
    QVERIFY(tool.update(press+delta,&result));
    const auto last = result;
    QVERIFY(!tool.update(source.quad().canvasCorners()[(corner+2)%4],&result));
    QVERIFY(result == last);
    QVERIFY(!tool.update(QPointF(std::numeric_limits<qreal>::quiet_NaN(),20),&result));
    QVERIFY(result == last);
    QVERIFY(tool.update(press+delta*2,&result));
    QCOMPARE(result.quad().canvasCorners()[corner],press+delta*2);
    QVERIFY(result.quad().isProjectable());
}

void PlaneEditToolTest::allHandlesPreserveUneditedGeometry_data()
{
    QTest::addColumn<int>("handle");
    QTest::addColumn<bool>("perspective");
    for (int handle = 0; handle < 8; ++handle) for (bool perspective : {false,true}) {
        const auto row = QString("handle-%1-perspective-%2").arg(handle).arg(perspective).toLatin1();
        QTest::newRow(row.constData()) << handle << perspective;
    }
}

void PlaneEditToolTest::allHandlesPreserveUneditedGeometry()
{
    QFETCH(int,handle);
    QFETCH(bool,perspective);
    auto source = interactionPlane(perspective);
    source.setParent(12,2);
    source.setAngleToParentDegrees(115);
    source.setHasCustomAngle(true);
    const auto press = source.quad().controlPoints()[handle];
    const auto target = press + (press-QPointF(200,200))*.1;
    const int edge = handle-4;
    PlaneEditTool tool;
    tool.begin(source,press,handle,edge,false,editCanvasSize);
    PerspectivePlane result;
    QVERIFY(tool.update(target,&result));
    QVERIFY(!(result.quad() == source.quad()));
    QVERIFY(result.quad().isProjectable());
    for (int i = 0; i < 4; ++i) {
        const bool changed = handle < 4 ? i == handle : i == edge || i == (edge+1)%4;
        if (!changed) {
            QCOMPARE(result.quad().canvasCorners()[i],source.quad().canvasCorners()[i]);
            QCOMPARE(result.quad().surfaceCorners()[i],source.quad().surfaceCorners()[i]);
        }
    }
    QCOMPARE(result.surfaceGroupId(),source.surfaceGroupId());
    QCOMPARE(result.parentPlaneIndex(),source.parentPlaneIndex());
    QCOMPARE(result.parentEdgeIndex(),source.parentEdgeIndex());
    QCOMPARE(result.lockedEdgeMask(),source.lockedEdgeMask());
    QCOMPARE(result.angleToParentDegrees(),source.angleToParentDegrees());
    QCOMPARE(result.hasCustomAngle(),source.hasCustomAngle());
}

void PlaneEditToolTest::lockedHandlesAndLinkedBodiesCannotMove_data()
{
    QTest::addColumn<int>("mask");
    QTest::addColumn<int>("handle");
    for (int mask = 1; mask < 16; ++mask) for (int handle = -1; handle < 8; ++handle) {
        const bool locked = handle < 0 || (handle < 4
            ? (mask & ((1<<handle)|(1<<((handle+3)%4)))) != 0
            : (mask & (1<<(handle-4))) != 0);
        if (!locked) continue;
        const auto row = QString("locks-%1-handle-%2").arg(mask).arg(handle).toLatin1();
        QTest::newRow(row.constData()) << mask << handle;
    }
    QTest::newRow("linked-child-body") << 0 << -2;
}

void PlaneEditToolTest::lockedHandlesAndLinkedBodiesCannotMove()
{
    QFETCH(int,mask);
    QFETCH(int,handle);
    VpController controller;
    auto &doc = controller.document();
    QPointF press;
    if (handle == -2) {
        EditGesture gesture;
        QVERIFY(setupGesture(controller,RotatePlane,&gesture));
        const auto &corners = doc.planes()[1].quad().canvasCorners();
        for (const auto &corner : corners) press += corner/4;
    } else {
        QVERIFY(doc.setImage(editBackground()));
        auto source = interactionPlane();
        for (int edge = 0; edge < 4; ++edge) source.setEdgeLocked(edge,(mask & (1<<edge)) != 0);
        QCOMPARE(doc.appendPlane(source),0);
        auto child = interactionPlane();
        auto corners = child.quad().canvasCorners();
        for (auto &corner : corners) corner += QPointF(400,0);
        child.quad().setCanvasCorners(corners);
        child.setParent(0,0);
        QCOMPARE(doc.appendPlane(child),1);
        doc.setSelectedPlane(0);
        controller.setTool(VpTools::EditPlane);
        doc.resetHistory();
        press = handle < 0 ? QPointF(200,200) : source.quad().controlPoints()[handle];
    }
    const auto before = doc.planes();
    const auto modifiers = handle >= 4 ? Qt::ControlModifier : Qt::NoModifier;
    controller.pointerPress(press,1,modifiers);
    controller.pointerMove(press+QPointF(20,12),modifiers);
    controller.pointerRelease(press+QPointF(20,12),modifiers);
    QVERIFY(doc.planes() == before);
    QVERIFY(!doc.editActive());
    QVERIFY(controller.extrudePreview() == nullptr);
    QVERIFY(!controller.canUndo());
}

void PlaneEditToolTest::extrusionPreviewAndCommitLifecycle_data()
{
    QTest::addColumn<int>("edge");
    for (int edge = 0; edge < 4; ++edge) QTest::newRow(qPrintable(QString::number(edge))) << edge;
}

void PlaneEditToolTest::extrusionPreviewAndCommitLifecycle()
{
    QFETCH(int,edge);
    VpController controller;
    auto &doc = controller.document();
    QVERIFY(doc.setImage(editBackground()));
    const auto source = interactionPlane();
    QCOMPARE(doc.appendPlane(source),0);
    doc.setSelectedPlane(0);
    controller.setTool(VpTools::EditPlane);
    doc.resetHistory();
    const auto before = doc.planes();
    QPointF press, target;
    QVERIFY(extrusionTarget(source,edge,&press,&target));
    controller.pointerPress(press,1,Qt::ControlModifier);
    controller.pointerMove(target,Qt::ControlModifier);
    QVERIFY(controller.extrudePreview() != nullptr);
    QVERIFY(doc.planes() == before);
    QVERIFY(!doc.planes()[0].isEdgeLocked(edge));
    QVERIFY(!controller.canUndo());
    controller.pointerMove(press,Qt::ControlModifier);
    controller.pointerMove(target+(target-press)*.2,Qt::ControlModifier);
    QVERIFY(controller.extrudePreview() != nullptr);
    const auto preview = *controller.extrudePreview();
    controller.pointerRelease(target+(target-press)*.2,Qt::ControlModifier);
    QCOMPARE(doc.planes().size(),2);
    const auto child = doc.planes()[1];
    QVERIFY(child.quad() == preview.quad());
    QCOMPARE(child.quad().canvasCorners()[0],source.quad().canvasCorners()[edge]);
    QCOMPARE(child.quad().canvasCorners()[1],source.quad().canvasCorners()[(edge+1)%4]);
    QCOMPARE(child.quad().surfaceCorners()[0],source.quad().surfaceCorners()[edge]);
    QCOMPARE(child.quad().surfaceCorners()[1],source.quad().surfaceCorners()[(edge+1)%4]);
    QCOMPARE(child.surfaceGroupId(),source.surfaceGroupId());
    QCOMPARE(child.parentPlaneIndex(),0);
    QCOMPARE(child.parentEdgeIndex(),edge);
    QCOMPARE(child.angleToParentDegrees(),qreal(90));
    QVERIFY(!child.hasCustomAngle());
    QVERIFY(child.isEdgeLocked(0));
    QVERIFY(doc.planes()[0].isEdgeLocked(edge));
    QCOMPARE(doc.selectedPlane(),1);
    QVERIFY(controller.planeAngleEditable());
    QVERIFY(controller.extrudePreview() == nullptr);
    const auto after = doc.planes();
    controller.undo();
    QVERIFY(doc.planes() == before);
    QCOMPARE(doc.selectedPlane(),0);
    QVERIFY(!controller.canUndo());
    controller.redo();
    QVERIFY(doc.planes() == after);
    QCOMPARE(doc.selectedPlane(),1);
}

void PlaneEditToolTest::invalidExtrusionDoesNotCommit_data() { extrusionPreviewAndCommitLifecycle_data(); }

void PlaneEditToolTest::invalidExtrusionDoesNotCommit()
{
    QFETCH(int,edge);
    VpController controller;
    auto &doc = controller.document();
    QVERIFY(doc.setImage(editBackground()));
    const auto source = interactionPlane();
    QCOMPARE(doc.appendPlane(source),0);
    controller.setTool(VpTools::EditPlane);
    doc.setSelectedPlane(0);
    doc.resetHistory();
    const auto before = doc.planes();
    QPointF press, target;
    QVERIFY(extrusionTarget(source,edge,&press,&target));
    const auto normal = (target-press)/QLineF(press,target).length();
    const auto tangentTarget = press + QPointF(-normal.y(),normal.x())*20;
    controller.pointerPress(press,1,Qt::ControlModifier);
    controller.pointerMove(tangentTarget,Qt::ControlModifier);
    QVERIFY(controller.extrudePreview() == nullptr);
    controller.pointerRelease(tangentTarget,Qt::ControlModifier);
    QVERIFY(doc.planes() == before);
    QVERIFY(!doc.editActive());
    QVERIFY(!controller.canUndo());
}

void PlaneEditToolTest::ctrlDragThresholdUsesScreenDistance_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<qreal>("pixels");
    for (qreal scale : {.25,1.0,4.0}) for (qreal pixels : {3.9,4.0,4.1}) {
        const auto row = QString("scale-%1-pixels-%2").arg(scale).arg(pixels).toLatin1();
        QTest::newRow(row.constData()) << scale << pixels;
    }
}

void PlaneEditToolTest::ctrlDragThresholdUsesScreenDistance()
{
    QFETCH(qreal,scale);
    QFETCH(qreal,pixels);
    VpController controller;
    auto &doc = controller.document();
    QVERIFY(doc.setImage(editBackground()));
    QCOMPARE(doc.appendPlane(interactionPlane()),0);
    QCOMPARE(doc.appendPlane(interactionPlane()),1);
    doc.setSelectedPlane(0);
    controller.setTool(VpTools::EditPlane);
    doc.resetHistory();
    const auto before = doc.planes();
    const QPointF press(200,200), target = press+QPointF(pixels/scale,0);
    controller.pointerPress(press,scale,Qt::ControlModifier);
    controller.pointerRelease(target,Qt::ControlModifier);
    QCOMPARE(doc.selectedPlane(),1);
    QCOMPARE(controller.canUndo(),pixels >= 4);
    if (pixels < 4) QVERIFY(doc.planes() == before);
    else {
        QVERIFY(doc.planes()[0] == before[0]);
        QVERIFY(QLineF(doc.planes()[1].quad().canvasCorners()[0],
            before[1].quad().canvasCorners()[0]+QPointF(pixels/scale,0)).length() < 1e-7);
        controller.undo();
        QVERIFY(doc.planes() == before);
    }
}

void PlaneEditToolTest::handleHitToleranceScalesWithView_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<bool>("inside");
    for (qreal scale : {.25,1.0,4.0}) for (bool inside : {false,true}) {
        const auto row = QString("scale-%1-inside-%2").arg(scale).arg(inside).toLatin1();
        QTest::newRow(row.constData()) << scale << inside;
    }
}

void PlaneEditToolTest::handleHitToleranceScalesWithView()
{
    QFETCH(qreal,scale);
    QFETCH(bool,inside);
    VpController controller;
    auto &doc = controller.document();
    QVERIFY(doc.setImage(editBackground()));
    const auto source = interactionPlane();
    QCOMPARE(doc.appendPlane(source),0);
    controller.setTool(VpTools::EditPlane);
    doc.resetHistory();
    const qreal tolerance = qMax(8.0/scale,4.0);
    const auto press = source.quad().canvasCorners()[0]-QPointF(tolerance+(inside ? -.2 : .2),0);
    controller.pointerPress(press,scale,Qt::NoModifier);
    QCOMPARE(controller.draggedPlaneControlPoint(),inside ? 0 : -1);
    QCOMPARE(doc.selectedPlane(),inside ? 0 : -1);
    controller.pointerRelease(press,Qt::NoModifier);
    QVERIFY(doc.planes()[0] == source);
    QVERIFY(!controller.canUndo());
}

void PlaneEditToolTest::unchangedGesturePreservesRedo_data() { addActionRows(); }

void PlaneEditToolTest::unchangedGesturePreservesRedo()
{
    QFETCH(int,action);
    VpController controller;
    EditGesture gesture;
    QVERIFY(setupGesture(controller,action,&gesture));
    auto &doc = controller.document();
    const auto before = doc.planes();
    doc.beginEdit();
    QVERIFY(doc.appendPlane(interactionPlane()) >= 0);
    doc.commitEdit(true);
    const auto redoBranch = doc.planes();
    controller.undo();
    QVERIFY(controller.canRedo());
    controller.pointerPress(gesture.press,1,gesture.modifiers);
    controller.pointerRelease(gesture.press,gesture.modifiers);
    QVERIFY(doc.planes() == before);
    QVERIFY(!controller.canUndo());
    QVERIFY(controller.canRedo());
    QVERIFY(!doc.editActive());
    controller.redo();
    QVERIFY(doc.planes() == redoBranch);
}

void PlaneEditToolTest::toolReuseAndInvalidOutputs()
{
    const auto source = interactionPlane();
    const auto pivot = source.quad().controlPoints()[4];
    PlaneEditTool tool;
    const auto press = polarPoint(pivot,170);
    tool.begin(source,press,6,2,false,editCanvasSize,true,0);
    PerspectivePlane result;
    QVERIFY(tool.update(polarPoint(pivot,190),&result));
    QVERIFY(qAbs(result.angleToParentDegrees()-110) < 1e-8);
    tool.begin(source,press,6,2,false,editCanvasSize,true,0);
    result = source;
    result.setSurfaceGroupId(99);
    const auto sentinel = result;
    QVERIFY(!tool.update(pivot,&result));
    QVERIFY(result == sentinel);
    QVERIFY(!tool.update(polarPoint(pivot,175),nullptr));
    QVERIFY(tool.update(polarPoint(pivot,175),&result));
    QVERIFY(qAbs(result.angleToParentDegrees()-95) < 1e-8);
    for (int edge : {-1,4}) {
        tool.begin(source,press,6,2,false,editCanvasSize,true,edge);
        result = sentinel;
        QVERIFY(!tool.update(polarPoint(pivot,175),&result));
        QVERIFY(result == sentinel);
    }
    for (int action = MovePlane; action <= ExtrudePlane; ++action) {
        const int handle = action == MovePlane ? -1 : action == MoveCorner ? 0 : 4;
        const auto point = handle < 0 ? QPointF(200,200) : source.quad().controlPoints()[handle];
        tool.begin(source,point,handle,handle >= 4 ? 0 : -1,action == ExtrudePlane,editCanvasSize);
        QVERIFY(!tool.rotating());
        QCOMPARE(tool.extruding(),action == ExtrudePlane);
        QCOMPARE(tool.resizing(),action == ExtendEdge);
        QVERIFY(!tool.update(point+QPointF(10,10),nullptr));
        if (action != ExtrudePlane) {
            QVERIFY(tool.update(point,&result));
            QVERIFY(result == source);
        }
    }
}

QTEST_MAIN(PlaneEditToolTest)
#include "tst_planeedittool.moc"
