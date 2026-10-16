#include <algorithm>
#include <limits>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QtTest>

#include "canvas/auxiliaryoverlayitem.h"
#include "canvas/vpcontroller.h"
#include "core/scenerenderer.h"

// Use the general surface mapping API for normalized coordinates.
#include "core/perspectivequad.h"
static PerspectiveTransform normalizedMapping(const PerspectiveQuad &quad)
{
    return PerspectiveQuad(quad.canvasCorners(),
        {QPointF(0,0), QPointF(1,0), QPointF(1,1), QPointF(0,1)}).surfaceToCanvasTransform();
}

using Corners = PerspectiveQuad::Corners;
Q_DECLARE_METATYPE(Corners)

namespace {

Corners canvasRectangle()
{
    return {QPointF(10, 10), QPointF(110, 10), QPointF(110, 80), QPointF(10, 80)};
}

Corners surfaceRectangle()
{
    return {QPointF(0, 0), QPointF(100, 0), QPointF(100, 70), QPointF(0, 70)};
}

Corners trapezoid()
{
    return {QPointF(100, 0), QPointF(300, 0), QPointF(360, 200), QPointF(40, 200)};
}

void comparePoints(const QPointF &actual, const QPointF &expected)
{
    const QString message = QStringLiteral("actual (%1, %2), expected (%3, %4)")
                                .arg(actual.x()).arg(actual.y())
                                .arg(expected.x()).arg(expected.y());
    QVERIFY2(qAbs(actual.x() - expected.x()) <= 1e-6
                 && qAbs(actual.y() - expected.y()) <= 1e-6,
             qPrintable(message));
}

} // namespace

class PerspectiveQuadTest : public QObject
{
    Q_OBJECT

private slots:
    void diagnosticStatusAndGuideRendering();
    void defaultState();
    void storesIndependentCoordinates();
    void settersAndCopies();
    void polygonsAndControlPoints();
    void validity_data();
    void validity();
    void rejectsNonFiniteCorners_data();
    void rejectsNonFiniteCorners();
    void validityDoesNotCheckSurface();
    void containsCanvasPoint_data();
    void containsCanvasPoint();
    void hitsControlPoints_data();
    void hitsControlPoints();
    void invalidHitInput_data();
    void invalidHitInput();
    void affineTransforms();
    void perspectiveTransforms_data();
    void perspectiveTransforms();
    void transformsAreSnapshots();
    void degenerateTransforms();
};

void PerspectiveQuadTest::defaultState()
{
    const PerspectiveQuad quad;
    QCOMPARE(quad.canvasCorners(), Corners{});
    QCOMPARE(quad.surfaceCorners(), Corners{});
    QCOMPARE(quad.controlPoints(), QVector<QPointF>(8, QPointF()));
    QVERIFY(!quad.isValid());
    QVERIFY(!quad.containsCanvasPoint(QPointF(1, 1)));
    QVERIFY(!quad.surfaceToCanvasTransform().isValid());
    QVERIFY(!normalizedMapping(quad).isValid());
}

void PerspectiveQuadTest::storesIndependentCoordinates()
{
    Corners canvas = canvasRectangle();
    Corners surface = surfaceRectangle();
    const PerspectiveQuad quad(canvas, surface);
    canvas[0] = QPointF(-500, -500);
    surface[3] = QPointF(800, 800);
    QCOMPARE(quad.canvasCorners(), canvasRectangle());
    QCOMPARE(quad.surfaceCorners(), surfaceRectangle());
}

void PerspectiveQuadTest::settersAndCopies()
{
    PerspectiveQuad quad;
    quad.setCanvasCorners(canvasRectangle());
    quad.setSurfaceCorners(surfaceRectangle());
    for (int i = 0; i < PerspectiveQuad::CornerCount; ++i) {
        const Corners previousCanvas = quad.canvasCorners();
        const Corners previousSurface = quad.surfaceCorners();
        const QPointF canvasPoint(200 + i, 300 + i);
        const QPointF surfacePoint(-200 - i, -300 - i);
        quad.setCanvasCorner(i, canvasPoint);
        QCOMPARE(quad.surfaceCorners(), previousSurface);
        quad.setSurfaceCorner(i, surfacePoint);
        for (int j = 0; j < PerspectiveQuad::CornerCount; ++j) {
            QCOMPARE(quad.canvasCorners()[j], j == i ? canvasPoint : previousCanvas[j]);
            QCOMPARE(quad.surfaceCorners()[j], j == i ? surfacePoint : previousSurface[j]);
        }
    }
    PerspectiveQuad copy = quad;
    PerspectiveQuad assigned;
    assigned = quad;
    quad.setCanvasCorners(canvasRectangle());
    quad.setSurfaceCorners(surfaceRectangle());
    QCOMPARE(copy.canvasCorners(), assigned.canvasCorners());
    QCOMPARE(copy.surfaceCorners(), assigned.surfaceCorners());
    QVERIFY(copy.canvasCorners() != quad.canvasCorners());
    QVERIFY(copy.surfaceCorners() != quad.surfaceCorners());
}

void PerspectiveQuadTest::polygonsAndControlPoints()
{
    PerspectiveQuad quad(trapezoid(), surfaceRectangle());
    QCOMPARE(quad.canvasPolygon(), QPolygonF({QPointF(100, 0), QPointF(300, 0),
                                             QPointF(360, 200), QPointF(40, 200)}));
    QCOMPARE(quad.surfacePolygon(), QPolygonF({QPointF(0, 0), QPointF(100, 0),
                                              QPointF(100, 70), QPointF(0, 70)}));
    const QVector<QPointF> expected{QPointF(100, 0), QPointF(300, 0),
                                  QPointF(360, 200), QPointF(40, 200),
                                  QPointF(200, 0), QPointF(330, 100),
                                  QPointF(200, 200), QPointF(70, 100)};
    QCOMPARE(quad.controlPoints(), expected);
    quad.setSurfaceCorner(0, QPointF(-1000, 900));
    QCOMPARE(quad.controlPoints(), expected); // 控制点仅依赖画布坐标。
    quad.setCanvasCorner(0, QPointF(80, -20));
    QCOMPARE(quad.controlPoints()[4], QPointF(190, -10));
    QCOMPARE(quad.controlPoints()[7], QPointF(60, 90));
}

void PerspectiveQuadTest::validity_data()
{
    QTest::addColumn<Corners>("canvas");
    QTest::addColumn<bool>("valid");
    QTest::newRow("rectangle") << canvasRectangle() << true;
    QTest::newRow("perspective") << trapezoid() << true;
    Corners reversed = trapezoid();
    std::reverse(reversed.begin(), reversed.end());
    QTest::newRow("reversed-winding") << reversed << true;
    Corners translated = trapezoid();
    for (QPointF &point : translated)
        point += QPointF(-1000, -2000);
    QTest::newRow("negative-coordinates") << translated << true;
    QTest::newRow("collapsed") << Corners{} << false;
    QTest::newRow("repeated-corner")
        << Corners{QPointF(0, 0), QPointF(100, 0), QPointF(100, 0), QPointF(0, 100)} << false;
    QTest::newRow("collinear")
        << Corners{QPointF(0, 0), QPointF(100, 0), QPointF(200, 0), QPointF(300, 0)} << false;
    QTest::newRow("concave")
        << Corners{QPointF(0, 0), QPointF(100, 0), QPointF(30, 30), QPointF(0, 100)} << false;
    QTest::newRow("self-intersecting")
        << Corners{QPointF(0, 0), QPointF(100, 100), QPointF(0, 100), QPointF(100, 0)} << false;
    QTest::newRow("edge-below-minimum")
        << Corners{QPointF(0, 0), QPointF(7.999, 0), QPointF(7.999, 100), QPointF(0, 100)} << false;
    QTest::newRow("edge-at-minimum")
        << Corners{QPointF(0, 0), QPointF(8, 0), QPointF(8, 100), QPointF(0, 100)} << true;
    // 平行四边形的边均大于 8，叉乘大于 4，单独验证实际面积 50 的门槛。
    QTest::newRow("area-below-minimum")
        << Corners{QPointF(0, 0), QPointF(10, 0), QPointF(20, 4.999), QPointF(10, 4.999)} << false;
    QTest::newRow("area-at-minimum")
        << Corners{QPointF(0, 0), QPointF(10, 0), QPointF(20, 5), QPointF(10, 5)} << true;
    QTest::newRow("cross-below-minimum")
        << Corners{QPointF(0, 0), QPointF(100, 0), QPointF(200, 0.03999), QPointF(0, 100)} << false;
    QTest::newRow("cross-at-minimum")
        << Corners{QPointF(0, 0), QPointF(100, 0), QPointF(200, 0.04), QPointF(0, 100)} << true;
    // 凸性、边长和面积都合格，但强透视令底角的齐次分母过小。
    QTest::newRow("denominator-too-small")
        << Corners{QPointF(0, 0), QPointF(8, 0), QPointF(1e6, 100), QPointF(0, 100)} << false;
    QTest::newRow("denominator-safe")
        << Corners{QPointF(0, 0), QPointF(20, 0), QPointF(1e6, 100), QPointF(0, 100)} << true;
}

void PerspectiveQuadTest::validity()
{
    QFETCH(Corners, canvas);
    QFETCH(bool, valid);
    QCOMPARE(PerspectiveQuad(canvas, surfaceRectangle()).isValid(), valid);
}

void PerspectiveQuadTest::rejectsNonFiniteCorners_data()
{
    QTest::addColumn<int>("corner");
    QTest::addColumn<int>("axis");
    QTest::addColumn<qreal>("value");
    const qreal values[]{std::numeric_limits<qreal>::quiet_NaN(),
                         std::numeric_limits<qreal>::infinity(),
                         -std::numeric_limits<qreal>::infinity()};
    for (int corner = 0; corner < 4; ++corner)
        for (int axis = 0; axis < 2; ++axis)
            for (int value = 0; value < 3; ++value) {
                const QByteArray name = QByteArray::number(corner) + "-"
                    + QByteArray::number(axis) + "-" + QByteArray::number(value);
                QTest::newRow(name.constData()) << corner << axis << values[value];
            }
}

void PerspectiveQuadTest::rejectsNonFiniteCorners()
{
    QFETCH(int, corner);
    QFETCH(int, axis);
    QFETCH(qreal, value);
    Corners canvas = canvasRectangle();
    if (axis == 0)
        canvas[corner].setX(value);
    else
        canvas[corner].setY(value);
    QVERIFY(!PerspectiveQuad(canvas, surfaceRectangle()).isValid());
}

void PerspectiveQuadTest::validityDoesNotCheckSurface()
{
    PerspectiveQuad quad(canvasRectangle(), Corners{});
    QVERIFY(quad.isValid());
    QVERIFY(!quad.surfaceToCanvasTransform().isValid());
    QVERIFY(normalizedMapping(quad).isValid());
    quad.setSurfaceCorner(2, QPointF(std::numeric_limits<qreal>::quiet_NaN(), 0));
    QVERIFY(quad.isValid());
}

void PerspectiveQuadTest::containsCanvasPoint_data()
{
    QTest::addColumn<QPointF>("point");
    QTest::addColumn<bool>("inside");
    QTest::newRow("center") << QPointF(200, 100) << true;
    QTest::newRow("near-left-inside") << QPointF(70.01, 100) << true;
    QTest::newRow("near-left-outside") << QPointF(69.99, 100) << false;
    QTest::newRow("bounding-box-only") << QPointF(50, 10) << false;
    QTest::newRow("above") << QPointF(200, -0.01) << false;
    QTest::newRow("below") << QPointF(200, 200.01) << false;
    QTest::newRow("surface-only") << QPointF(10, 10) << false;
}

void PerspectiveQuadTest::containsCanvasPoint()
{
    QFETCH(QPointF, point);
    QFETCH(bool, inside);
    PerspectiveQuad quad(trapezoid(), surfaceRectangle());
    QCOMPARE(quad.containsCanvasPoint(point), inside);
    Corners reversed = trapezoid();
    std::reverse(reversed.begin(), reversed.end());
    quad.setCanvasCorners(reversed);
    QCOMPARE(quad.containsCanvasPoint(point), inside);
}

void PerspectiveQuadTest::hitsControlPoints_data()
{
    QTest::addColumn<int>("index");
    for (int i = 0; i < 8; ++i)
        QTest::newRow(qPrintable(QString::number(i))) << i;
}

void PerspectiveQuadTest::hitsControlPoints()
{
    QFETCH(int, index);
    const PerspectiveQuad quad(canvasRectangle(), surfaceRectangle());
    const QVector<QPointF> expected{QPointF(10, 10), QPointF(110, 10),
                                  QPointF(110, 80), QPointF(10, 80),
                                  QPointF(60, 10), QPointF(110, 45),
                                  QPointF(60, 80), QPointF(10, 45)};
    const QPointF point = expected[index];
    QCOMPARE(quad.controlPointIndexAt(point, 0), index);
    QCOMPARE(quad.controlPointIndexAt(point + QPointF(3, 4), 5), index);
    QCOMPARE(quad.controlPointIndexAt(point + QPointF(3, 4), 4.999), -1);
}

void PerspectiveQuadTest::invalidHitInput_data()
{
    QTest::addColumn<QPointF>("point");
    QTest::addColumn<qreal>("tolerance");
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    QTest::newRow("negative-tolerance") << QPointF(10, 10) << qreal(-0.001);
    QTest::newRow("nan-tolerance") << QPointF(10, 10) << nan;
    QTest::newRow("positive-infinite-tolerance") << QPointF(10, 10) << infinity;
    QTest::newRow("negative-infinite-tolerance") << QPointF(10, 10) << -infinity;
    QTest::newRow("nan-x") << QPointF(nan, 10) << qreal(5);
    QTest::newRow("nan-y") << QPointF(10, nan) << qreal(5);
    QTest::newRow("positive-infinite-x") << QPointF(infinity, 10) << qreal(5);
    QTest::newRow("negative-infinite-x") << QPointF(-infinity, 10) << qreal(5);
    QTest::newRow("positive-infinite-y") << QPointF(10, infinity) << qreal(5);
    QTest::newRow("negative-infinite-y") << QPointF(10, -infinity) << qreal(5);
}

void PerspectiveQuadTest::invalidHitInput()
{
    QFETCH(QPointF, point);
    QFETCH(qreal, tolerance);
    const PerspectiveQuad quad(canvasRectangle(), surfaceRectangle());
    QCOMPARE(quad.controlPointIndexAt(point, tolerance), -1);
}

void PerspectiveQuadTest::affineTransforms()
{
    const PerspectiveQuad quad(canvasRectangle(), surfaceRectangle());
    const PerspectiveTransform surface = quad.surfaceToCanvasTransform();
    const PerspectiveTransform uv = normalizedMapping(quad);
    QVERIFY(surface.isValid());
    QVERIFY(uv.isValid());
    const Corners unit{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)};
    for (int i = 0; i < 4; ++i) {
        QPointF result;
        QVERIFY(surface.mapForward(surfaceRectangle()[i], &result));
        comparePoints(result, canvasRectangle()[i]);
        QVERIFY(uv.mapForward(unit[i], &result));
        comparePoints(result, canvasRectangle()[i]);
    }
    QPointF result;
    QVERIFY(surface.mapForward(QPointF(50, 35), &result));
    comparePoints(result, QPointF(60, 45));
    QVERIFY(uv.mapForward(QPointF(0.5, 0.5), &result));
    comparePoints(result, QPointF(60, 45));
    QVERIFY(surface.mapInverse(QPointF(60, 45), &result));
    comparePoints(result, QPointF(50, 35));
    QVERIFY(uv.mapInverse(QPointF(60, 45), &result));
    comparePoints(result, QPointF(0.5, 0.5));
}

void PerspectiveQuadTest::perspectiveTransforms_data()
{
    QTest::addColumn<QPointF>("uvPoint");
    QTest::addColumn<QPointF>("expected");
    QTest::newRow("corner-0") << QPointF(0, 0) << QPointF(100, 0);
    QTest::newRow("corner-1") << QPointF(1, 0) << QPointF(300, 0);
    QTest::newRow("corner-2") << QPointF(1, 1) << QPointF(360, 200);
    QTest::newRow("corner-3") << QPointF(0, 1) << QPointF(40, 200);
    QTest::newRow("center") << QPointF(0.5, 0.5) << QPointF(200, 1000.0 / 13);
    QTest::newRow("interior") << QPointF(0.25, 0.4) << QPointF(2400.0 / 17, 1000.0 / 17);
    QTest::newRow("outside-left") << QPointF(-0.5, 0) << QPointF(0, 0);
    QTest::newRow("outside-bottom") << QPointF(0.5, 1.5) << QPointF(200, 3000.0 / 7);
}

void PerspectiveQuadTest::perspectiveTransforms()
{
    QFETCH(QPointF, uvPoint);
    QFETCH(QPointF, expected);
    // 展开矩形带平移且宽高不同，确保 surface 映射使用实际坐标而非单位 UV。
    const PerspectiveQuad quad(trapezoid(),
        {QPointF(-20, 30), QPointF(180, 30), QPointF(180, 110), QPointF(-20, 110)});
    const QPointF surfacePoint(-20 + 200 * uvPoint.x(), 30 + 80 * uvPoint.y());
    const PerspectiveTransform uv = normalizedMapping(quad);
    const PerspectiveTransform surface = quad.surfaceToCanvasTransform();
    QVERIFY(uv.isValid());
    QVERIFY(surface.isValid());
    QVERIFY(!uv.forward().isAffine());
    QPointF result;
    QVERIFY(uv.mapForward(uvPoint, &result));
    comparePoints(result, expected);
    QVERIFY(surface.mapForward(surfacePoint, &result));
    comparePoints(result, expected);
    QVERIFY(uv.mapInverse(expected, &result));
    comparePoints(result, uvPoint);
    QVERIFY(surface.mapInverse(expected, &result));
    comparePoints(result, surfacePoint);
}

void PerspectiveQuadTest::transformsAreSnapshots()
{
    PerspectiveQuad quad(canvasRectangle(), surfaceRectangle());
    const PerspectiveTransform original = quad.surfaceToCanvasTransform();
    quad.setCanvasCorners({QPointF(20, 30), QPointF(220, 30), QPointF(220, 170), QPointF(20, 170)});
    const PerspectiveTransform changed = quad.surfaceToCanvasTransform();
    QPointF result;
    QVERIFY(original.mapForward(QPointF(50, 35), &result));
    comparePoints(result, QPointF(60, 45));
    QVERIFY(changed.mapForward(QPointF(50, 35), &result));
    comparePoints(result, QPointF(120, 100));
    quad.setSurfaceCorners({QPointF(0, 0), QPointF(200, 0), QPointF(200, 140), QPointF(0, 140)});
    QVERIFY(quad.surfaceToCanvasTransform().mapForward(QPointF(50, 35), &result));
    comparePoints(result, QPointF(70, 65));
    QVERIFY(normalizedMapping(quad).mapForward(QPointF(0.5, 0.5), &result));
    comparePoints(result, QPointF(120, 100));
}

void PerspectiveQuadTest::degenerateTransforms()
{
    const PerspectiveQuad collapsedCanvas(Corners{}, surfaceRectangle());
    const PerspectiveQuad collapsedSurface(canvasRectangle(), Corners{});
    QVERIFY(!collapsedCanvas.surfaceToCanvasTransform().isValid());
    QVERIFY(!normalizedMapping(collapsedCanvas).isValid());
    QVERIFY(!collapsedSurface.surfaceToCanvasTransform().isValid());
    QVERIFY(normalizedMapping(collapsedSurface).isValid());
    QPointF result(123, 456);
    QVERIFY(!normalizedMapping(collapsedCanvas).mapForward(QPointF(0.5, 0.5), &result));
    QCOMPARE(result, QPointF(123, 456));
    QVERIFY(!collapsedSurface.surfaceToCanvasTransform().mapInverse(QPointF(60, 45), &result));
    QCOMPARE(result, QPointF(123, 456));
}

void PerspectiveQuadTest::diagnosticStatusAndGuideRendering()
{
    QQuickWindow window;
    window.resize(300, 300);
    window.show();
    QCoreApplication::processEvents();
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    using Status = PerspectiveQuad::Status;
    const QSize size(300, 300);
    const PerspectiveQuad::Corners surface{{QPointF(0, 0), QPointF(200, 0), QPointF(200, 150), QPointF(0, 150)}};
    const PerspectiveQuad rectangle({QPointF(50, 50), QPointF(250, 50), QPointF(250, 250), QPointF(50, 250)}, surface);
    const PerspectiveQuad yellow({QPointF(50, 50), QPointF(200, 50), QPointF(230, 200), QPointF(80, 200)}, surface);
    const PerspectiveQuad degenerate({QPointF(50, 50), QPointF(250, 50), QPointF(100, 100), QPointF(50, 250)}, surface);
    const auto fromHomography = [&](const QTransform &h) {
        PerspectiveQuad::Corners corners;
        const PerspectiveQuad::Corners uv{{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}};
        for (int i = 0; i < 4; ++i) corners[i] = h.map(uv[i]) * 300 + QPointF(150, 150);
        return PerspectiveQuad(corners, surface);
    };
    const auto blue = fromHomography(QTransform(.4, .05, .1, -.12, .4, .2, -.3, -.3, 1));
    const auto red = fromHomography(QTransform(.4, .05, .1, -.05, .5, .2, -.3, -.3, 1));
    check(rectangle.status(size) == Status::Valid, "parallel rectangle is blue");
    check(blue.status(size) == Status::Valid, "recoverable orthogonal perspective is blue");
    check(yellow.status(size) == Status::UnresolvedVanishingPoints, "unresolved orthogonality is yellow");
    check(red.status(size) == Status::InvalidAspectRatio && red.isProjectable(), "impossible metric aspect is red but safe 2D projection remains");
    check(degenerate.status(size) == Status::InvalidAspectRatio && !degenerate.isProjectable(), "degenerate geometry is red");
    for (const auto &quad : {rectangle, blue, yellow, red}) {
        auto scaled = quad;
        for (int i = 0; i < 4; ++i) scaled.setCanvasCorner(i, quad.canvasCorners()[i] * 2);
        check(scaled.status(QSize(600, 600)) == quad.status(size), "status invariant under image scale");
        check(quad.guideColor(false, size) == quad.guideColor(true, size), "inactive plane preserves status color");
        check(quad.guideColor(true, size).alpha() == 255, "active plane border is opaque");
    }
    auto invalid = rectangle;
    invalid.setCanvasCorner(0, QPointF(std::numeric_limits<qreal>::infinity(), 1));
    check(!invalid.isProjectable() && invalid.status(size) == Status::InvalidAspectRatio, "nonfinite coordinates rejected");

    QImage background(size, QImage::Format_ARGB32_Premultiplied); background.fill(Qt::white);
    for (const auto &quad : {blue, yellow, red}) {
        VpDocument doc; doc.setImage(background);
        PerspectivePlane plane(quad.canvasCorners(), quad.surfaceCorners());
        check(doc.appendPlane(plane) == 0, "warning plane retained for editing");
        doc.setSelectedPlane(0);
        QImage guides(size, QImage::Format_ARGB32_Premultiplied); guides.fill(Qt::transparent);
        QPainter painter(&guides);
        SceneRenderer renderer(doc); SceneRenderer::Guides options;
        options.editHandlesVisible = true;
        renderer.renderGuides(painter, 1, options); painter.end();
        const QColor expected = quad.guideColor(true, size);
        int matches = 0;
        for (int y = 0; y < guides.height(); ++y)
            for (int x = 0; x < guides.width(); ++x) {
                const QColor actual = guides.pixelColor(x, y);
                if (actual.alpha() && qAbs(actual.red() - expected.red()) < 3
                    && qAbs(actual.green() - expected.green()) < 3 && qAbs(actual.blue() - expected.blue()) < 3) ++matches;
            }
        check(matches > 30, "raster guides use diagnostic color");
        AuxiliaryOverlayItem overlay(window.contentItem()); overlay.setSize(size);
        overlay.setPlaneGuides({plane}, 0, true, false, nullptr, 50, 1, {}, size);
        AuxiliaryRasterItem *raster = nullptr;
        for (QQuickItem *child : overlay.childItems())
            if (auto *item = dynamic_cast<AuxiliaryRasterItem *>(child)) raster = item;
        check(raster && !raster->pictures.isEmpty(), "overlay builds software guide commands");
        if (raster && !raster->pictures.isEmpty()) {
            QImage overlayImage(size, QImage::Format_ARGB32_Premultiplied); overlayImage.fill(Qt::transparent);
            QPainter overlayPainter(&overlayImage); raster->paint(&overlayPainter); overlayPainter.end();
            int overlayMatches = 0;
            for (int y = 0; y < overlayImage.height(); ++y)
                for (int x = 0; x < overlayImage.width(); ++x) {
                    const QColor actual = overlayImage.pixelColor(x, y);
                    if (actual.alpha() > 120 && qAbs(actual.red() - expected.red()) < 3
                        && qAbs(actual.green() - expected.green()) < 3 && qAbs(actual.blue() - expected.blue()) < 3) ++overlayMatches;
                }
            check(overlayMatches > 30, "cached overlay uses diagnostic color");
        }
        const auto builds = overlay.planeGeometryBuildCount();
        overlay.setPlaneGuides({plane}, 0, true, false, nullptr, 50, 1, {}, size);
        check(overlay.planeGeometryBuildCount() == builds, "unchanged status retains cached geometry");
        overlay.setPlaneGuides({plane}, 0, true, false, nullptr, 50, 1, {}, size, 2);
        if (raster) {
            QImage dragged(size, QImage::Format_ARGB32_Premultiplied); dragged.fill(Qt::transparent);
            QPainter draggedPainter(&dragged); raster->paint(&draggedPainter); draggedPainter.end();
            const QColor center = dragged.pixelColor(quad.canvasCorners()[2].toPoint());
            check(center.alpha() == 255 && qAbs(center.red() - expected.red()) < 3
                  && qAbs(center.green() - expected.green()) < 3 && qAbs(center.blue() - expected.blue()) < 3,
                  "dragged handle becomes solid status color without geometry change");
        }
        overlay.setPlaneGuides({plane}, 0, true, false, nullptr, 50, 1, {}, size);
        check(overlay.planeGeometryBuildCount() == builds + 2, "release restores normal handle without geometry change");
    }

}

QTEST_MAIN(PerspectiveQuadTest)
#include "tst_perspectivequad.moc"
