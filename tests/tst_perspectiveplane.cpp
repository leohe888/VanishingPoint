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
    QVERIFY(makeTestPlane().facet().isValid());
}

void PerspectivePlaneTest::rejectsSelfIntersection()
{
    QVERIFY(!makeSelfIntersectingPlane().facet().isValid());
}

void PerspectivePlaneTest::findsHandles()
{
    const PerspectivePlane plane = makeTestPlane();
    QCOMPARE(plane.facet().controlPointIndexAt(QPointF(10, 10), 1.0), 0);
    QCOMPARE(plane.facet().controlPointIndexAt(QPointF(60, 10), 1.0), 4);
    QCOMPARE(plane.facet().controlPointIndexAt(QPointF(60, 45), 1.0), -1);
}

void PerspectivePlaneTest::selectsTopmostPlane()
{
    PerspectivePlane lower = makeTestPlane();
    PerspectivePlane upper = makeTestPlane();
    PerspectiveFacet::Corners corners = upper.facet().canvasCorners();
    for (QPointF &corner : corners)
        corner += QPointF(20, 10);
    upper.facet().setCanvasCorners(corners);
    QCOMPARE(topmostPlaneIndexAt({lower, upper}, QPointF(50, 40)), 1);
    QCOMPARE(topmostPlaneIndexAt({lower, upper}, QPointF(15, 15)), 0);
    QCOMPARE(topmostPlaneIndexAt({lower, upper}, QPointF(500, 500)), -1);
}

void PerspectivePlaneTest::rejectsNonFinitePlane()
{
    PerspectivePlane plane = makeTestPlane();
    QPointF invalidCorner = plane.facet().canvasCorners()[2];
    invalidCorner.setX(std::numeric_limits<qreal>::quiet_NaN());
    plane.facet().setCanvasCorner(2, invalidCorner);
    QVERIFY(!plane.facet().isValid());
}

void PerspectivePlaneTest::rejectsInvalidHitTestInput()
{
    const PerspectivePlane plane = makeTestPlane();
    QCOMPARE(plane.facet().controlPointIndexAt(QPointF(10, 10), -1.0), -1);
    QCOMPARE(plane.facet().edgeIndexAt(QPointF(60, 10), -1.0), -1);
}

void PerspectivePlaneTest::ignoresInvalidEdgeIndex()
{
    const PerspectivePlane plane = makeTestPlane();
    const PerspectivePlane result = resizePlaneFromEdge(
        plane, -1, QPointF(60, -10), QPointF(60, 10));
    QCOMPARE(result.facet().canvasCorners(), plane.facet().canvasCorners());
    QCOMPARE(result.facet().surfaceCorners(), plane.facet().surfaceCorners());
}

QTEST_APPLESS_MAIN(PerspectivePlaneTest)
#include "tst_perspectiveplane.moc"
