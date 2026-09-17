#include <QtTest>

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
    void rejectsQuadWithoutFourCorners();
    void rejectsDegenerateQuad();
    void mapsRectCornersToCorners();
    void mapsTrapezoidCornersExactly();
    void roundTripsPoints();
    void mapsPointsOutsideQuad();
    void failsOnNullResult();
    void mapsBeyondHorizonWithoutError();
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

void PerspectiveTransformTest::rejectsQuadWithoutFourCorners()
{
    const QPolygonF triangle{QPointF(0, 0), QPointF(100, 0), QPointF(0, 100)};
    const QPolygonF pentagon{QPointF(0, 0), QPointF(100, 0), QPointF(120, 50),
                             QPointF(50, 100), QPointF(0, 60)};

    QVERIFY(!PerspectiveTransform(QPolygonF(), rectCanvas()).isValid());
    QVERIFY(!PerspectiveTransform(triangle, rectCanvas()).isValid());
    QVERIFY(!PerspectiveTransform(rectSurface(), triangle).isValid());
    QVERIFY(!PerspectiveTransform(pentagon, rectCanvas()).isValid());
}

void PerspectiveTransformTest::rejectsDegenerateQuad()
{
    const QPolygonF collapsed{QPointF(5, 5), QPointF(5, 5), QPointF(5, 5), QPointF(5, 5)};
    const QPolygonF collinear{QPointF(0, 0), QPointF(10, 0), QPointF(20, 0), QPointF(30, 0)};

    QVERIFY(!PerspectiveTransform(collapsed, rectCanvas()).isValid());
    QVERIFY(!PerspectiveTransform(rectSurface(), collapsed).isValid());
    QVERIFY(!PerspectiveTransform(rectSurface(), collinear).isValid());
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

void PerspectiveTransformTest::mapsBeyondHorizonWithoutError()
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

    // 本类只做映射、不校验定义域：越界时照样返回 true，给的是有限但无意义的坐标
    QPointF result;
    QVERIFY(transform.mapForward(QPointF(0, probeY), &result));
    QVERIFY(qIsFinite(result.x()) && qIsFinite(result.y()));
    QVERIFY(qAbs(result.x()) > 1e4 || qAbs(result.y()) > 1e4);
}

QTEST_APPLESS_MAIN(PerspectiveTransformTest)
#include "tst_perspectivetransform.moc"
