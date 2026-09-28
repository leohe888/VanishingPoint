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
    void rotationPreservesGeometry_data();
    void rotationPreservesGeometry();
    void rotationRoundTrip();
    void rotationRejectsInvalidInput();
    void normalAndExtrusionPreserveGeometry();
};

void PerspectivePlaneTest::validatesConvexPlane()
{
    QVERIFY(makeTestPlane().quad().isValid());
}

void PerspectivePlaneTest::rejectsSelfIntersection()
{
    QVERIFY(!makeSelfIntersectingPlane().quad().isValid());
}

void PerspectivePlaneTest::findsHandles()
{
    const PerspectivePlane plane = makeTestPlane();
    QCOMPARE(plane.quad().controlPointIndexAt(QPointF(10, 10), 1.0), 0);
    QCOMPARE(plane.quad().controlPointIndexAt(QPointF(60, 10), 1.0), 4);
    QCOMPARE(plane.quad().controlPointIndexAt(QPointF(60, 45), 1.0), -1);
}

void PerspectivePlaneTest::selectsTopmostPlane()
{
    PerspectivePlane lower = makeTestPlane();
    PerspectivePlane upper = makeTestPlane();
    PerspectiveQuad::Corners corners = upper.quad().canvasCorners();
    for (QPointF &corner : corners)
        corner += QPointF(20, 10);
    upper.quad().setCanvasCorners(corners);
    QCOMPARE(topmostPlaneIndexAt({lower, upper}, QPointF(50, 40)), 1);
    QCOMPARE(topmostPlaneIndexAt({lower, upper}, QPointF(15, 15)), 0);
    QCOMPARE(topmostPlaneIndexAt({lower, upper}, QPointF(500, 500)), -1);
}

void PerspectivePlaneTest::rejectsNonFinitePlane()
{
    PerspectivePlane plane = makeTestPlane();
    QPointF invalidCorner = plane.quad().canvasCorners()[2];
    invalidCorner.setX(std::numeric_limits<qreal>::quiet_NaN());
    plane.quad().setCanvasCorner(2, invalidCorner);
    QVERIFY(!plane.quad().isValid());
}

void PerspectivePlaneTest::rejectsInvalidHitTestInput()
{
    const PerspectivePlane plane = makeTestPlane();
    QCOMPARE(plane.quad().controlPointIndexAt(QPointF(10, 10), -1.0), -1);
    QCOMPARE(plane.quad().edgeIndexAt(QPointF(60, 10), -1.0), -1);
}

void PerspectivePlaneTest::ignoresInvalidEdgeIndex()
{
    const PerspectivePlane plane = makeTestPlane();
    const PerspectivePlane result = resizePlaneFromEdge(
        plane, -1, QPointF(60, -10), QPointF(60, 10));
    QCOMPARE(result.quad().canvasCorners(), plane.quad().canvasCorners());
    QCOMPARE(result.quad().surfaceCorners(), plane.quad().surfaceCorners());
}

void PerspectivePlaneTest::rotationPreservesGeometry_data()
{
    QTest::addColumn<bool>("parallel");
    QTest::addColumn<int>("edge");
    QTest::addColumn<QPointF>("expectedFarB");
    QTest::addColumn<QPointF>("expectedFarA");
    // 原算法的画布坐标快照，覆盖有限消失点与无穷远消失点。
    const QPointF perspective[4][2] = {
        {{477.275222371835, 410.558882432588}, {201.894062862244, 363.279874946830}},
        {{222.165448258722, 362.542489028560}, {204.938558191327, 151.707096456816}},
        {{180.663062675965, 157.785106007997}, {520.921225819924, 191.651163630492}},
        {{506.444784536388, 180.808535438962}, {465.265486900063, 403.918126162137}}
    };
    const QPointF parallelEdges[4][2] = {
        {{542.325707557584, 369.440869520247}, {210.232389923221, 369.440869520247}},
        {{228.142614490479, 373.102012904904}, {228.142614490479, 190.346980642643}},
        {{210.232389923221, 192.605608464397}, {542.325707557584, 192.605608464397}},
        {{526.168627443956, 190.346980642643}, {526.168627443956, 373.102012904904}}
    };
    for (int edge = 0; edge < 4; ++edge) {
        QTest::newRow(qPrintable(QString("perspective-edge-%1").arg(edge)))
            << false << edge << perspective[edge][0] << perspective[edge][1];
        QTest::newRow(qPrintable(QString("parallel-edge-%1").arg(edge)))
            << true << edge << parallelEdges[edge][0] << parallelEdges[edge][1];
    }
}

void PerspectivePlaneTest::rotationPreservesGeometry()
{
    QFETCH(bool, parallel);
    QFETCH(int, edge);
    QFETCH(QPointF, expectedFarB);
    QFETCH(QPointF, expectedFarA);
    PerspectivePlane plane = makeTestPlane();
    plane.quad().setCanvasCorners(parallel
        ? PerspectivePlane::Corners{QPointF(200, 180), QPointF(550, 180),
                                    QPointF(550, 380), QPointF(200, 380)}
        : PerspectivePlane::Corners{QPointF(120, 140), QPointF(540, 180),
                                    QPointF(480, 460), QPointF(160, 400)});
    plane.setParent(2, 3);
    plane.setEdgeLocked(edge, true);
    const auto result = rotatePlaneAroundEdge(plane, edge, 75.0, QSize(800, 600));
    QVERIFY(result.quad().isValid());
    QCOMPARE(result.angleToParentDegrees(), 75.0);
    QVERIFY(result.hasCustomAngle());
    QCOMPARE(result.quad().canvasCorners()[edge], plane.quad().canvasCorners()[edge]);
    QCOMPARE(result.quad().canvasCorners()[(edge + 1) % 4], plane.quad().canvasCorners()[(edge + 1) % 4]);
    QCOMPARE(result.quad().surfaceCorners(), plane.quad().surfaceCorners());
    QCOMPARE(result.parentPlaneIndex(), plane.parentPlaneIndex());
    QCOMPARE(result.parentEdgeIndex(), plane.parentEdgeIndex());
    QCOMPARE(result.surfaceGroupId(), plane.surfaceGroupId());
    QCOMPARE(result.lockedEdgeMask(), plane.lockedEdgeMask());
    QVERIFY(QLineF(result.quad().canvasCorners()[(edge + 2) % 4], expectedFarB).length() < 1e-6);
    QVERIFY(QLineF(result.quad().canvasCorners()[(edge + 3) % 4], expectedFarA).length() < 1e-6);
    // 角度跨越完整周数时，仍归一化为相同的角度与几何。
    for (qreal equivalentAngle : {435.0, -285.0}) {
        const auto wrapped = rotatePlaneAroundEdge(plane, edge, equivalentAngle, QSize(800, 600));
        QCOMPARE(wrapped.angleToParentDegrees(), 75.0);
        for (int i = 0; i < 4; ++i)
            QVERIFY(QLineF(wrapped.quad().canvasCorners()[i], result.quad().canvasCorners()[i]).length() < 1e-6);
    }
}

void PerspectivePlaneTest::rotationRoundTrip()
{
    const auto plane = makeTestPlane();
    const auto rotated = rotatePlaneAroundEdge(plane, 0, 75.0, QSize(800, 600));
    QVERIFY(rotated.hasCustomAngle());
    const auto restored = rotatePlaneAroundEdge(rotated, 0, 90.0, QSize(800, 600));
    QCOMPARE(restored.angleToParentDegrees(), 90.0);
    for (int i = 0; i < 4; ++i)
        QVERIFY(QLineF(restored.quad().canvasCorners()[i], plane.quad().canvasCorners()[i]).length() < 1e-6);
}

void PerspectivePlaneTest::rotationRejectsInvalidInput()
{
    const auto plane = makeTestPlane();
    for (int edge : {-1, 4}) {
        const auto result = rotatePlaneAroundEdge(plane, edge, 75.0, QSize(800, 600));
        QCOMPARE(result.quad().canvasCorners(), plane.quad().canvasCorners());
        QCOMPARE(result.hasCustomAngle(), plane.hasCustomAngle());
    }
    const auto invalidAngle = rotatePlaneAroundEdge(
        plane, 0, std::numeric_limits<qreal>::quiet_NaN(), QSize(800, 600));
    QCOMPARE(invalidAngle.quad().canvasCorners(), plane.quad().canvasCorners());
    const auto emptyImage = rotatePlaneAroundEdge(plane, 0, 75.0, QSize());
    QCOMPARE(emptyImage.quad().canvasCorners(), plane.quad().canvasCorners());
    auto degenerate = plane;
    degenerate.quad().setCanvasCorners({QPointF(), QPointF(10, 0), QPointF(20, 0), QPointF(30, 0)});
    const auto rejected = rotatePlaneAroundEdge(degenerate, 0, 75.0, QSize(800, 600));
    QCOMPARE(rejected.quad().canvasCorners(), degenerate.quad().canvasCorners());
}

void PerspectivePlaneTest::normalAndExtrusionPreserveGeometry()
{
    auto plane = makeTestPlane();
    plane.quad().setCanvasCorners({QPointF(120, 140), QPointF(540, 180),
                                  QPointF(480, 460), QPointF(160, 400)});
    const QPointF midpoint(330, 160);
    QPointF normal;
    QVERIFY(projectedNormalDirection(plane, midpoint, QSize(800, 600), &normal));
    QVERIFY(QLineF(normal, QPointF(0.975776726279, 0.218768783085)).length() < 1e-6);
    const auto extruded = extrudePerpendicularPlane(
        plane, 0, midpoint + normal * 100.0, midpoint, QSize(800, 600));
    QVERIFY(extruded.quad().isValid());
    QCOMPARE(extruded.quad().canvasCorners()[0], plane.quad().canvasCorners()[0]);
    QCOMPARE(extruded.quad().canvasCorners()[1], plane.quad().canvasCorners()[1]);
    QVERIFY(QLineF(extruded.quad().canvasCorners()[2],
                   QPointF(472.330285464783, 186.324015928538)).length() < 1e-6);
    QVERIFY(QLineF(extruded.quad().canvasCorners()[3],
                   QPointF(377.541063157113, 176.904661620943)).length() < 1e-6);
    const QPointF tangent(-normal.y(), normal.x());
    const auto lateral = extrudePerpendicularPlane(
        plane, 0, midpoint + normal * 100.0 + tangent * 80.0, midpoint, QSize(800, 600));
    for (int i = 0; i < 4; ++i)
        QVERIFY(QLineF(lateral.quad().canvasCorners()[i], extruded.quad().canvasCorners()[i]).length() < 1e-6);
}

QTEST_APPLESS_MAIN(PerspectivePlaneTest)
#include "tst_perspectiveplane.moc"
