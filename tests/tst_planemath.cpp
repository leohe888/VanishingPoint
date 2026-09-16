#include <QtTest>

#include "testhelpers.h"

class PlaneMathTest : public QObject
{
    Q_OBJECT

private slots:
    void validatesConvexPlane();
    void rejectsSelfIntersection();
    void findsHandles();
    void selectsTopmostPlane();
};

void PlaneMathTest::validatesConvexPlane()
{
    QVERIFY(PlaneMath::isValidPlane(makeTestPlane()));
}

void PlaneMathTest::rejectsSelfIntersection()
{
    QVERIFY(!PlaneMath::isValidPlane(makeSelfIntersectingPlane()));
}

void PlaneMathTest::findsHandles()
{
    const Plane plane = makeTestPlane();
    QCOMPARE(PlaneMath::handleAt(plane, QPointF(10, 10), 1.0), 0);
    QCOMPARE(PlaneMath::handleAt(plane, QPointF(60, 10), 1.0), 4);
    QCOMPARE(PlaneMath::handleAt(plane, QPointF(60, 45), 1.0), -1);
}

void PlaneMathTest::selectsTopmostPlane()
{
    Plane lower = makeTestPlane();
    Plane upper = makeTestPlane();
    for (QPointF &corner : upper.corner)
        corner += QPointF(20, 10);
    QCOMPARE(PlaneMath::planeAt({lower, upper}, QPointF(50, 40)), 1);
    QCOMPARE(PlaneMath::planeAt({lower, upper}, QPointF(15, 15)), 0);
    QCOMPARE(PlaneMath::planeAt({lower, upper}, QPointF(500, 500)), -1);
}

QTEST_APPLESS_MAIN(PlaneMathTest)
#include "tst_planemath.moc"
