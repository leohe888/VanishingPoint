#include <QtTest>
#include <QTemporaryDir>
#include <QPainter>

#include "core/brushtool.h"
#include "core/vpdocument.h"
#include "testhelpers.h"

class BrushToolTest : public QObject
{
    Q_OBJECT

private slots:
    void clampsParameters();
    void refusesToStart();
    void paintsOnPlane();
    void paintsAtCanvasScale();
    void appliesOpacity();
    void previewDrawsDab();
    void undoRemovesWholeStroke();

private:
    static QImage makeLayer();
    static int alphaAt(const QImage &layer, int x, int y);
};

QImage BrushToolTest::makeLayer()
{
    QImage layer(200, 120, QImage::Format_ARGB32);
    layer.fill(Qt::transparent);
    return layer;
}

int BrushToolTest::alphaAt(const QImage &layer, int x, int y)
{
    return qAlpha(layer.pixel(x, y));
}

// 参数越界时由引擎钳到合法区间
void BrushToolTest::clampsParameters()
{
    BrushTool tool;
    tool.setDiameter(0);
    QCOMPARE(tool.diameter(), 1);
    tool.setDiameter(999);
    QCOMPARE(tool.diameter(), 500);
    tool.setHardness(-20);
    QCOMPARE(tool.hardness(), 0);
    tool.setHardness(400);
    QCOMPARE(tool.hardness(), 100);
    tool.setOpacity(0);
    QCOMPARE(tool.opacity(), 1);
    tool.setOpacity(200);
    QCOMPARE(tool.opacity(), 100);
}

// 没有画布或没有绘画层时不应进入落笔状态
void BrushToolTest::refusesToStart()
{
    QImage layer = makeLayer();
    QImage nullLayer;
    BrushTool tool;

    QVERIFY(tool.begin(nullLayer, {}, layer.size(), QPointF(100, 60)).isEmpty());
    QVERIFY(!tool.drawing());

    QVERIFY(tool.begin(layer, {}, QSize(), QPointF(100, 60)).isEmpty());
    QVERIFY(!tool.drawing());
}

// 平面内落笔：笔触跟随面片尺度，直径 20 在边长 100 的平面上覆盖 20 像素
void BrushToolTest::paintsOnPlane()
{
    QImage layer = makeLayer();
    const QVector<PerspectivePlane> planes{makeTestPlane()};
    BrushTool tool;
    tool.setDiameter(20);
    tool.setHardness(100);
    tool.setOpacity(100);
    tool.setColor(Qt::black);

    QVERIFY(!tool.begin(layer, planes, layer.size(), QPointF(60, 45)).isEmpty());
    QVERIFY(tool.drawing());
    QCOMPARE(alphaAt(layer, 60, 45), 255);
    QCOMPARE(alphaAt(layer, 60, 20), 0);

    // 续笔：从 (60,45) 拖到 (90,45) 应留下连续笔迹
    QVERIFY(!tool.move(layer, QPointF(90, 45)).isEmpty());
    QCOMPARE(alphaAt(layer, 90, 45), 255);
    QCOMPARE(alphaAt(layer, 75, 45), 255);

    tool.end();
    QVERIFY(!tool.drawing());
}

// 平面之外落笔：以整张图像为基准面，直径即画布像素直径
void BrushToolTest::paintsAtCanvasScale()
{
    QImage layer = makeLayer();
    BrushTool tool;
    tool.setDiameter(40);
    tool.setHardness(100);
    tool.setOpacity(100);
    tool.setColor(Qt::black);

    QVERIFY(!tool.begin(layer, {}, layer.size(), QPointF(100, 60)).isEmpty());
    QCOMPARE(alphaAt(layer, 100, 60), 255);
    QVERIFY(alphaAt(layer, 85, 60) > 0);
    QVERIFY(alphaAt(layer, 115, 60) > 0);
    QCOMPARE(alphaAt(layer, 70, 60), 0);
    QCOMPARE(alphaAt(layer, 130, 60), 0);
}

// 不透明度落在笔触像素的 alpha 上
void BrushToolTest::appliesOpacity()
{
    QImage layer = makeLayer();
    BrushTool tool;
    tool.setDiameter(40);
    tool.setHardness(100);
    tool.setOpacity(50);
    tool.setColor(Qt::black);

    tool.begin(layer, {}, layer.size(), QPointF(100, 60));
    QVERIFY(qAbs(alphaAt(layer, 100, 60) - 128) <= 2);
}

// 光标预览落的是笔触本身（内容），不是轮廓
void BrushToolTest::previewDrawsDab()
{
    QImage canvas = makeLayer();
    BrushTool tool;
    tool.setDiameter(40);
    tool.setHardness(100);
    tool.setOpacity(100);
    tool.setColor(Qt::red);

    QPainter painter(&canvas);
    tool.renderPreview(painter, {}, canvas.size(), QPointF(100, 60));
    painter.end();

    QCOMPARE(alphaAt(canvas, 100, 60), 255);
    QCOMPARE(QColor(canvas.pixel(100, 60)).rgb(), QColor(Qt::red).rgb());
    QCOMPARE(alphaAt(canvas, 100, 20), 0);
}

// 一次落笔只记一格历史，撤销必须连第一个笔触点一起回退
void BrushToolTest::undoRemovesWholeStroke()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("background.png"));
    QImage background(200, 120, QImage::Format_ARGB32);
    background.fill(Qt::white);
    QVERIFY(background.save(path));

    VpDocument document;
    QVERIFY(document.loadImage(path));

    BrushTool tool;
    tool.setDiameter(40);
    tool.setHardness(100);
    tool.setOpacity(100);
    tool.setColor(Qt::black);

    // 与 VpCanvas 的文案一致：先开事务，再落笔
    document.beginPaintTransaction();
    document.addPaintDirty(tool.begin(document.paintLayer(), {}, background.size(), QPointF(100, 60)));
    document.addPaintDirty(tool.move(document.paintLayer(), QPointF(140, 60)));
    tool.end();
    document.commitHistory();

    QCOMPARE(alphaAt(document.paintLayer(), 100, 60), 255);
    QCOMPARE(alphaAt(document.paintLayer(), 140, 60), 255);

    QVERIFY(document.undo());
    QCOMPARE(alphaAt(document.paintLayer(), 100, 60), 0);
    QCOMPARE(alphaAt(document.paintLayer(), 140, 60), 0);

    QVERIFY(document.redo());
    QCOMPARE(alphaAt(document.paintLayer(), 100, 60), 255);
    QCOMPARE(alphaAt(document.paintLayer(), 140, 60), 255);
}

QTEST_GUILESS_MAIN(BrushToolTest)
#include "tst_brushtool.moc"