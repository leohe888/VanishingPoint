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
    QVERIFY(PlaneMath::isValidPlane(result));
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

QTEST_APPLESS_MAIN(PlaneEditToolTest)
#include "tst_planeedittool.moc"
