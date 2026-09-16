#include <QtTest>

#include "core/planecreatetool.h"

class PlaneCreateToolTest : public QObject
{
    Q_OBJECT

private slots:
    void startsEmpty();
    void completesAfterFourPoints();
    void ignoresFifthPoint();
    void removesLastPoint();
    void createsExpectedPlane();
    void resetClearsState();
};

static void addRectangle(PlaneCreateTool &tool)
{
    tool.addPoint(QPointF(10, 10));
    tool.addPoint(QPointF(110, 10));
    tool.addPoint(QPointF(110, 80));
    tool.addPoint(QPointF(10, 80));
}

void PlaneCreateToolTest::startsEmpty()
{
    PlaneCreateTool tool;
    QVERIFY(!tool.active());
    QVERIFY(!tool.complete());
    QCOMPARE(tool.points().size(), 0);
}

void PlaneCreateToolTest::completesAfterFourPoints()
{
    PlaneCreateTool tool;
    addRectangle(tool);
    QVERIFY(tool.active());
    QVERIFY(tool.complete());
    QCOMPARE(tool.points().size(), 4);
}

void PlaneCreateToolTest::ignoresFifthPoint()
{
    PlaneCreateTool tool;
    addRectangle(tool);
    tool.addPoint(QPointF(200, 200));
    QCOMPARE(tool.points().size(), 4);
    QCOMPARE(tool.points().last(), QPointF(10, 80));
}

void PlaneCreateToolTest::removesLastPoint()
{
    PlaneCreateTool tool;
    addRectangle(tool);

    tool.removeLastPoint();
    QCOMPARE(tool.points().size(), 3);
    QVERIFY(tool.active());
    QVERIFY(!tool.complete());

    // 回退到空，继续回退应安全无副作用
    for (int i = 0; i < 4; ++i)
        tool.removeLastPoint();
    QCOMPARE(tool.points().size(), 0);
    QVERIFY(!tool.active());

    // 回退后仍可继续落点
    tool.addPoint(QPointF(20, 20));
    QCOMPARE(tool.points().size(), 1);
    QCOMPARE(tool.points().first(), QPointF(20, 20));
}

void PlaneCreateToolTest::createsExpectedPlane()
{
    PlaneCreateTool tool;
    addRectangle(tool);
    const Plane plane = tool.makePlane(3, QStringLiteral("测试平面"));
    QCOMPARE(plane.corner[0], QPointF(10, 10));
    QCOMPARE(plane.corner[2], QPointF(110, 80));
    QCOMPARE(plane.surfaceCorner[2], QPointF(100, 70));
    QCOMPARE(plane.surfaceGroup, 3);
    QCOMPARE(plane.name, QStringLiteral("测试平面"));
    QVERIFY(PlaneMath::isValidPlane(plane));
}

void PlaneCreateToolTest::resetClearsState()
{
    PlaneCreateTool tool;
    tool.addPoint(QPointF(10, 10));
    tool.reset();
    QVERIFY(!tool.active());
    QCOMPARE(tool.points().size(), 0);
}

QTEST_APPLESS_MAIN(PlaneCreateToolTest)
#include "tst_planecreatetool.moc"
