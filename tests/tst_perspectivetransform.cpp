#include <QtTest>
#include <limits>

#include "core/perspectivetransform.h"

// 展开图 100×70 的规整矩形 -> 画布上同尺寸、平移 (10, 10) 的矩形（仿射，结果可手算）
static QPolygonF rectSurface()
{
    return QPolygonF{QPointF(0, 0), QPointF(100, 0), QPointF(100, 70), QPointF(0, 70)};
}

static QPolygonF rectCanvas()
{
    return QPolygonF{QPointF(10, 10), QPointF(110, 10), QPointF(110, 80), QPointF(10, 80)};
}

// 展开图 100×100 -> 画布上底 200、下底 320 的梯形（真实透视，非仿射）
static QPolygonF trapezoidSurface()
{
    return QPolygonF{QPointF(0, 0), QPointF(100, 0), QPointF(100, 100), QPointF(0, 100)};
}

static QPolygonF trapezoidCanvas()
{
    return QPolygonF{QPointF(100, 0), QPointF(300, 0), QPointF(360, 200), QPointF(40, 200)};
}

// 逐点比较：失败时把两边的数值都打出来，方便定位偏差
static void comparePoints(const QPointF &actual, const QPointF &expected, qreal tolerance = 1e-6)
{
    const QString message = QStringLiteral("实际 (%1, %2)，期望 (%3, %4)")
                                .arg(actual.x()).arg(actual.y())
                                .arg(expected.x()).arg(expected.y());
    QVERIFY2(qAbs(actual.x() - expected.x()) <= tolerance
                 && qAbs(actual.y() - expected.y()) <= tolerance,
             qPrintable(message));
}

class PerspectiveTransformTest : public QObject
{
    Q_OBJECT

private slots:
    void defaultInstanceIsInvalid();
    void mapsRectCornersToCorners();
    void mapsTrapezoidCornersExactly();
    void roundTripsPoints();
    void mapsPointsOutsideQuad();
    void failsOnNullResult();
    void rejectsBeyondHorizon();
    void rejectsWrongCornerCounts_data();
    void rejectsWrongCornerCounts();
    void rejectsDegenerateInputs_data();
    void rejectsDegenerateInputs();
    void mapsAffineCoordinates_data();
    void mapsAffineCoordinates();
    void mapsAnalyticalPerspectivePoints_data();
    void mapsAnalyticalPerspectivePoints();
    void mapsBetweenNonRectangularQuads();
    void roundTripsDenseGrid();
    void exposesForwardAndInverseTransforms();
    void ownsCalibrationSnapshot();
    void copiesAndReassignsValidity();
    void rejectsHorizonPoints();
    void rejectsNonFinitePoints_data();
    void rejectsNonFinitePoints();
};

void PerspectiveTransformTest::defaultInstanceIsInvalid()
{
    const PerspectiveTransform transform;
    QVERIFY(!transform.isValid());

    QPointF result(123, 456);
    QVERIFY(!transform.mapForward(QPointF(0, 0), &result));
    QVERIFY(!transform.mapInverse(QPointF(0, 0), &result));
    QCOMPARE(result, QPointF(123, 456)); // 失败时不写出参
}





void PerspectiveTransformTest::mapsRectCornersToCorners()
{
    const PerspectiveTransform transform(rectSurface(), rectCanvas());
    QVERIFY(transform.isValid());

    QPointF result;
    QVERIFY(transform.mapForward(QPointF(0, 0), &result));
    comparePoints(result, QPointF(10, 10));
    QVERIFY(transform.mapForward(QPointF(100, 0), &result));
    comparePoints(result, QPointF(110, 10));
    QVERIFY(transform.mapForward(QPointF(100, 70), &result));
    comparePoints(result, QPointF(110, 80));
    QVERIFY(transform.mapForward(QPointF(50, 35), &result));
    comparePoints(result, QPointF(60, 45));

    QVERIFY(transform.mapInverse(QPointF(60, 45), &result));
    comparePoints(result, QPointF(50, 35));
    QVERIFY(transform.mapInverse(QPointF(10, 80), &result));
    comparePoints(result, QPointF(0, 70));
}

void PerspectiveTransformTest::mapsTrapezoidCornersExactly()
{
    const PerspectiveTransform transform(trapezoidSurface(), trapezoidCanvas());
    QVERIFY(transform.isValid());
    QVERIFY(!transform.forward().isAffine()); // 梯形必然带透视项

    for (int i = 0; i < 4; ++i) {
        QPointF result;
        QVERIFY(transform.mapForward(trapezoidSurface().at(i), &result));
        comparePoints(result, trapezoidCanvas().at(i));
    }
}

void PerspectiveTransformTest::roundTripsPoints()
{
    const PerspectiveTransform transform(trapezoidSurface(), trapezoidCanvas());
    QVERIFY(transform.isValid());

    const QList<QPointF> points{QPointF(0, 0), QPointF(50, 50), QPointF(37, 52),
                                QPointF(100, 100), QPointF(99.5, 0.5)};
    for (const QPointF &point : points) {
        QPointF canvasPoint;
        QVERIFY(transform.mapForward(point, &canvasPoint));
        QPointF back;
        QVERIFY(transform.mapInverse(canvasPoint, &back));
        comparePoints(back, point);
    }
}

void PerspectiveTransformTest::mapsPointsOutsideQuad()
{
    // 四边形只是标定尺，点允许落在它外面（布可以延伸到平面之外）
    const PerspectiveTransform transform(rectSurface(), rectCanvas());
    QVERIFY(transform.isValid());

    QPointF outside;
    QVERIFY(transform.mapForward(QPointF(-30, 100), &outside));
    comparePoints(outside, QPointF(-20, 110));

    QPointF back;
    QVERIFY(transform.mapInverse(outside, &back));
    comparePoints(back, QPointF(-30, 100));
}

void PerspectiveTransformTest::failsOnNullResult()
{
    const PerspectiveTransform transform(rectSurface(), rectCanvas());
    QVERIFY(transform.isValid());

    QVERIFY(!transform.mapForward(QPointF(0, 0), nullptr));
    QVERIFY(!transform.mapInverse(QPointF(0, 0), nullptr));
}

void PerspectiveTransformTest::rejectsBeyondHorizon()
{
    const PerspectiveTransform transform(trapezoidSurface(), trapezoidCanvas());
    QVERIFY(transform.isValid());

    // 地平线即 w = m13*x + m23*y + m33 = 0 的那条线，跨过去映射到无穷远
    // （本组梯形数据下地平线在展开图 y ≈ 266.7，探针取 366.7，是拖拽时真会碰到的越界位置）
    const QTransform &forward = transform.forward();
    QVERIFY(qAbs(forward.m23()) > 1e-9);
    const qreal horizonY = -forward.m33() / forward.m23();

    qreal probeY = horizonY + 100.0;   // x 取 0，探针只沿 y 移动
    if (forward.m23() * probeY + forward.m33() > 0)
        probeY = horizonY - 100.0;
    QVERIFY(forward.m23() * probeY + forward.m33() <= 0);

    QPointF result(123,456);
    QVERIFY(!transform.mapForward(QPointF(0, probeY), &result));
    QCOMPARE(result, QPointF(123,456));
}

// 用明确的解析式提供预期结果，避免仅靠正反向往返掩盖成对的错误。
static QPointF affinePoint(const QPointF &p, qreal a, qreal b, qreal c, qreal d,
                           qreal tx, qreal ty)
{
    return {a * p.x() + b * p.y() + tx, c * p.x() + d * p.y() + ty};
}

static QPointF projectivePoint(const QPointF &p)
{
    const qreal w = 1 + 0.015 * p.x() - 0.02 * p.y();
    return {(1.2 * p.x() + 0.3 * p.y() + 8) / w,
            (-0.2 * p.x() + 0.9 * p.y() - 3) / w};
}

void PerspectiveTransformTest::rejectsWrongCornerCounts_data()
{
    QTest::addColumn<int>("sourceCount");
    QTest::addColumn<int>("targetCount");
    for (const int count : {0, 1, 2, 3, 5, 6}) {
        const QByteArray sourceRow = "source-" + QByteArray::number(count);
        const QByteArray targetRow = "target-" + QByteArray::number(count);
        QTest::newRow(sourceRow.constData()) << count << 4;
        QTest::newRow(targetRow.constData()) << 4 << count;
    }
    QTest::newRow("both-empty") << 0 << 0;
    QTest::newRow("both-too-many") << 5 << 6;
}

void PerspectiveTransformTest::rejectsWrongCornerCounts()
{
    QFETCH(int, sourceCount);
    QFETCH(int, targetCount);
    QPolygonF source = rectSurface();
    QPolygonF target = rectCanvas();
    source.resize(sourceCount);
    target.resize(targetCount);
    const PerspectiveTransform transform(source, target);
    QVERIFY(!transform.isValid());
    QPointF result(123, 456);
    QVERIFY(!transform.mapForward(QPointF(25, 30), &result));
    QCOMPARE(result, QPointF(123, 456));
    QVERIFY(!transform.mapInverse(QPointF(25, 30), &result));
    QCOMPARE(result, QPointF(123, 456));
}

void PerspectiveTransformTest::rejectsDegenerateInputs_data()
{
    QTest::addColumn<QPolygonF>("degenerate");
    QTest::addColumn<bool>("invalidSource");
    const QList<QPair<QByteArray, QPolygonF>> cases{
        {"collapsed", {QPointF(5, 5), QPointF(5, 5), QPointF(5, 5), QPointF(5, 5)}},
        {"horizontal-line", {QPointF(0, 0), QPointF(10, 0), QPointF(20, 0), QPointF(30, 0)}},
        {"vertical-line", {QPointF(0, 0), QPointF(0, 10), QPointF(0, 20), QPointF(0, 30)}},
        {"zero-width", {QPointF(5, 0), QPointF(5, 0), QPointF(5, 70), QPointF(5, 70)}},
        {"zero-height", {QPointF(0, 5), QPointF(100, 5), QPointF(100, 5), QPointF(0, 5)}}
    };
    for (const auto &entry : cases) {
        const QByteArray sourceRow = "source-" + entry.first;
        const QByteArray targetRow = "target-" + entry.first;
        QTest::newRow(sourceRow.constData()) << entry.second << true;
        QTest::newRow(targetRow.constData()) << entry.second << false;
    }
}

void PerspectiveTransformTest::rejectsDegenerateInputs()
{
    QFETCH(QPolygonF, degenerate);
    QFETCH(bool, invalidSource);
    const PerspectiveTransform transform(invalidSource ? degenerate : rectSurface(),
                                         invalidSource ? rectCanvas() : degenerate);
    QVERIFY(!transform.isValid());
    QPointF result(123, 456);
    QVERIFY(!transform.mapForward(QPointF(), &result));
    QCOMPARE(result, QPointF(123, 456));
    QVERIFY(!transform.mapInverse(QPointF(), &result));
    QCOMPARE(result, QPointF(123, 456));
}

void PerspectiveTransformTest::mapsAffineCoordinates_data()
{
    QTest::addColumn<qreal>("a");
    QTest::addColumn<qreal>("b");
    QTest::addColumn<qreal>("c");
    QTest::addColumn<qreal>("d");
    QTest::addColumn<qreal>("tx");
    QTest::addColumn<qreal>("ty");
    QTest::addColumn<qreal>("tolerance");
    QTest::newRow("identity") << 1.0 << 0.0 << 0.0 << 1.0 << 0.0 << 0.0 << 1e-6;
    QTest::newRow("translation") << 1.0 << 0.0 << 0.0 << 1.0 << -30.0 << 42.0 << 1e-6;
    QTest::newRow("nonuniform-scale") << 2.0 << 0.0 << 0.0 << 0.5 << 10.0 << 20.0 << 1e-6;
    QTest::newRow("rotation") << 0.0 << -1.0 << 1.0 << 0.0 << 80.0 << 10.0 << 1e-6;
    QTest::newRow("reflection") << -1.0 << 0.0 << 0.0 << 1.0 << 100.0 << 0.0 << 1e-6;
    QTest::newRow("shear") << 1.0 << 0.4 << -0.3 << 1.0 << 7.0 << -9.0 << 1e-6;
    QTest::newRow("combined") << 1.2 << -0.6 << 0.8 << 2.0 << -50.0 << 33.0 << 1e-6;
    QTest::newRow("large-coordinates") << 120.0 << 40.0 << -25.0 << 200.0
                                      << 8000000.0 << -6000000.0 << 1e-4;
    QTest::newRow("small-scale") << 0.0001 << 0.0 << 0.0 << 0.0002 << 0.0 << 0.0 << 1e-6;
}

void PerspectiveTransformTest::mapsAffineCoordinates()
{
    QFETCH(qreal, a);
    QFETCH(qreal, b);
    QFETCH(qreal, c);
    QFETCH(qreal, d);
    QFETCH(qreal, tx);
    QFETCH(qreal, ty);
    QFETCH(qreal, tolerance);
    const QPolygonF source = rectSurface();
    QPolygonF target;
    for (const QPointF &point : source)
        target.append(affinePoint(point, a, b, c, d, tx, ty));
    const PerspectiveTransform transform(source, target);
    QVERIFY(transform.isValid());
    QVERIFY(transform.forward().isAffine());
    QList<QPointF> points = source;
    points.append({QPointF(50, 35), QPointF(12.5, 63.75), QPointF(-30, 100),
                   QPointF(150, -25), QPointF(0, 35), QPointF(50, 0)});
    for (const QPointF &point : points) {
        const QPointF expected = affinePoint(point, a, b, c, d, tx, ty);
        QPointF mapped;
        QVERIFY(transform.mapForward(point, &mapped));
        comparePoints(mapped, expected, tolerance);
        QVERIFY(transform.mapInverse(expected, &mapped));
        comparePoints(mapped, point, tolerance);
    }
}

void PerspectiveTransformTest::mapsAnalyticalPerspectivePoints_data()
{
    QTest::addColumn<QPointF>("source");
    QTest::addColumn<QPointF>("target");
    QTest::newRow("top-left") << QPointF(0, 0) << QPointF(100, 0);
    QTest::newRow("top-right") << QPointF(100, 0) << QPointF(300, 0);
    QTest::newRow("bottom-right") << QPointF(100, 100) << QPointF(360, 200);
    QTest::newRow("bottom-left") << QPointF(0, 100) << QPointF(40, 200);
    // x'=(2x-.75y+100)/(1-.00375y), y'=1.25y/(1-.00375y)。
    // 透视映射的中心 y 不是角点 y 的平均值。
    QTest::newRow("center") << QPointF(50, 50) << QPointF(200, 1000.0 / 13);
    QTest::newRow("interior") << QPointF(25, 40) << QPointF(2400.0 / 17, 1000.0 / 17);
    QTest::newRow("left-edge") << QPointF(0, 50) << QPointF(1000.0 / 13, 1000.0 / 13);
    QTest::newRow("right-edge") << QPointF(100, 50) << QPointF(4200.0 / 13, 1000.0 / 13);
    QTest::newRow("outside-left") << QPointF(-50, 0) << QPointF(0, 0);
    QTest::newRow("outside-below") << QPointF(50, 200) << QPointF(200, 1000);
    QTest::newRow("outside-above") << QPointF(50, -100) << QPointF(200, -1000.0 / 11);
}

void PerspectiveTransformTest::mapsAnalyticalPerspectivePoints()
{
    QFETCH(QPointF, source);
    QFETCH(QPointF, target);
    const PerspectiveTransform transform(trapezoidSurface(), trapezoidCanvas());
    QVERIFY(transform.isValid());
    QVERIFY(!transform.forward().isAffine());
    QPointF result;
    QVERIFY(transform.mapForward(source, &result));
    comparePoints(result, target);
    QVERIFY(transform.mapInverse(target, &result));
    comparePoints(result, source);
}

void PerspectiveTransformTest::mapsBetweenNonRectangularQuads()
{
    const QPolygonF source{QPointF(-3, 7), QPointF(5, 9), QPointF(6, 16), QPointF(-4, 15)};
    QPolygonF target;
    for (const QPointF &point : source)
        target.append(projectivePoint(point));
    const PerspectiveTransform transform(source, target);
    const PerspectiveTransform swapped(target, source);
    QVERIFY(transform.isValid());
    QVERIFY(swapped.isValid());
    QList<QPointF> points = source;
    points.append({QPointF(0, 10), QPointF(2, 12), QPointF(-8, 5), QPointF(10, 18)});
    for (const QPointF &point : points) {
        const QPointF expected = projectivePoint(point);
        QPointF result;
        QVERIFY(transform.mapForward(point, &result));
        comparePoints(result, expected);
        QVERIFY(transform.mapInverse(expected, &result));
        comparePoints(result, point);
        QVERIFY(swapped.mapForward(expected, &result));
        comparePoints(result, point);
        QVERIFY(swapped.mapInverse(point, &result));
        comparePoints(result, expected);
    }
}

void PerspectiveTransformTest::roundTripsDenseGrid()
{
    const PerspectiveTransform transform(trapezoidSurface(), trapezoidCanvas());
    QVERIFY(transform.isValid());
    // 361 个点包含边界附近、内部和外部，均不越过地平线。
    for (int x = -40; x <= 140; x += 10) {
        for (int y = -40; y <= 140; y += 10) {
            const QPointF point(x + 0.125, y + 0.375);
            QPointF projected;
            QPointF restored;
            QVERIFY(transform.mapForward(point, &projected));
            QVERIFY(transform.mapInverse(projected, &restored));
            comparePoints(restored, point);
        }
    }
}

void PerspectiveTransformTest::exposesForwardAndInverseTransforms()
{
    const PerspectiveTransform transform(trapezoidSurface(), trapezoidCanvas());
    const QPointF source(25, 40);
    const QPointF expected(2400.0 / 17, 1000.0 / 17);
    comparePoints(transform.forward().map(source), expected);
    QPointF restored;
    QVERIFY(transform.mapInverse(expected, &restored));
    comparePoints(restored, source);
}

void PerspectiveTransformTest::ownsCalibrationSnapshot()
{
    QPolygonF source = trapezoidSurface();
    QPolygonF target = trapezoidCanvas();
    const PerspectiveTransform transform(source, target);
    source[0] = QPointF(1000, 1000);
    target.clear();
    QPointF result;
    QVERIFY(transform.mapForward(QPointF(50, 50), &result));
    comparePoints(result, QPointF(200, 1000.0 / 13));
}

void PerspectiveTransformTest::copiesAndReassignsValidity()
{
    PerspectiveTransform original(trapezoidSurface(), trapezoidCanvas());
    PerspectiveTransform copied(original);
    original = PerspectiveTransform();
    QVERIFY(!original.isValid());
    QPointF result(123, 456);
    QVERIFY(!original.mapForward(QPointF(), &result));
    QCOMPARE(result, QPointF(123, 456));
    QVERIFY(!original.mapInverse(QPointF(), &result));
    QCOMPARE(result, QPointF(123, 456));
    QVERIFY(copied.mapInverse(QPointF(200, 1000.0 / 13), &result));
    comparePoints(result, QPointF(50, 50));
    original = copied;
    QVERIFY(original.isValid());
    copied = PerspectiveTransform(rectSurface(), rectSurface());
    QVERIFY(original.mapForward(QPointF(50, 50), &result));
    comparePoints(result, QPointF(200, 1000.0 / 13));
    original = PerspectiveTransform(QPolygonF(), rectCanvas());
    QVERIFY(!original.isValid());
    QVERIFY(!original.mapForward(QPointF(), &result));
}

void PerspectiveTransformTest::rejectsHorizonPoints()
{
    const PerspectiveTransform transform(trapezoidSurface(), trapezoidCanvas());
    const QTransform &forward = transform.forward();
    QVERIFY(qAbs(forward.m23()) > 1e-9);
    const qreal horizonY = -forward.m33() / forward.m23();
    QPointF result;
    // 不额外规定 Qt 在地平线的坐标截断方式，只验证委托行为。
    QVERIFY(!transform.mapForward(QPointF(0, horizonY), &result));
    for (const qreal y : {horizonY - 1, horizonY + 1, horizonY + 100}) {
        const QPointF point(0, y);
        if (forward.m23()*y + forward.m33() <= 0) {
            QVERIFY(!transform.mapForward(point, &result));
        } else {
            QVERIFY(transform.mapForward(point, &result));
            comparePoints(result, forward.map(point));
        }
    }
    const QTransform inverse = transform.forward().inverted();
    QVERIFY(qAbs(inverse.m23()) > 1e-9);
    const QPointF inverseHorizon(0, -inverse.m33() / inverse.m23());
    QVERIFY(!transform.mapInverse(inverseHorizon, &result));
}

void PerspectiveTransformTest::rejectsNonFinitePoints_data()
{
    QTest::addColumn<QPointF>("point");
    const qreal nan = std::numeric_limits<qreal>::quiet_NaN();
    const qreal infinity = std::numeric_limits<qreal>::infinity();
    QTest::newRow("nan-x") << QPointF(nan, 25);
    QTest::newRow("nan-y") << QPointF(25, nan);
    QTest::newRow("positive-infinity-x") << QPointF(infinity, 25);
    QTest::newRow("negative-infinity-x") << QPointF(-infinity, 25);
    QTest::newRow("positive-infinity-y") << QPointF(25, infinity);
    QTest::newRow("negative-infinity-y") << QPointF(25, -infinity);
}

void PerspectiveTransformTest::rejectsNonFinitePoints()
{
    QFETCH(QPointF, point);
    const PerspectiveTransform transform(rectSurface(), rectSurface());
    QVERIFY(transform.isValid());
    QPointF result;
    result = QPointF(123,456);
    QVERIFY(!transform.mapForward(point, &result));
    QCOMPARE(result, QPointF(123,456));
    QVERIFY(!transform.mapInverse(point, &result));
    QCOMPARE(result, QPointF(123,456));
}

QTEST_APPLESS_MAIN(PerspectiveTransformTest)
#include "tst_perspectivetransform.moc"
