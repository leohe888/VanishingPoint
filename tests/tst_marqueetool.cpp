#include <QtTest>
#include <QPainter>

#include "core/marqueetool.h"
#include "testhelpers.h"

namespace {

QVector<PerspectivePlane> connectedPlanes()
{
    PerspectivePlane right(
        {QPointF(110, 10), QPointF(230, 30), QPointF(230, 96), QPointF(110, 80)},
        {QPointF(100, 0), QPointF(200, 0), QPointF(200, 70), QPointF(100, 70)});
    right.setSurfaceGroupId(0);
    return {makeTestPlane(), right};
}

QPointF canvasPoint(const PerspectivePlane &plane, const QPointF &surface)
{
    QPointF result;
    plane.quad().surfaceToCanvasTransform().mapForward(surface, &result);
    return result;
}

QImage sampleImage(const QVector<PerspectivePlane> &planes)
{
    QImage image(260, 120, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < planes.size(); ++i) {
        painter.setBrush(i == 0 ? Qt::red : Qt::blue);
        painter.drawPolygon(planes[i].quad().canvasPolygon());
    }
    return image;
}

void createAcrossSeam(MarqueeTool &tool, const QVector<PerspectivePlane> &planes)
{
    tool.beginCreate(planes, canvasPoint(planes[0], QPointF(80, 10)));
    tool.update(canvasPoint(planes[1], QPointF(140, 50)), Qt::NoModifier, 50);
    tool.end();
}

} // namespace

class MarqueeToolTest : public QObject
{
    Q_OBJECT
private slots:
    void createsAndMovesWithShift();
    void keepsSurfaceSnapshotAcrossSeam();
    void copiesPixelsAcrossSeam();
    void rebuildsClonePreviewAndExtractsPixels();
    void clearsDegenerateAndCancelledSelection();
};

void MarqueeToolTest::createsAndMovesWithShift()
{
    MarqueeTool tool;
    QVERIFY(tool.beginCreate({makeTestPlane()}, QPointF(60, 45)));
    tool.update(QPointF(40, 35), Qt::ShiftModifier, 10);
    QCOMPARE(tool.rect(), QRectF(30, 15, 20, 20));
    tool.end();
    QVERIFY(!tool.active());
    QVERIFY(tool.beginMove(QPointF(50, 35)));
    tool.update(QPointF(67, 42), Qt::ShiftModifier, 10);
    QCOMPARE(tool.rect(), QRectF(50, 15, 20, 20));
    // 每次从按下时的选区算起，不累加上一帧位移。
    tool.update(QPointF(57, 52), Qt::ShiftModifier, 10);
    QCOMPARE(tool.rect(), QRectF(30, 35, 20, 20));
}

void MarqueeToolTest::keepsSurfaceSnapshotAcrossSeam()
{
    auto planes = connectedPlanes();
    MarqueeTool tool;
    createAcrossSeam(tool, planes);
    QVERIFY(qAbs(tool.rect().left() - 80) < 1e-6);
    QVERIFY(qAbs(tool.rect().width() - 60) < 1e-6);
    const QPointF point = canvasPoint(planes[1], QPointF(130, 30));
    QVERIFY(tool.contains(point));
    QVERIFY(tool.contains(QPointF(100, 40)));
    QVERIFY(!tool.contains(QPointF(40, 40)));
    // 修改文档里的几何不应改变正在使用的选区快照。
    planes[1].quad().setCanvasCorner(2, QPointF(250, 115));
    QPointF surface;
    QVERIFY(tool.mapToSurface(point, &surface));
    QVERIFY(QLineF(surface, QPointF(130, 30)).length() < 1e-6);
}

void MarqueeToolTest::copiesPixelsAcrossSeam()
{
    const auto planes = connectedPlanes();
    MarqueeTool tool;
    createAcrossSeam(tool, planes);
    const FloatingImage image = tool.copy(sampleImage(planes),
                                         canvasPoint(planes[1], QPointF(130, 30)));
    QVERIFY(!image.bitmap.isNull());
    QVERIFY(image.surfaceAttached);
    QCOMPARE(image.hostQuadIndex, 1);
    QCOMPARE(image.surfaceQuads.size(), 2);
    QVERIFY(QLineF(image.placementOrigin, QPointF(80, 10)).length() < 1e-6);
    QCOMPARE(image.bitmap.pixelColor(10, 20), QColor(Qt::red));
    QCOMPARE(image.bitmap.pixelColor(40, 20), QColor(Qt::blue));
}

void MarqueeToolTest::rebuildsClonePreviewAndExtractsPixels()
{
    const auto planes = connectedPlanes();
    const QImage source = sampleImage(planes);
    QImage before(source.size(), source.format());
    before.fill(Qt::transparent);
    before.setPixelColor(5, 5, Qt::green);
    QImage preview = before;
    MarqueeTool tool;
    createAcrossSeam(tool, planes);
    const QPointF press = canvasPoint(planes[0], QPointF(90, 30));
    QVERIFY(tool.beginFill(press, source, before));
    const QPointF moved = canvasPoint(planes[1], QPointF(110, 30));
    const QRect dirty = tool.update(moved, Qt::ControlModifier, 50, &preview);
    QVERIFY(dirty.contains(QPoint(95, 40)));
    QCOMPARE(preview.pixelColor(95, 40), QColor(Qt::blue));
    QCOMPARE(preview.pixelColor(5, 5), QColor(Qt::green));
    const QImage firstPreview = preview;
    tool.update(press, Qt::ControlModifier, 50, &preview);
    QCOMPARE(preview.pixelColor(95, 40), QColor(Qt::red));
    tool.update(moved, Qt::ControlModifier, 50, &preview);
    QCOMPARE(preview, firstPreview);
    QCOMPARE(before.pixelColor(95, 40), QColor(Qt::transparent));

    const FloatingImage image = tool.clone();
    QCOMPARE(image.bitmap.pixelColor(5, 20), preview.pixelColor(95, 40));
    QCOMPARE(image.bitmap.pixelColor(40, 20), QColor(Qt::blue));
    tool.end();
    QVERIFY(tool.clone().bitmap.isNull());
    QVERIFY(!tool.rect().isEmpty());
}

void MarqueeToolTest::clearsDegenerateAndCancelledSelection()
{
    MarqueeTool tool;
    QVERIFY(tool.beginCreate({makeTestPlane()}, QPointF(30, 30)));
    tool.update(QPointF(30, 60), Qt::NoModifier, 50);
    tool.end();
    QVERIFY(tool.rect().isEmpty());
    QVERIFY(!tool.active());
    createAcrossSeam(tool, connectedPlanes());
    tool.clear();
    QVERIFY(tool.outline().isEmpty());
    QVERIFY(tool.copy(QImage(), QPointF()).bitmap.isNull());
    QVERIFY(!tool.beginMove(QPointF(30, 30)));
    QVERIFY(!tool.beginCreate({makeTestPlane()}, QPointF(-10, -10)));
}

QTEST_APPLESS_MAIN(MarqueeToolTest)
#include "tst_marqueetool.moc"
