#include <QtTest>

#include "core/floatingimageprojection.h"
#include "core/floatingimagetransformtool.h"

namespace {

PerspectiveFacet leftFacet()
{
    return {
        {QPointF(10, 10), QPointF(110, 10), QPointF(110, 80), QPointF(10, 80)},
        {QPointF(0, 0), QPointF(100, 0), QPointF(100, 70), QPointF(0, 70)}
    };
}

PerspectiveFacet rightFacet()
{
    return {
        {QPointF(110, 10), QPointF(230, 30), QPointF(230, 96), QPointF(110, 80)},
        {QPointF(100, 0), QPointF(200, 0), QPointF(200, 70), QPointF(100, 70)}
    };
}

FloatingImage imageAcrossFacetSeam()
{
    FloatingImage image;
    image.bitmap = QImage(200, 70, QImage::Format_ARGB32);
    image.bitmap.fill(Qt::red);
    image.placementOrigin = QPointF(0, 0);
    image.surfaceAttached = true;
    image.hostFacetIndex = 0;
    image.surfaceFacets = QVector<PerspectiveFacet>{leftFacet(), rightFacet()};
    return image;
}

QPointF cornerAverage(const PerspectiveFacet &facet)
{
    QPointF sum;
    for (const QPointF &corner : facet.canvasPolygon())
        sum += corner / 4.0;
    return sum;
}

qreal centerDeviation(const PerspectiveFacet &facet)
{
    FloatingImage image;
    image.bitmap = QImage(100, 70, QImage::Format_ARGB32);
    image.bitmap.fill(Qt::red);
    image.placementOrigin = facet.surfaceCorners()[0];
    image.surfaceAttached = true;
    image.hostFacetIndex = 0;
    image.surfaceFacets = QVector<PerspectiveFacet>{facet};

    const auto projection = FloatingImageProjection::forImage(image);
    if (projection->patches().size() != 1)
        return -1;
    return QLineF(projection->patches().first().bitmapToCanvas.forward().map(QPointF(50, 35)),
                  cornerAverage(facet)).length();
}

} // namespace

class FloatingImageProjectionTest : public QObject
{
    Q_OBJECT

private slots:
    void detachedImageProducesSinglePatch();
    void attachedImageSpansEveryFacet();
    void hitTestCoversEveryFacet();
    void perspectiveFacetUsesProjectiveMapping();
    void affineFacetUsesLinearMapping();
    void attachedCoordinatesRoundTrip();
    void transformToolScalesFromCorner();
    void transformToolRotatesAroundCenter();
};

void FloatingImageProjectionTest::detachedImageProducesSinglePatch()
{
    FloatingImage image;
    image.bitmap = QImage(40, 30, QImage::Format_ARGB32);
    image.bitmap.fill(Qt::red);
    image.placementOrigin = QPointF(100, 50);

    const auto projection = FloatingImageProjection::forImage(image);
    QCOMPARE(projection->patches().size(), 1);
    QCOMPARE(projection->canvasOutline().boundingRect(), QRectF(100, 50, 40, 30));
}

void FloatingImageProjectionTest::attachedImageSpansEveryFacet()
{
    QCOMPARE(FloatingImageProjection::forImage(imageAcrossFacetSeam())->patches().size(), 2);
}

void FloatingImageProjectionTest::hitTestCoversEveryFacet()
{
    const auto projection = FloatingImageProjection::forImage(imageAcrossFacetSeam());
    QVERIFY(projection->hitTest(QPointF(60, 45)));
    QVERIFY(projection->hitTest(QPointF(170, 55)));
    QVERIFY(!projection->hitTest(QPointF(400, 400)));
}

void FloatingImageProjectionTest::perspectiveFacetUsesProjectiveMapping()
{
    QVERIFY(centerDeviation(rightFacet()) > 0.5);
}

void FloatingImageProjectionTest::affineFacetUsesLinearMapping()
{
    QVERIFY(centerDeviation(leftFacet()) < 0.5);
}

void FloatingImageProjectionTest::attachedCoordinatesRoundTrip()
{
    const FloatingImage image = imageAcrossFacetSeam();
    const QPointF placementPoint(150, 35);
    const QPointF canvasPoint = image.mapPlacementToCanvas(placementPoint);
    QPointF mappedBack;
    QVERIFY(image.mapCanvasToPlacement(canvasPoint, &mappedBack));
    QVERIFY(QLineF(placementPoint, mappedBack).length() < 1e-6);
}

void FloatingImageProjectionTest::transformToolScalesFromCorner()
{
    FloatingImage image;
    image.bitmap = QImage(100, 50, QImage::Format_ARGB32);
    image.placementOrigin = QPointF(10, 20);

    FloatingImageTransformTool tool;
    QVERIFY(tool.beginTransform(image, QPointF(110, 70), 2,
                                FloatingImageTransformTool::Mode::Scale));
    FloatingImage result;
    QVERIFY(tool.update(QPointF(210, 120), false, false, &result));
    QCOMPARE(result.placementOrigin, QPointF(10, 20));
    QCOMPARE(result.scaleFactors, QPointF(2, 2));
}

void FloatingImageProjectionTest::transformToolRotatesAroundCenter()
{
    FloatingImage image;
    image.bitmap = QImage(100, 50, QImage::Format_ARGB32);
    image.placementOrigin = QPointF(10, 20);

    FloatingImageTransformTool tool;
    QVERIFY(tool.beginTransform(image, QPointF(10, 20), 0,
                                FloatingImageTransformTool::Mode::Rotate));
    FloatingImage result;
    QVERIFY(tool.update(QPointF(85, -5), false, false, &result));
    QVERIFY(qAbs(result.rotationDegrees - 90.0) < 1e-6);
}

QTEST_APPLESS_MAIN(FloatingImageProjectionTest)
#include "tst_floatingimageprojection.moc"
