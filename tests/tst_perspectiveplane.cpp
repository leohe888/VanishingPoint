#include <QtTest>

#include <cmath>
#include <limits>

#include "testhelpers.h"

namespace {

using Corners = PerspectivePlane::Corners;

void comparePoint(const QPointF &actual, const QPointF &expected, qreal tolerance = 1e-6)
{
    const QString message = QStringLiteral("actual (%1, %2), expected (%3, %4)")
        .arg(actual.x(), 0, 'g', 15).arg(actual.y(), 0, 'g', 15)
        .arg(expected.x(), 0, 'g', 15).arg(expected.y(), 0, 'g', 15);
    QVERIFY2(QLineF(actual, expected).length() <= tolerance, qPrintable(message));
}

void compareRelations(const PerspectivePlane &actual, const PerspectivePlane &expected)
{
    QCOMPARE(actual.surfaceGroupId(), expected.surfaceGroupId());
    QCOMPARE(actual.lockedEdgeMask(), expected.lockedEdgeMask());
    QCOMPARE(actual.parentPlaneIndex(), expected.parentPlaneIndex());
    QCOMPARE(actual.parentEdgeIndex(), expected.parentEdgeIndex());
}

void comparePlane(const PerspectivePlane &actual, const PerspectivePlane &expected)
{
    QCOMPARE(actual.quad().canvasCorners(), expected.quad().canvasCorners());
    QCOMPARE(actual.quad().surfaceCorners(), expected.quad().surfaceCorners());
    compareRelations(actual, expected);
    QCOMPARE(actual.angleToParentDegrees(), expected.angleToParentDegrees());
    QCOMPARE(actual.hasCustomAngle(), expected.hasCustomAngle());
}

PerspectivePlane withMetadata(PerspectivePlane plane)
{
    plane.setSurfaceGroupId(17);
    plane.setParent(8, 2);
    plane.setEdgeLocked(1, true);
    plane.setEdgeLocked(3, true);
    plane.setAngleToParentDegrees(90);
    plane.setHasCustomAngle(false);
    return plane;
}

PerspectivePlane trapezoidPlane()
{
    return withMetadata(PerspectivePlane(
        {QPointF(100, 0), QPointF(300, 0), QPointF(360, 200), QPointF(40, 200)},
        {QPointF(0, 0), QPointF(100, 0), QPointF(100, 100), QPointF(0, 100)}));
}

// 该梯形的解析投影，独立于待测的 PerspectiveTransform。
QPointF projectTrapezoid(const QPointF &surface)
{
    const double u = surface.x() / 100.0;
    const double v = surface.y() / 100.0;
    return QPointF((100 + 200 * u - 75 * v) / (1 - 0.375 * v),
                   125 * v / (1 - 0.375 * v));
}

struct WorldPoint {
    double x;
    double y;
    double z;
};

// 测试场景直接给定世界角点、正交基和相机，不调用生产代码恢复姿态。
struct CameraScene {
    QSize size;
    double focal;
    std::array<WorldPoint, 4> corners;
    WorldPoint u;
    WorldPoint v;
    WorldPoint normal;
    double width = 240;
    double depth = 160;

    QPointF project(const WorldPoint &point) const
    {
        return QPointF(size.width() / 2.0 + focal * point.x / point.z,
                       size.height() / 2.0 + focal * point.y / point.z);
    }

    PerspectivePlane plane() const
    {
        Corners canvas;
        for (int i = 0; i < 4; ++i)
            canvas[i] = project(corners[i]);
        return withMetadata(PerspectivePlane(canvas,
            {QPointF(0, 0), QPointF(width, 0), QPointF(width, depth), QPointF(0, depth)}));
    }

    // 对侧角点的偏移只沿 u 或 v；解析旋转后为 offset*cos(delta)-normal*length*sin(delta)。
    QPointF rotatedOuter(int edge, bool atB, double deltaDegrees) const
    {
        const WorldPoint base = corners[atB ? (edge + 1) % 4 : edge];
        WorldPoint offset = edge % 2 == 0 ? v : u;
        const double sign = edge == 0 || edge == 3 ? 1.0 : -1.0;
        const double length = edge % 2 == 0 ? depth : width;
        const double radians = deltaDegrees * std::acos(-1.0) / 180.0;
        const double cosine = std::cos(radians);
        const double sine = std::sin(radians);
        return project({base.x + length * (sign * offset.x * cosine - normal.x * sine),
                        base.y + length * (sign * offset.y * cosine - normal.y * sine),
                        base.z + length * (sign * offset.z * cosine - normal.z * sine)});
    }
};

CameraScene frontScene()
{
    return {QSize(800, 600), 960,
            {{{-120, -80, 800}, {120, -80, 800}, {120, 80, 800}, {-120, 80, 800}}},
            {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
}

CameraScene tiltedScene()
{
    // u、v 为单位正交向量；两个消失点均有限，可恢复真实焦距 600，而非经验值 1200。
    return {QSize(1000, 800), 600,
            {{{-120, -100, 1000}, {72, -100, 1144}, {14.4, 28, 1220.8}, {-177.6, 28, 1076.8}}},
            {0.8, 0, 0.6}, {-0.36, 0.8, 0.48}, {-0.48, -0.6, 0.64}};
}

void verifyOnLine(const QPointF &point, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const QPointF ap = point - a;
    const qreal length = QLineF(a, b).length();
    QVERIFY(length > 1e-9);
    QVERIFY(qAbs(ab.x() * ap.y() - ab.y() * ap.x()) / length < 1e-6);
}

} // namespace

class PerspectivePlaneTest : public QObject
{
    Q_OBJECT

private slots:
    void selectsTopmostPlane();
    void ignoresInvalidEdgeIndex();
    void defaultState();
    void copiesOwnCoordinatesAndMetadata();
    void managesEdgeLocks();
    void parentLifecycle();
    void resolvesTopmostQuad();
    void resolvesCanvasFallback_data();
    void resolvesCanvasFallback();
    void translation_data();
    void translation();
    void translatesPerspectiveSurface_data();
    void translatesPerspectiveSurface();
    void translationRejectsNonFinite_data();
    void translationRejectsNonFinite();
    void translationFailureKeepsOutput();
    void translationSupportsAliasedOutput();
    void resizeEdges_data();
    void resizeEdges();
    void resizesPerspectiveEdge();
    void resizeFailureAndDegeneracy();
    void normalDirection_data();
    void normalDirection();
    void normalAtInfinity();
    void normalFailureKeepsOutput();
    void extrusion_data();
    void extrusion();
    void extrusionSmallDrag_data();
    void extrusionSmallDrag();
    void extrusionInvalidInput();
    void rotation_data();
    void rotation();
    void rotationInvalidInput_data();
    void rotationInvalidInput();
    void rotationRejectsDegenerateAndBehindCamera();
};







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





void PerspectivePlaneTest::ignoresInvalidEdgeIndex()
{
    const PerspectivePlane plane = makeTestPlane();
    const PerspectivePlane result = resizePlaneFromEdge(
        plane, -1, QPointF(60, -10), QPointF(60, 10));
    QCOMPARE(result.quad().canvasCorners(), plane.quad().canvasCorners());
    QCOMPARE(result.quad().surfaceCorners(), plane.quad().surfaceCorners());
}

void PerspectivePlaneTest::defaultState()
{
    const PerspectivePlane plane;
    QCOMPARE(plane.quad().canvasCorners(), Corners{});
    QCOMPARE(plane.quad().surfaceCorners(), Corners{});
    QVERIFY(!plane.quad().isValid());
    QCOMPARE(plane.surfaceGroupId(), -1);
    QCOMPARE(plane.lockedEdgeMask(), quint8(0));
    QCOMPARE(plane.parentPlaneIndex(), -1);
    QCOMPARE(plane.parentEdgeIndex(), -1);
    QCOMPARE(plane.angleToParentDegrees(), qreal(90));
    QVERIFY(!plane.hasCustomAngle());
}

void PerspectivePlaneTest::copiesOwnCoordinatesAndMetadata()
{
    Corners canvas = makeTestPlane().quad().canvasCorners();
    Corners surface = makeTestPlane().quad().surfaceCorners();
    PerspectivePlane plane = withMetadata(PerspectivePlane(canvas, surface));
    plane.setAngleToParentDegrees(123.5);
    plane.setHasCustomAngle(true);
    const PerspectivePlane original = plane;
    PerspectivePlane assigned;
    assigned = plane;
    canvas[0] = QPointF(999, 999);
    surface[0] = QPointF(-999, -999);
    comparePlane(assigned, original);
    plane.quad().setCanvasCorner(0, QPointF(20, 30));
    plane.quad().setSurfaceCorner(0, QPointF(5, 7));
    plane.setSurfaceGroupId(99);
    plane.setParent(1, 0);
    plane.setEdgeLocked(1, false);
    plane.setAngleToParentDegrees(45);
    plane.setHasCustomAngle(false);
    comparePlane(assigned, original);
    QCOMPARE(original.quad().canvasCorners(), makeTestPlane().quad().canvasCorners());
    QCOMPARE(original.quad().surfaceCorners(), makeTestPlane().quad().surfaceCorners());
}

void PerspectivePlaneTest::managesEdgeLocks()
{
    // 枚举所有组合，确保修改一位不会影响其他位，重复设置保持幂等。
    for (int mask = 0; mask < 16; ++mask) {
        PerspectivePlane plane;
        for (int edge = 0; edge < 4; ++edge)
            plane.setEdgeLocked(edge, mask & (1 << edge));
        QCOMPARE(plane.lockedEdgeMask(), quint8(mask));
        for (int edge = 0; edge < 4; ++edge) {
            QCOMPARE(plane.isEdgeLocked(edge), bool(mask & (1 << edge)));
            PerspectivePlane changed = plane;
            changed.setEdgeLocked(edge, true);
            changed.setEdgeLocked(edge, true);
            QCOMPARE(changed.lockedEdgeMask(), quint8(mask | (1 << edge)));
            changed.setEdgeLocked(edge, false);
            changed.setEdgeLocked(edge, false);
            QCOMPARE(changed.lockedEdgeMask(), quint8(mask & ~(1 << edge)));
        }
        for (int edge : {-1, 4, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()}) {
            QVERIFY(!plane.isEdgeLocked(edge));
            plane.setEdgeLocked(edge, true);
            plane.setEdgeLocked(edge, false);
            QCOMPARE(plane.lockedEdgeMask(), quint8(mask));
        }
    }
}

void PerspectivePlaneTest::parentLifecycle()
{
    PerspectivePlane plane = withMetadata(makeTestPlane());
    plane.setAngleToParentDegrees(137.5);
    plane.setHasCustomAngle(true);
    plane.setParent(42, 3);
    QCOMPARE(plane.parentPlaneIndex(), 42);
    QCOMPARE(plane.parentEdgeIndex(), 3);
    QCOMPARE(plane.angleToParentDegrees(), qreal(137.5));
    QVERIFY(plane.hasCustomAngle());
    const Corners canvas = plane.quad().canvasCorners();
    const Corners surface = plane.quad().surfaceCorners();
    plane.clearParent();
    plane.clearParent();
    QCOMPARE(plane.parentPlaneIndex(), -1);
    QCOMPARE(plane.parentEdgeIndex(), -1);
    QCOMPARE(plane.angleToParentDegrees(), qreal(90));
    QVERIFY(!plane.hasCustomAngle());
    QCOMPARE(plane.surfaceGroupId(), 17);
    QCOMPARE(plane.lockedEdgeMask(), quint8(10));
    QCOMPARE(plane.quad().canvasCorners(), canvas);
    QCOMPARE(plane.quad().surfaceCorners(), surface);
}

void PerspectivePlaneTest::resolvesTopmostQuad()
{
    PerspectivePlane upper = makeTestPlane();
    upper.quad().setSurfaceCorner(0, QPointF(-200, -300));
    QVector<PerspectivePlane> planes{makeTestPlane(), upper};
    PerspectiveQuad result;
    QVERIFY(resolveQuad(planes, QSize(), QPointF(50, 40), &result));
    QCOMPARE(result.canvasCorners(), upper.quad().canvasCorners());
    QCOMPARE(result.surfaceCorners(), upper.quad().surfaceCorners());
    planes[1].quad().setSurfaceCorner(0, QPointF(500, 600));
    QCOMPARE(result.surfaceCorners(), upper.quad().surfaceCorners());
    QVERIFY(!resolveQuad(planes, QSize(300, 200), QPointF(50, 40), nullptr));
    QCOMPARE(topmostPlaneIndexAt({}, QPointF(50, 40)), -1);
}

void PerspectivePlaneTest::resolvesCanvasFallback_data()
{
    QTest::addColumn<QSize>("size");
    QTest::addColumn<bool>("success");
    QTest::newRow("normal") << QSize(320, 200) << true;
    QTest::newRow("tiny") << QSize(1, 1) << true;
    QTest::newRow("empty") << QSize() << false;
    QTest::newRow("zero-width") << QSize(0, 100) << false;
    QTest::newRow("zero-height") << QSize(100, 0) << false;
    QTest::newRow("negative-width") << QSize(-10, 100) << false;
    QTest::newRow("negative-height") << QSize(100, -10) << false;
}

void PerspectivePlaneTest::resolvesCanvasFallback()
{
    QFETCH(QSize, size);
    QFETCH(bool, success);
    // 回退矩形与查询位置无关；同时验证空列表和有平面但未命中的情况。
    for (const QVector<PerspectivePlane> &planes : {QVector<PerspectivePlane>{}, QVector<PerspectivePlane>{makeTestPlane()}}) {
        PerspectiveQuad result = makeTestPlane().quad();
        const PerspectiveQuad original = result;
        QCOMPARE(resolveQuad(planes, size, QPointF(-1000, -1000), &result), success);
        if (success) {
            const Corners expected{QPointF(0, 0), QPointF(size.width(), 0),
                                   QPointF(size.width(), size.height()), QPointF(0, size.height())};
            QCOMPARE(result.canvasCorners(), expected);
            QCOMPARE(result.surfaceCorners(), expected);
        } else {
            QCOMPARE(result.canvasCorners(), original.canvasCorners());
            QCOMPARE(result.surfaceCorners(), original.surfaceCorners());
        }
    }
}

void PerspectivePlaneTest::translation_data()
{
    QTest::addColumn<QPointF>("press");
    QTest::addColumn<QPointF>("delta");
    QTest::newRow("positive") << QPointF(30, 40) << QPointF(20, 15);
    QTest::newRow("negative") << QPointF(30, 40) << QPointF(-40, -25);
    QTest::newRow("zero") << QPointF(30, 40) << QPointF();
    QTest::newRow("outside-quad") << QPointF(-500, 500) << QPointF(30, -20);
}

void PerspectivePlaneTest::translation()
{
    QFETCH(QPointF, press);
    QFETCH(QPointF, delta);
    const PerspectivePlane source = withMetadata(makeTestPlane());
    PerspectivePlane result;
    QVERIFY(translatePlaneOnSurface(source, press + delta, press, &result));
    for (int i = 0; i < 4; ++i) {
        comparePoint(result.quad().canvasCorners()[i], source.quad().canvasCorners()[i] + delta);
        comparePoint(result.quad().surfaceCorners()[i], source.quad().surfaceCorners()[i] + delta);
    }
    compareRelations(result, source);
    QCOMPARE(result.angleToParentDegrees(), source.angleToParentDegrees());
    QCOMPARE(result.hasCustomAngle(), source.hasCustomAngle());
    QVERIFY(result.quad().isValid());
}

void PerspectivePlaneTest::translatesPerspectiveSurface_data()
{
    QTest::addColumn<QPointF>("delta");
    QTest::newRow("positive") << QPointF(10, 5);
    QTest::newRow("negative") << QPointF(-10, -5);
    QTest::newRow("zero") << QPointF();
    QTest::newRow("extends-beyond-quad") << QPointF(40, 10);
}

void PerspectivePlaneTest::translatesPerspectiveSurface()
{
    QFETCH(QPointF, delta);
    const PerspectivePlane source = trapezoidPlane();
    const QPointF pressSurface(50, 50);
    PerspectivePlane result;
    QVERIFY(translatePlaneOnSurface(source, projectTrapezoid(pressSurface + delta),
                                   projectTrapezoid(pressSurface), &result));
    for (int i = 0; i < 4; ++i) {
        const QPointF expected = source.quad().surfaceCorners()[i] + delta;
        comparePoint(result.quad().surfaceCorners()[i], expected);
        comparePoint(result.quad().canvasCorners()[i], projectTrapezoid(expected));
    }
    compareRelations(result, source);
    QVERIFY(result.quad().isValid());
}

void PerspectivePlaneTest::translationRejectsNonFinite_data()
{
    QTest::addColumn<QPointF>("point");
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal inf = std::numeric_limits<qreal>::infinity();
    QTest::newRow("nan-x") << QPointF(nan, 20);
    QTest::newRow("nan-y") << QPointF(20, nan);
    QTest::newRow("positive-inf-x") << QPointF(inf, 20);
    QTest::newRow("negative-inf-x") << QPointF(-inf, 20);
    QTest::newRow("positive-inf-y") << QPointF(20, inf);
    QTest::newRow("negative-inf-y") << QPointF(20, -inf);
}

void PerspectivePlaneTest::translationRejectsNonFinite()
{
    QFETCH(QPointF, point);
    const PerspectivePlane source = withMetadata(makeTestPlane());
    const PerspectivePlane sentinel = withMetadata(trapezoidPlane());
    for (bool invalidPress : {false, true}) {
        PerspectivePlane result = sentinel;
        QVERIFY(!translatePlaneOnSurface(source, invalidPress ? QPointF(20, 20) : point,
                                         invalidPress ? point : QPointF(20, 20), &result));
        comparePlane(result, sentinel);
    }
    comparePlane(resizePlaneFromEdge(source, 0, point, QPointF(20, 20)), source);
    comparePlane(resizePlaneFromEdge(source, 0, QPointF(20, 20), point), source);
    QVERIFY(!extrudePerpendicularPlane(source, 0, point, QPointF(20, 20), QSize(800, 600)).quad().isValid());
    QVERIFY(!extrudePerpendicularPlane(source, 0, QPointF(20, 20), point, QSize(800, 600)).quad().isValid());
}

void PerspectivePlaneTest::translationFailureKeepsOutput()
{
    const PerspectivePlane sentinel = withMetadata(makeTestPlane());
    PerspectivePlane result = sentinel;
    QVERIFY(!translatePlaneOnSurface(sentinel, QPointF(20, 20), QPointF(10, 10), nullptr));
    QVERIFY(!translatePlaneOnSurface(PerspectivePlane(), QPointF(20, 20), QPointF(10, 10), &result));
    comparePlane(result, sentinel);
    PerspectivePlane invalidSurface = sentinel;
    invalidSurface.quad().setSurfaceCorners(Corners{});
    QVERIFY(!translatePlaneOnSurface(invalidSurface, QPointF(20, 20), QPointF(10, 10), &result));
    comparePlane(result, sentinel);
    for (const QPointF &drag : {QPointF(1e7, 0), QPointF(-1e7, 0), QPointF(0, 1e7), QPointF(0, -1e7)}) {
        QVERIFY(!translatePlaneOnSurface(sentinel, drag, QPointF(60, 45), &result));
        comparePlane(result, sentinel);
    }
    // 平移后顶边和底边跨越投影地平线，候选几何须被拒绝。
    const PerspectivePlane perspective = trapezoidPlane();
    QVERIFY(!translatePlaneOnSurface(perspective, projectTrapezoid(QPointF(50, 250)),
                                    projectTrapezoid(QPointF(50, 50)), &result));
    comparePlane(result, sentinel);
}

void PerspectivePlaneTest::translationSupportsAliasedOutput()
{
    PerspectivePlane source = trapezoidPlane();
    PerspectivePlane separate;
    const QPointF press = projectTrapezoid(QPointF(50, 50));
    const QPointF drag = projectTrapezoid(QPointF(60, 55));
    QVERIFY(translatePlaneOnSurface(source, drag, press, &separate));
    QVERIFY(translatePlaneOnSurface(source, drag, press, &source));
    comparePlane(source, separate);
    const PerspectivePlane original = source;
    QVERIFY(!translatePlaneOnSurface(source, QPointF(1e8, 1e8), press, &source));
    comparePlane(source, original);
}

void PerspectivePlaneTest::resizeEdges_data()
{
    QTest::addColumn<int>("edge");
    QTest::addColumn<qreal>("amount");
    for (int edge = 0; edge < 4; ++edge)
        for (qreal amount : {-15.0, 0.0, 15.0}) {
            const QByteArray name = QByteArray::number(edge) + "-" + QByteArray::number(amount);
            QTest::newRow(name.constData()) << edge << amount;
        }
}

void PerspectivePlaneTest::resizeEdges()
{
    QFETCH(int, edge);
    QFETCH(qreal, amount);
    const PerspectivePlane source = withMetadata(makeTestPlane());
    const QPointF directions[]{QPointF(0, -1), QPointF(1, 0), QPointF(0, 1), QPointF(-1, 0)};
    const QPointF delta = directions[edge] * amount;
    const int next = (edge + 1) % 4;
    const QPointF press = (source.quad().canvasCorners()[edge] + source.quad().canvasCorners()[next]) / 2;
    const QPointF tangent(-directions[edge].y(), directions[edge].x());
    const PerspectivePlane result = resizePlaneFromEdge(source, edge, press + delta, press);
    const PerspectivePlane withTangentialDrag = resizePlaneFromEdge(source, edge, press + delta + tangent * 25, press);
    for (int i = 0; i < 4; ++i) {
        const QPointF offset = i == edge || i == next ? delta : QPointF();
        comparePoint(result.quad().canvasCorners()[i], source.quad().canvasCorners()[i] + offset);
        comparePoint(result.quad().surfaceCorners()[i], source.quad().surfaceCorners()[i] + offset);
        comparePoint(withTangentialDrag.quad().canvasCorners()[i], result.quad().canvasCorners()[i]);
        comparePoint(withTangentialDrag.quad().surfaceCorners()[i], result.quad().surfaceCorners()[i]);
    }
    compareRelations(result, source);
    QCOMPARE(result.angleToParentDegrees(), source.angleToParentDegrees());
    QCOMPARE(result.hasCustomAngle(), source.hasCustomAngle());
    QVERIFY(result.quad().isValid());
}

void PerspectivePlaneTest::resizesPerspectiveEdge()
{
    const PerspectivePlane source = trapezoidPlane();
    const PerspectivePlane result = resizePlaneFromEdge(source, 1, QPointF(350, 140), QPointF(330, 100));
    // 两侧边的消失点为 (200, -1000/3)，新边经过 (350, 100)，与顶底边求交。
    comparePoint(result.quad().canvasCorners()[1], QPointF(4100.0 / 13, 0));
    comparePoint(result.quad().canvasCorners()[2], QPointF(5000.0 / 13, 200));
    comparePoint(result.quad().surfaceCorners()[1], QPointF(1400.0 / 13, 0));
    comparePoint(result.quad().surfaceCorners()[2], QPointF(1400.0 / 13, 100));
    for (int i : {0, 3}) {
        QCOMPARE(result.quad().canvasCorners()[i], source.quad().canvasCorners()[i]);
        QCOMPARE(result.quad().surfaceCorners()[i], source.quad().surfaceCorners()[i]);
    }
    compareRelations(result, source);
    QVERIFY(result.quad().isValid());
}

void PerspectivePlaneTest::resizeFailureAndDegeneracy()
{
    const PerspectivePlane source = withMetadata(makeTestPlane());
    for (int edge : {-1, 4, std::numeric_limits<int>::max()})
        comparePlane(resizePlaneFromEdge(source, edge, QPointF(60, -10), QPointF(60, 10)), source);
    const PerspectivePlane empty;
    comparePlane(resizePlaneFromEdge(empty, 0, QPointF(10, 10), QPointF()), empty);
    PerspectivePlane invalidSurface = source;
    invalidSurface.quad().setSurfaceCorners(Corners{});
    comparePlane(resizePlaneFromEdge(invalidSurface, 0, QPointF(60, -10), QPointF(60, 10)), invalidSurface);
    // 本函数可返回塌缩候选，最终有效性由调用方检查；不要误认所有失败都会回退。
    const PerspectivePlane collapsed = resizePlaneFromEdge(source, 0, QPointF(60, 80), QPointF(60, 10));
    QVERIFY(!collapsed.quad().isValid());
    compareRelations(collapsed, source);
}

void PerspectivePlaneTest::normalDirection_data()
{
    QTest::addColumn<bool>("tilted");
    QTest::addColumn<QPointF>("point");
    QTest::newRow("front-upper-left") << false << QPointF(300, 200);
    QTest::newRow("front-lower-right") << false << QPointF(500, 400);
    QTest::newRow("tilted-inside") << true << QPointF(460, 370);
    QTest::newRow("tilted-outside") << true << QPointF(-500, 800);
}

void PerspectivePlaneTest::normalDirection()
{
    QFETCH(bool, tilted);
    QFETCH(QPointF, point);
    const CameraScene scene = tilted ? tiltedScene() : frontScene();
    const QPointF vanishing = scene.project(scene.normal);
    const QPointF offset = vanishing - point;
    const QPointF expected = offset / QLineF(QPointF(), offset).length();
    PerspectivePlane source = scene.plane();
    QPointF direction;
    QVERIFY(projectedNormalDirection(source, point, scene.size, &direction));
    comparePoint(direction, expected);
    QVERIFY(qAbs(QLineF(QPointF(), direction).length() - 1) < 1e-12);
    // 有限消失点不依赖角点绕序。
    const Corners original = source.quad().canvasCorners();
    source.quad().setCanvasCorners({original[0], original[3], original[2], original[1]});
    QVERIFY(projectedNormalDirection(source, point, scene.size, &direction));
    comparePoint(direction, expected);
}

void PerspectivePlaneTest::normalAtInfinity()
{
    const CameraScene camera = frontScene();
    const PerspectivePlane side(
        {camera.project({120, -100, 800}), camera.project({120, 100, 800}),
         camera.project({120, 100, 1000}), camera.project({120, -100, 1000})},
        {QPointF(0, 0), QPointF(200, 0), QPointF(200, 200), QPointF(0, 200)});
    QVERIFY(side.quad().isValid());
    QPointF firstDirection;
    QVERIFY(projectedNormalDirection(side, QPointF(400, 300), camera.size, &firstDirection));
    for (const QPointF &point : {QPointF(400, 300), QPointF(-100, 900), QPointF(600, 350)}) {
        QPointF direction;
        QVERIFY(projectedNormalDirection(side, point, camera.size, &direction));
        // 齐次消失点的符号任意；验证水平单位方向及其与查询位置无关的性质。
        QVERIFY(qAbs(qAbs(direction.x()) - 1) < 1e-12);
        QVERIFY(qAbs(direction.y()) < 1e-12);
        comparePoint(direction, firstDirection);
    }
}

void PerspectivePlaneTest::normalFailureKeepsOutput()
{
    const QPointF sentinel(123, 456);
    QPointF output = sentinel;
    QVERIFY(!projectedNormalDirection(PerspectivePlane(), QPointF(20, 20), QSize(800, 600), &output));
    QCOMPARE(output, sentinel);
    const PerspectivePlane collinear(
        {QPointF(0, 0), QPointF(100, 0), QPointF(200, 0), QPointF(300, 0)}, Corners{});
    QVERIFY(!projectedNormalDirection(collinear, QPointF(20, 20), QSize(800, 600), &output));
    QCOMPARE(output, sentinel);
    const CameraScene scene = frontScene();
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal inf = std::numeric_limits<qreal>::infinity();
    for (const QPointF &point : {scene.project(scene.normal), QPointF(nan, 20),
                                QPointF(20, nan), QPointF(inf, 20), QPointF(20, -inf)}) {
        QVERIFY(!projectedNormalDirection(scene.plane(), point, scene.size, &output));
        QCOMPARE(output, sentinel);
    }
}

void PerspectivePlaneTest::extrusion_data()
{
    QTest::addColumn<bool>("tilted");
    QTest::addColumn<int>("edge");
    QTest::addColumn<qreal>("amount");
    for (bool tilted : {false, true})
        for (int edge = 0; edge < 4; ++edge)
            for (qreal amount : {-20.0, 20.0}) {
                const QByteArray name = QByteArray::number(tilted) + "-"
                    + QByteArray::number(edge) + "-" + QByteArray::number(amount);
                QTest::newRow(name.constData()) << tilted << edge << amount;
            }
}

void PerspectivePlaneTest::extrusion()
{
    QFETCH(bool, tilted);
    QFETCH(int, edge);
    QFETCH(qreal, amount);
    const CameraScene scene = tilted ? tiltedScene() : frontScene();
    const PerspectivePlane source = scene.plane();
    const int next = (edge + 1) % 4;
    const QPointF a = source.quad().canvasCorners()[edge];
    const QPointF b = source.quad().canvasCorners()[next];
    const QPointF midpoint = (a + b) / 2;
    const QPointF normalVanishing = scene.project(scene.normal);
    const QPointF offset = normalVanishing - midpoint;
    const QPointF direction = offset / QLineF(QPointF(), offset).length();
    const QPointF drag = midpoint + direction * amount;
    const PerspectivePlane result = extrudePerpendicularPlane(source, edge, drag, midpoint, scene.size);
    QVERIFY(result.quad().isValid());
    QCOMPARE(result.quad().canvasCorners()[0], a);
    QCOMPARE(result.quad().canvasCorners()[1], b);
    QCOMPARE(result.quad().surfaceCorners()[0], source.quad().surfaceCorners()[edge]);
    QCOMPARE(result.quad().surfaceCorners()[1], source.quad().surfaceCorners()[next]);
    QCOMPARE(result.surfaceGroupId(), source.surfaceGroupId());
    QCOMPARE(result.lockedEdgeMask(), quint8(1));
    QCOMPARE(result.parentPlaneIndex(), -1);
    QCOMPARE(result.parentEdgeIndex(), -1);
    QCOMPARE(result.angleToParentDegrees(), qreal(90));
    QVERIFY(!result.hasCustomAngle());
    verifyOnLine(result.quad().canvasCorners()[3], a, normalVanishing);
    verifyOnLine(result.quad().canvasCorners()[2], b, normalVanishing);
    verifyOnLine(drag, result.quad().canvasCorners()[3], result.quad().canvasCorners()[2]);
    if (tilted) {
        // 外侧边与共享边在三维中平行，投影后经过同一个已知消失点。
        const WorldPoint axis = edge % 2 == 0 ? scene.u : scene.v;
        verifyOnLine(scene.project(axis), result.quad().canvasCorners()[2], result.quad().canvasCorners()[3]);
    } else {
        // 正面矩形的边消失点在无穷远：用从主点径向缩放的解析值验证平行回退分支。
        const qreal scale = 1 - amount / QLineF(midpoint, normalVanishing).length();
        comparePoint(result.quad().canvasCorners()[3], normalVanishing + (a - normalVanishing) * scale);
        comparePoint(result.quad().canvasCorners()[2], normalVanishing + (b - normalVanishing) * scale);
    }
    const QPointF surfaceA = result.quad().surfaceCorners()[0];
    const QPointF surfaceB = result.quad().surfaceCorners()[1];
    const QPointF seam = surfaceB - surfaceA;
    const QPointF outward = result.quad().surfaceCorners()[3] - surfaceA;
    const QPointF center(scene.width / 2, scene.depth / 2);
    QVERIFY(qAbs(QPointF::dotProduct(seam, outward)) < 1e-6);
    QVERIFY(QPointF::dotProduct(outward, center - (surfaceA + surfaceB) / 2) < 0);
    comparePoint(result.quad().surfaceCorners()[2] - surfaceB, outward);
    const qreal canvasDepth = (QLineF(a, result.quad().canvasCorners()[3]).length()
        + QLineF(b, result.quad().canvasCorners()[2]).length()) / 2;
    const qreal expectedDepth = qMax(qreal(1), canvasDepth * QLineF(surfaceA, surfaceB).length() / QLineF(a, b).length());
    QVERIFY(qAbs(QLineF(QPointF(), outward).length() - expectedDepth) < 1e-6);
    const QPointF tangent(-direction.y(), direction.x());
    const PerspectivePlane tangential = extrudePerpendicularPlane(source, edge, drag + tangent * 30, midpoint, scene.size);
    for (int i = 0; i < 4; ++i) {
        comparePoint(tangential.quad().canvasCorners()[i], result.quad().canvasCorners()[i]);
        comparePoint(tangential.quad().surfaceCorners()[i], result.quad().surfaceCorners()[i]);
    }
    comparePlane(source, scene.plane());
}

void PerspectivePlaneTest::extrusionSmallDrag_data()
{
    QTest::addColumn<qreal>("amount");
    QTest::newRow("zero") << qreal(0);
    QTest::newRow("positive-below-threshold") << qreal(1.999);
    QTest::newRow("negative-below-threshold") << qreal(-1.999);
    QTest::newRow("positive-at-threshold") << qreal(2);
    QTest::newRow("negative-at-threshold") << qreal(-2);
}

void PerspectivePlaneTest::extrusionSmallDrag()
{
    QFETCH(qreal, amount);
    const CameraScene scene = frontScene();
    const PerspectivePlane source = scene.plane();
    const QPointF press(400, 204);
    const PerspectivePlane result = extrudePerpendicularPlane(source, 0, press + QPointF(0, amount), press, scene.size);
    if (qAbs(amount) < 2) {
        QCOMPARE(result.quad().canvasCorners()[3], result.quad().canvasCorners()[0]);
        QCOMPARE(result.quad().canvasCorners()[2], result.quad().canvasCorners()[1]);
        QCOMPARE(result.quad().surfaceCorners()[3], result.quad().surfaceCorners()[0]);
        QVERIFY(!result.quad().isValid());
    } else {
        // 到达 2 的构造阈值不等于达到四边形最小边长要求。
        QVERIFY(result.quad().canvasCorners()[3] != result.quad().canvasCorners()[0]);
        QVERIFY(result.quad().surfaceCorners()[3] != result.quad().surfaceCorners()[0]);
    }
    QCOMPARE(result.lockedEdgeMask(), quint8(1));
    QCOMPARE(result.surfaceGroupId(), source.surfaceGroupId());
}

void PerspectivePlaneTest::extrusionInvalidInput()
{
    const PerspectivePlane source = frontScene().plane();
    for (int edge : {-1, 4, std::numeric_limits<int>::max()})
        comparePlane(extrudePerpendicularPlane(source, edge, QPointF(400, 250), QPointF(400, 204), QSize(800, 600)), PerspectivePlane());
    for (const QSize &size : {QSize(), QSize(0, 600), QSize(800, 0), QSize(-1, 600)})
        comparePlane(extrudePerpendicularPlane(source, 0, QPointF(400, 250), QPointF(400, 204), size), PerspectivePlane());
    const PerspectivePlane failed = extrudePerpendicularPlane(PerspectivePlane(), 0, QPointF(20, 20), QPointF(), QSize(800, 600));
    QVERIFY(!failed.quad().isValid());
}

void PerspectivePlaneTest::rotation_data()
{
    QTest::addColumn<bool>("tilted");
    QTest::addColumn<int>("edge");
    QTest::addColumn<qreal>("target");
    QTest::addColumn<qreal>("normalized");
    const QList<QPair<qreal, qreal>> angles{{90, 90}, {120, 120}, {60, 60}, {270, 270},
                                          {450, 90}, {-270, 90}, {0, 0}, {360, 360},
                                          {720, 360}, {-360, 0}};
    for (bool tilted : {false, true})
        for (int edge = 0; edge < 4; ++edge)
            for (const auto &angle : angles) {
                const QByteArray name = QByteArray::number(tilted) + "-"
                    + QByteArray::number(edge) + "-" + QByteArray::number(angle.first);
                QTest::newRow(name.constData()) << tilted << edge << angle.first << angle.second;
            }
}

void PerspectivePlaneTest::rotation()
{
    QFETCH(bool, tilted);
    QFETCH(int, edge);
    QFETCH(qreal, target);
    QFETCH(qreal, normalized);
    const CameraScene scene = tilted ? tiltedScene() : frontScene();
    const PerspectivePlane source = scene.plane();
    const PerspectivePlane result = rotatePlaneAroundEdge(source, edge, target, scene.size);
    QVERIFY(result.quad().isValid());
    QCOMPARE(result.quad().canvasCorners()[edge], source.quad().canvasCorners()[edge]);
    QCOMPARE(result.quad().canvasCorners()[(edge + 1) % 4], source.quad().canvasCorners()[(edge + 1) % 4]);
    comparePoint(result.quad().canvasCorners()[(edge + 3) % 4], scene.rotatedOuter(edge, false, target - 90));
    comparePoint(result.quad().canvasCorners()[(edge + 2) % 4], scene.rotatedOuter(edge, true, target - 90));
    QCOMPARE(result.quad().surfaceCorners(), source.quad().surfaceCorners());
    compareRelations(result, source);
    QCOMPARE(result.angleToParentDegrees(), normalized);
    QVERIFY(result.hasCustomAngle());
    comparePlane(source, scene.plane());
    // 再次设置同一目标角度应保持几何不变，不能再次施加旋转增量。
    const PerspectivePlane repeated = rotatePlaneAroundEdge(result, edge, target, scene.size);
    for (int i = 0; i < 4; ++i)
        comparePoint(repeated.quad().canvasCorners()[i], result.quad().canvasCorners()[i]);
    QCOMPARE(repeated.angleToParentDegrees(), normalized);
    QVERIFY(repeated.hasCustomAngle());
}

void PerspectivePlaneTest::rotationInvalidInput_data()
{
    QTest::addColumn<int>("edge");
    QTest::addColumn<qreal>("angle");
    QTest::addColumn<QSize>("size");
    const QSize size(800, 600);
    QTest::newRow("negative-edge") << -1 << qreal(120) << size;
    QTest::newRow("too-large-edge") << 4 << qreal(120) << size;
    QTest::newRow("maximum-edge") << std::numeric_limits<int>::max() << qreal(120) << size;
    QTest::newRow("nan-angle") << 0 << std::numeric_limits<qreal>::quiet_NaN() << size;
    QTest::newRow("positive-infinite-angle") << 0 << std::numeric_limits<qreal>::infinity() << size;
    QTest::newRow("negative-infinite-angle") << 0 << -std::numeric_limits<qreal>::infinity() << size;
    QTest::newRow("empty-background") << 0 << qreal(120) << QSize();
    QTest::newRow("zero-width") << 0 << qreal(120) << QSize(0, 600);
    QTest::newRow("zero-height") << 0 << qreal(120) << QSize(800, 0);
    QTest::newRow("negative-width") << 0 << qreal(120) << QSize(-1, 600);
}

void PerspectivePlaneTest::rotationInvalidInput()
{
    QFETCH(int, edge);
    QFETCH(qreal, angle);
    QFETCH(QSize, size);
    const PerspectivePlane source = frontScene().plane();
    comparePlane(rotatePlaneAroundEdge(source, edge, angle, size), source);
}

void PerspectivePlaneTest::rotationRejectsDegenerateAndBehindCamera()
{
    const QSize size(800, 600);
    const PerspectivePlane empty;
    comparePlane(rotatePlaneAroundEdge(empty, 0, 120, size), empty);
    const PerspectivePlane collinear = withMetadata(PerspectivePlane(
        {QPointF(0, 0), QPointF(100, 0), QPointF(200, 0), QPointF(300, 0)}, Corners{}));
    comparePlane(rotatePlaneAroundEdge(collinear, 0, 120, size), collinear);

    // 共享边穿过主点所在水平线，转动 90° 后平面穿过相机，投影塌缩成直线。
    CameraScene collapsedScene = frontScene();
    collapsedScene.corners = {{{-120, 0, 800}, {120, 0, 800}, {120, 160, 800}, {-120, 160, 800}}};
    const PerspectivePlane collapsed = collapsedScene.plane();
    QVERIFY(collapsed.quad().isValid());
    comparePlane(rotatePlaneAroundEdge(collapsed, 0, 180, size), collapsed);

    // 宽度大于相机距离，绕右边转动 90° 后两个对侧角点落到相机后方。
    CameraScene behindScene = frontScene();
    behindScene.width = 2000;
    behindScene.corners = {{{-1000, -80, 800}, {1000, -80, 800}, {1000, 80, 800}, {-1000, 80, 800}}};
    const PerspectivePlane behind = behindScene.plane();
    QVERIFY(behind.quad().isValid());
    comparePlane(rotatePlaneAroundEdge(behind, 1, 180, size), behind);
}

QTEST_APPLESS_MAIN(PerspectivePlaneTest)
#include "tst_perspectiveplane.moc"
