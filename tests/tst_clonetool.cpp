#include <QtTest>
#include <QTemporaryDir>
#include <QPainter>

#include "core/canvasdocument.h"
#include "core/clonetool.h"
#include "testhelpers.h"

class CloneToolTest : public QObject
{
    Q_OBJECT

private slots:
    void clampsParameters();
    void refusesToStart();
    void clonesFromSourceOffset();
    void strokeKeepsOffset();
    void followsPlanePerspective();
    void alignedMovesSourceWithCursor();
    void unalignedReturnsToOriginalSource();
    void unalignedPreviewFollowsDuringStroke();
    void previewDrawsSampledContent();
    void undoRemovesWholeStroke();

private:
    static QImage makeLayer();
    static QImage makeSource();
    static int alphaAt(const QImage &image, int x, int y);
    static QRgb rgbAt(const QImage &image, int x, int y);
    static CloneTool readyTool();
};

QImage CloneToolTest::makeLayer()
{
    QImage layer(200, 120, QImage::Format_ARGB32);
    layer.fill(Qt::transparent);
    return layer;
}

// 白色底 + 一块红色标记：仿制到哪个像素一目了然
QImage CloneToolTest::makeSource()
{
    QImage source(200, 120, QImage::Format_ARGB32);
    source.fill(Qt::white);
    QPainter painter(&source);
    painter.fillRect(50, 30, 40, 40, Qt::red);
    painter.end();
    return source;
}

int CloneToolTest::alphaAt(const QImage &image, int x, int y)
{
    return qAlpha(image.pixel(x, y));
}

QRgb CloneToolTest::rgbAt(const QImage &image, int x, int y)
{
    return QColor(image.pixel(x, y)).rgb();
}

// 直径 8、完全硬边、完全不透明：笔触点中心像素与源像素逐一对应
CloneTool CloneToolTest::readyTool()
{
    CloneTool tool;
    tool.setDiameter(8);
    tool.setHardness(100);
    tool.setOpacity(100);
    return tool;
}

// 参数越界时由引擎钳到合法区间
void CloneToolTest::clampsParameters()
{
    CloneTool tool;
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

// 没有源点、绘画层无效或面片无法解析时都不应进入落笔状态
void CloneToolTest::refusesToStart()
{
    QImage layer = makeLayer();
    const QImage source = makeSource();
    QImage nullLayer;
    CloneTool tool = readyTool();

    QVERIFY(tool.begin(layer, source, {}, layer.size(), QPointF(100, 60)).isEmpty());
    QVERIFY(!tool.drawing());
    QVERIFY(!tool.pickSource({}, QSize(), QPointF(60, 45)));
    QVERIFY(!tool.hasSource());

    QVERIFY(tool.pickSource({}, layer.size(), QPointF(60, 45)));
    QVERIFY(tool.begin(nullLayer, source, {}, layer.size(), QPointF(100, 60)).isEmpty());
    QVERIFY(!tool.drawing());
}

// 落点取样自源点：按下点 (100,60) 与源点 (60,45) 的偏移在整笔中恒定
void CloneToolTest::clonesFromSourceOffset()
{
    QImage layer = makeLayer();
    const QImage source = makeSource();
    CloneTool tool = readyTool();
    QVERIFY(tool.pickSource({}, layer.size(), QPointF(60, 45)));
    QCOMPARE(tool.marker(), QPointF(60, 45));

    QVERIFY(!tool.begin(layer, source, {}, layer.size(), QPointF(100, 60)).isEmpty());
    QVERIFY(tool.drawing());
    QCOMPARE(rgbAt(layer, 100, 60), rgbAt(source, 60, 45));
    QCOMPARE(alphaAt(layer, 100, 60), 255);
    QCOMPARE(alphaAt(layer, 100, 20), 0); // 笔触直径之外不应被碰到
}

// 续笔沿笔迹补间，取样位置跟着光标一起平移
void CloneToolTest::strokeKeepsOffset()
{
    QImage layer = makeLayer();
    const QImage source = makeSource();
    CloneTool tool = readyTool();
    tool.pickSource({}, layer.size(), QPointF(60, 45));
    tool.begin(layer, source, {}, layer.size(), QPointF(100, 60));

    QVERIFY(!tool.move(layer, QPointF(140, 60)).isEmpty());
    QCOMPARE(rgbAt(layer, 120, 60), rgbAt(source, 80, 45));   // 源点仍落在红色标记内
    QCOMPARE(rgbAt(layer, 140, 60), rgbAt(source, 100, 45));  // 源点已滑出标记
    QCOMPARE(alphaAt(layer, 120, 60), 255);

    tool.end();
    QVERIFY(!tool.drawing());
}

// 源点与落点都在平面上时，偏移记在展开曲面上，取样位置随平面透视换算
void CloneToolTest::followsPlanePerspective()
{
    QImage layer = makeLayer();
    const QImage source = makeSource();
    const QVector<Plane> planes{makeTestPlane()}; // 画面角点 = 展开坐标 + (10,10)
    CloneTool tool = readyTool();
    QVERIFY(tool.pickSource(planes, layer.size(), QPointF(40, 40)));

    QVERIFY(!tool.begin(layer, source, planes, layer.size(), QPointF(90, 60)).isEmpty());
    QCOMPARE(rgbAt(layer, 90, 60), rgbAt(source, 40, 40));
    QCOMPARE(alphaAt(layer, 90, 60), 255);
}

// 对齐：偏移跨笔保留，抬笔后光标再动，源点仍按同样的位移跟着走
void CloneToolTest::alignedMovesSourceWithCursor()
{
    QImage layer = makeLayer();
    const QImage source = makeSource();
    CloneTool tool = readyTool();
    QVERIFY(tool.aligned());
    tool.pickSource({}, layer.size(), QPointF(60, 45));

    tool.begin(layer, source, {}, layer.size(), QPointF(100, 60));
    tool.hover({}, layer.size(), QPointF(120, 60)); // 绘制中：光标 +20，源点 +20
    QCOMPARE(tool.marker(), QPointF(80, 45));
    tool.end();

    tool.hover({}, layer.size(), QPointF(130, 60)); // 抬笔后继续移动，源点继续跟随
    QCOMPARE(tool.marker(), QPointF(90, 45));
}

// 非对齐：一笔之内源点跟随光标，抬笔即归位，下一笔重新锚定
void CloneToolTest::unalignedReturnsToOriginalSource()
{
    QImage layer = makeLayer();
    const QImage source = makeSource();
    CloneTool tool = readyTool();
    tool.setAligned(false);
    tool.pickSource({}, layer.size(), QPointF(60, 45));

    tool.begin(layer, source, {}, layer.size(), QPointF(100, 60));
    tool.hover({}, layer.size(), QPointF(120, 60));
    QCOMPARE(tool.marker(), QPointF(80, 45));
    tool.end();
    QCOMPARE(tool.marker(), QPointF(60, 45));

    // 第二笔从新落点重新锚定，取到的仍是源点本身的内容
    QVERIFY(!tool.begin(layer, source, {}, layer.size(), QPointF(150, 60)).isEmpty());
    QCOMPARE(rgbAt(layer, 150, 60), rgbAt(source, 60, 45));
}

// 非对齐模式下"源点归位"只发生在抬笔之后：落笔期间预览跟着源点走
void CloneToolTest::unalignedPreviewFollowsDuringStroke()
{
    QImage canvas = makeLayer();
    const QImage source = makeSource(); // 红色标记在 x∈[50,90)、y∈[30,70)
    CloneTool tool = readyTool();
    tool.setAligned(false);
    tool.pickSource({}, canvas.size(), QPointF(60, 45));

    tool.begin(canvas, source, {}, canvas.size(), QPointF(100, 60));
    QPainter painter(&canvas);
    tool.renderPreview(painter, source, {}, canvas.size(), QPointF(140, 60));
    painter.end();
    // 光标 +40，取样位置也 +40，已滑出红色标记；取到原位的内容就说明没跟随
    QCOMPARE(rgbAt(canvas, 140, 60), rgbAt(source, 100, 45));
    QVERIFY(rgbAt(canvas, 140, 60) != rgbAt(source, 60, 45));

    tool.end();
    QImage after = makeLayer();
    QPainter afterPainter(&after);
    tool.renderPreview(afterPainter, source, {}, after.size(), QPointF(140, 60));
    afterPainter.end();
    QCOMPARE(rgbAt(after, 140, 60), rgbAt(source, 60, 45)); // 抬笔后回到最开始的位置
}

// 光标预览落的就是即将仿制过来的内容，不是轮廓
void CloneToolTest::previewDrawsSampledContent()
{
    QImage canvas = makeLayer();
    const QImage source = makeSource();
    CloneTool tool = readyTool();
    tool.pickSource({}, canvas.size(), QPointF(60, 45));

    QPainter painter(&canvas);
    tool.renderPreview(painter, source, {}, canvas.size(), QPointF(100, 60));
    painter.end();

    QCOMPARE(rgbAt(canvas, 100, 60), rgbAt(source, 60, 45));
    QCOMPARE(alphaAt(canvas, 100, 60), 255);
    QCOMPARE(alphaAt(canvas, 100, 20), 0);
    QVERIFY(!tool.drawing()); // 预览不改变落笔状态
}

// 一次落笔只记一格历史，撤销必须连第一个笔触点一起回退
void CloneToolTest::undoRemovesWholeStroke()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("background.png"));
    QImage background(200, 120, QImage::Format_ARGB32);
    background.fill(Qt::white);
    QVERIFY(background.save(path));

    CanvasDocument document;
    QVERIFY(document.loadImage(path));

    CloneTool tool = readyTool();
    tool.pickSource({}, background.size(), QPointF(60, 45));

    // 与 VpCanvas 的文案一致：先开事务，再落笔
    document.beginPaintTransaction();
    document.addPaintDirty(tool.begin(document.paintLayer(), background, {},
                                      background.size(), QPointF(100, 60)));
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

QTEST_GUILESS_MAIN(CloneToolTest)
#include "tst_clonetool.moc"
