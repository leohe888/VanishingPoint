#include <QtTest>

#include "tools/planeedittool.h"
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
    void extendsAdjacentEdgesWithoutBendingSeam();
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

QTEST_APPLESS_MAIN(PlaneEditToolTest)
#include "tst_planeedittool.moc"
