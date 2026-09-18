#include <QtTest>

#include "core/planeedittool.h"
#include "testhelpers.h"

class PlaneEditToolTest : public QObject
{
    Q_OBJECT

private slots:
    void dragsCorner();
    void resizesFromEdgeMidpoint();
    void movesPlaneFromInterior();
    void rejectsInvalidCornerDrag();
    void extrudesPerpendicularPlaneFromEdgeMidpoint();
};

void PlaneEditToolTest::dragsCorner()
{
    const Plane plane = makeTestPlane();
    PlaneEditTool tool;
    tool.begin(plane, plane.corner[0], 0, -1, false, QSize(1200, 800));
    Plane result;
    QVERIFY(tool.update(QPointF(20, 20), &result));
    QCOMPARE(result.corner[0], QPointF(20, 20));
    QCOMPARE(result.corner[2], plane.corner[2]);
}

void PlaneEditToolTest::resizesFromEdgeMidpoint()
{
    const Plane plane = makeTestPlane();
    const QPointF midpoint(60, 10);
    PlaneEditTool tool;
    tool.begin(plane, midpoint, 4, 0, false, QSize(1200, 800));
    Plane result;
    QVERIFY(tool.update(QPointF(60, -10), &result));
    QVERIFY(PerspectivePlane::isValidPlane(result));
    QVERIFY(result.corner[0] != plane.corner[0]);
    QVERIFY(result.corner[1] != plane.corner[1]);
    QCOMPARE(result.corner[2], plane.corner[2]);
    QCOMPARE(result.corner[3], plane.corner[3]);
}

void PlaneEditToolTest::movesPlaneFromInterior()
{
    const Plane plane = makeTestPlane();
    PlaneEditTool tool;
    tool.begin(plane, QPointF(60, 45), -1, -1, false, QSize(1200, 800));
    Plane result;
    QVERIFY(tool.update(QPointF(80, 60), &result));
    QCOMPARE(result.corner[0], QPointF(30, 25));
    QCOMPARE(result.corner[2], QPointF(130, 95));
    QCOMPARE(result.surfaceCorner[0], QPointF(20, 15));
}

void PlaneEditToolTest::rejectsInvalidCornerDrag()
{
    const Plane plane = makeTestPlane();
    PlaneEditTool tool;
    tool.begin(plane, plane.corner[0], 0, -1, false, QSize(1200, 800));
    Plane result;
    QVERIFY(!tool.update(plane.corner[2], &result));
}

// Ctrl + 拖动边中点：从这条边拉出与之垂直的新平面。
void PlaneEditToolTest::extrudesPerpendicularPlaneFromEdgeMidpoint()
{
    const Plane plane = makeTestPlane();
    PlaneEditTool tool;
    tool.begin(plane, QPointF(60, 10), 4, 0, true, QSize(1200, 800));
    QVERIFY(tool.extruding());
    QCOMPARE(tool.edge(), 0);

    Plane result;
    QVERIFY(tool.update(QPointF(60, -40), &result));
    QVERIFY(PerspectivePlane::isValidPlane(result));
    QCOMPARE(result.corner[0], plane.corner[0]);       // 共用边原样继承
    QCOMPARE(result.corner[1], plane.corner[1]);
    QCOMPARE(result.surfaceCorner[0], plane.surfaceCorner[0]); // 接缝处曲面坐标相同
    QCOMPARE(result.surfaceCorner[1], plane.surfaceCorner[1]);
    QCOMPARE(int(result.lockedEdges), 1);              // 第 0 条边是与源平面共用的边
    QCOMPARE(result.surfaceGroup, plane.surfaceGroup); // 与源平面同属一个展开曲面
    QCOMPARE(result.relativeAngle, 90.0);              // 默认与源平面垂直
    QVERIFY(QLineF(result.corner[2], plane.corner[2]).length() > 1.0); // 外侧边已离开源平面

    PlaneEditTool tooShort;                            // 拖动距离过小时拉不出有效几何
    tooShort.begin(plane, QPointF(60, 10), 4, 0, true, QSize(1200, 800));
    QVERIFY(!tooShort.update(QPointF(60, 11), &result));
}

QTEST_APPLESS_MAIN(PlaneEditToolTest)
#include "tst_planeedittool.moc"
