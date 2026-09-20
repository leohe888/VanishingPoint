#include <QtTest>

#include <limits>

#include "testhelpers.h"

class PerspectivePlaneTest : public QObject
{
    Q_OBJECT

private slots:
    void validatesConvexPlane();
    void rejectsSelfIntersection();
    void findsHandles();
    void selectsTopmostPlane();
    void rejectsNonFinitePlane();
    void rejectsInvalidHitTestInput();
    void ignoresInvalidEdgeIndex();
};

void PerspectivePlaneTest::validatesConvexPlane()
{
    QVERIFY(PerspectivePlane::isValidPlane(makeTestPlane()));
}

void PerspectivePlaneTest::rejectsSelfIntersection()
{
    QVERIFY(!PerspectivePlane::isValidPlane(makeSelfIntersectingPlane()));
}

void PerspectivePlaneTest::findsHandles()
{
    const Plane plane = makeTestPlane();
    QCOMPARE(PerspectivePlane::handleAt(plane, QPointF(10, 10), 1.0), 0);
    QCOMPARE(PerspectivePlane::handleAt(plane, QPointF(60, 10), 1.0), 4);
    QCOMPARE(PerspectivePlane::handleAt(plane, QPointF(60, 45), 1.0), -1);
}

void PerspectivePlaneTest::selectsTopmostPlane()
{
    Plane lower = makeTestPlane();
    Plane upper = makeTestPlane();
    for (QPointF &corner : upper.corner)
        corner += QPointF(20, 10);
    QCOMPARE(PerspectivePlane::planeAt({lower, upper}, QPointF(50, 40)), 1);
    QCOMPARE(PerspectivePlane::planeAt({lower, upper}, QPointF(15, 15)), 0);
    QCOMPARE(PerspectivePlane::planeAt({lower, upper}, QPointF(500, 500)), -1);
}

void PerspectivePlaneTest::rejectsNonFinitePlane()
{
    Plane plane = makeTestPlane();
    plane.corner[2].setX(std::numeric_limits<qreal>::quiet_NaN());
    QVERIFY(!PerspectivePlane::isValidPlane(plane));
}

void PerspectivePlaneTest::rejectsInvalidHitTestInput()
{
    const Plane plane = makeTestPlane();
    QCOMPARE(PerspectivePlane::handleAt(plane, QPointF(10, 10), -1.0), -1);
    QCOMPARE(PerspectivePlane::edgeAt(plane, QPointF(60, 10), -1.0), -1);
}

void PerspectivePlaneTest::ignoresInvalidEdgeIndex()
{
    const Plane plane = makeTestPlane();
    const Plane result = PerspectivePlane::resizePlaneAlongEdge(
        plane, -1, QPointF(60, -10), QPointF(60, 10));
    QCOMPARE(result.corner, plane.corner);
    QCOMPARE(result.surfaceCorner, plane.surfaceCorner);
}

QTEST_APPLESS_MAIN(PerspectivePlaneTest)
#include "tst_perspectiveplane.moc"
