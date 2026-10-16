#include <QGuiApplication>
#include <QClipboard>
#include <QHash>
#include <QPainter>
#include <QtTest>
#include <limits>

#include "canvas/vpcontroller.h"
#include "testhelpers.h"
#include "tools/clonetool.h"

namespace {
void setupClone(VpController &controller)
{
    QImage background(200,120,QImage::Format_ARGB32); background.fill(Qt::white);
    { QPainter painter(&background); painter.fillRect(10,10,30,30,Qt::red); }
    controller.document().setImage(background);
    controller.document().appendPlane(makeTestPlane()); controller.document().resetHistory();
    controller.setTool(VpTools::CloneStamp); controller.setCloneDiameter(8);
    controller.setCloneHardness(100); controller.setCloneHealMode(0);
}

bool changesInside(const QImage &before,const QImage &after,const QRect &dirty)
{
    for(int y=0;y<after.height();++y) for(int x=0;x<after.width();++x)
        if(before.pixel(x,y)!=after.pixel(x,y) && !dirty.contains(x,y)) return false;
    return true;
}
}

class CloneToolTest : public QObject
{
    Q_OBJECT

private slots:
    void targetPlaneTracksHoverAndLocksDuringStroke();
    void clampsParameters();
    void refusesToStart();
    void clonesFromSourceOffset();
    void strokeKeepsOffset();
    void followsPlanePerspective();
    void alignedMovesSourceWithCursor();
    void unalignedReturnsToOriginalSource();
    void unalignedPreviewFollowsDuringStroke();
    void previewPreservesActiveStrokeSource();
    void previewDrawsSampledContent();
    void undoRemovesWholeStroke();
    void sourceLifecycleAndFailedPick();
    void alignedModeChanges_data();
    void alignedModeChanges();
    void differentPerspectiveMappingsKeepSnapshots();
    void sourceSnapshotAndNextStrokeSampling();
    void transparentBilinearSampling_data();
    void transparentBilinearSampling();
    void clipsSourceAndDestinationBounds();
    void continuousStrokeAndRelease_data();
    void continuousStrokeAndRelease();
    void invalidMovesRecover_data();
    void invalidMovesRecover();
    void rejectsProjectionPoleAndRecovers();
    void previewAndCoordinateCacheStayConsistent();
    void hardnessAndOpacityControlCoverage();
    void changingParametersPreservesHealingReplay_data();
    void changingParametersPreservesHealingReplay();
    void prepareRunsBeforeWriting();
    void interruptionsAndHistory_data();
    void interruptionsAndHistory();
    void emptyStrokePreservesRedo();
    void pasteFinishesCloneStroke();

private:
    static QImage makeLayer();
    static QImage makeSource();
    static int alphaAt(const QImage &image, int x, int y);
    static QRgb rgbAt(const QImage &image, int x, int y);
    static void readyTool(CloneTool &tool);
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
void CloneToolTest::readyTool(CloneTool &tool)
{
    tool.setDiameter(8);
    tool.setHardness(100);
    tool.setOpacity(100);

}

// 参数越界时钳到合法区间
void CloneToolTest::clampsParameters()
{
    CloneTool tool;
    QCOMPARE(tool.healMode(), int(HealingBlend::On));
    QVERIFY(!tool.aligned());
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
    CloneTool tool; readyTool(tool);

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
    CloneTool tool; readyTool(tool);
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
    CloneTool tool; readyTool(tool);
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
    const QVector<PerspectivePlane> planes{makeTestPlane()}; // 画面角点 = 展开坐标 + (10,10)
    CloneTool tool; readyTool(tool);
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
    CloneTool tool; readyTool(tool);
    tool.setAligned(true);
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
    CloneTool tool; readyTool(tool);
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
    CloneTool tool; readyTool(tool);
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

void CloneToolTest::previewPreservesActiveStrokeSource()
{
    QImage layer = makeLayer();
    const QImage source = makeSource();
    QImage previewSource = source;
    previewSource.fill(Qt::blue);
    CloneTool tool; readyTool(tool);
    QVERIFY(tool.pickSource({}, layer.size(), QPointF(60, 45)));
    QVERIFY(!tool.begin(layer, source, {}, layer.size(), QPointF(100, 60)).isEmpty());

    QImage preview = makeLayer();
    QPainter painter(&preview);
    tool.renderPreview(painter, previewSource, {}, preview.size(), QPointF(120, 60));
    painter.end();

    QVERIFY(!tool.move(layer, QPointF(120, 60)).isEmpty());
    QCOMPARE(rgbAt(layer, 120, 60), rgbAt(source, 80, 45));
}

// 光标预览落的就是即将仿制过来的内容，不是轮廓
void CloneToolTest::previewDrawsSampledContent()
{
    QImage canvas = makeLayer();
    const QImage source = makeSource();
    CloneTool tool; readyTool(tool);
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
    VpController controller; setupClone(controller); auto &doc=controller.document();
    const QImage before=doc.paintLayer(); QVERIFY(controller.pickCloneSource({20,20}));
    QVERIFY(controller.beginClone({60,60})); controller.moveClone({80,60}); controller.endClone();
    const QImage first=doc.paintLayer();
    QCOMPARE(first.pixelColor(60,60),QColor(Qt::red)); QCOMPARE(alphaAt(first,80,60),255);
    QVERIFY(controller.beginClone({120,80})); controller.moveClone({150,80}); controller.endClone();
    const QImage second=doc.paintLayer();
    controller.undo(); QCOMPARE(doc.paintLayer(),first);
    controller.undo(); QCOMPARE(doc.paintLayer(),before); QVERIFY(!doc.canUndo());
    controller.redo(); QCOMPARE(doc.paintLayer(),first);
    controller.redo(); QCOMPARE(doc.paintLayer(),second); QVERIFY(!doc.canRedo());
}

void CloneToolTest::targetPlaneTracksHoverAndLocksDuringStroke()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    VpController controller;
    QImage background(280,220,QImage::Format_ARGB32_Premultiplied);background.fill(Qt::white);
    controller.document().setImage(background);
    for(int x : {20,100}) {
        const PerspectiveQuad::Corners corners{{QPointF(x,20),QPointF(x+140,20),
                                                QPointF(x+140,180),QPointF(x,180)}};
        controller.document().appendPlane(PerspectivePlane(corners,corners));
    }
    const auto tool = VpTools::CloneStamp;
    {
        controller.setTool(tool);
        const bool undoBefore=controller.canUndo();
        check(controller.paintTargetPlane(QPointF(50,100))==0,"hover resolves lower non-overlapping plane");
        check(controller.paintTargetPlane(QPointF(120,100))==1,"overlap highlights topmost target plane");
        check(controller.paintTargetPlane(QPointF(5,5))==-1,"canvas fallback highlights no plane");
        check(controller.document().selectedPlane()==-1 && controller.canUndo()==undoBefore,
              "hover target does not select plane or add history");
        if(tool==VpTools::CloneStamp) check(controller.pickCloneSource(QPointF(50,40)),"clone source picks first plane");
        controller.pointerPress(QPointF(50,100),1,Qt::NoModifier);
        controller.pointerMove(QPointF(220,100),Qt::NoModifier);
        check(controller.paintTargetPlane(QPointF(220,100))==0,"stroke locks highlight to actual start plane");
        controller.pointerRelease(QPointF(220,100),Qt::NoModifier);
        check(controller.paintTargetPlane(QPointF(220,100))==1,"stroke release resumes hover plane");
        controller.pointerPress(QPointF(5,5),1,Qt::NoModifier);
        controller.pointerMove(QPointF(120,100),Qt::NoModifier);
        check(controller.paintTargetPlane(QPointF(120,100))==-1,"fallback stroke does not highlight newly crossed plane");
        controller.pointerRelease(QPointF(120,100),Qt::NoModifier);
    }

}

void CloneToolTest::sourceLifecycleAndFailedPick()
{
    QImage layer=makeLayer(); const QImage source=makeSource(); CloneTool tool; readyTool(tool);
    QVERIFY(tool.move(layer,{100,60}).isEmpty()); QVERIFY(tool.end(&layer).isEmpty());
    QVERIFY(tool.pickSource({},layer.size(),{60,45})); tool.setAligned(true);
    tool.begin(layer,source,{},layer.size(),{100,60}); tool.end();
    tool.hover({},layer.size(),{120,60}); QCOMPARE(tool.marker(),QPointF(80,45));
    QVERIFY(tool.pickSource({makeTestPlane()},layer.size(),{70,50})); QCOMPARE(tool.marker(),QPointF(70,50));
    tool.hover({},layer.size(),{150,60}); QCOMPARE(tool.marker(),QPointF(70,50));
    QVERIFY(!tool.pickSource({},layer.size(),{std::numeric_limits<qreal>::quiet_NaN(),10}));
    QVERIFY(tool.hasSource()); QCOMPARE(tool.marker(),QPointF(70,50));
    tool.hover({},layer.size(),{150,60}); QCOMPARE(tool.marker(),QPointF(70,50));
    tool.begin(layer,source,{},layer.size(),{150,60});
    QCOMPARE(layer.pixelColor(150,60),source.pixelColor(70,50));
    tool.resetSource(); QVERIFY(!tool.hasSource()); QVERIFY(!tool.drawing()); QCOMPARE(tool.marker(),QPointF());
    const QImage before=layer; QVERIFY(tool.move(layer,{180,60}).isEmpty()); tool.end(&layer); QCOMPARE(layer,before);
    QVERIFY(tool.previewBounds({},layer.size(),{100,60}).isEmpty());
    QImage preview=makeLayer(); const QImage empty=preview;
    { QPainter painter(&preview); tool.renderPreview(painter,source,{},layer.size(),{100,60}); }
    QCOMPARE(preview,empty);
    QVERIFY(tool.pickSource({},layer.size(),{60,45}));
    QVERIFY(!tool.begin(layer,source,{},layer.size(),{100,90}).isEmpty());
}

void CloneToolTest::alignedModeChanges_data()
{
    QTest::addColumn<bool>("initiallyAligned"); QTest::addColumn<bool>("duringStroke");
    for(bool aligned : {false,true}) for(bool during : {false,true})
        QTest::newRow(qPrintable(QString("aligned-%1-during-%2").arg(aligned).arg(during))) << aligned << during;
}

void CloneToolTest::alignedModeChanges()
{
    QFETCH(bool,initiallyAligned); QFETCH(bool,duringStroke);
    QImage layer=makeLayer(); const QImage source=makeSource(); CloneTool tool; readyTool(tool);
    tool.setAligned(initiallyAligned); QVERIFY(tool.pickSource({},layer.size(),{60,45}));
    tool.begin(layer,source,{},layer.size(),{100,60}); tool.hover({},layer.size(),{120,60});
    if(duringStroke) {
        tool.setAligned(!initiallyAligned); tool.move(layer,{120,60}); tool.hover({},layer.size(),{120,60});
        QCOMPARE(tool.marker(),QPointF(80,45)); // Switching must not re-anchor the current stroke.
    }
    tool.end();
    if(!duringStroke) tool.setAligned(!initiallyAligned);
    const bool keepsOffset=duringStroke && !initiallyAligned;
    tool.hover({},layer.size(),{140,60});
    QCOMPARE(tool.marker(),keepsOffset ? QPointF(100,45) : QPointF(60,45));
    tool.begin(layer,source,{},layer.size(),{140,60});
    QCOMPARE(rgbAt(layer,140,60),rgbAt(source,keepsOffset ? 100 : 60,45));
}

void CloneToolTest::differentPerspectiveMappingsKeepSnapshots()
{
    const PerspectiveQuad::Corners domain{{{0,0},{60,0},{60,60},{0,60}}};
    const QTransform sourceTransform(1,0,.001, .1,1,0, 5,5,1);
    const QTransform targetTransform(1.1,.1,.002, .15,1,.001, 100,25,1);
    auto sourceCorners=domain,targetCorners=domain;
    for(auto &p:sourceCorners) p=sourceTransform.map(p);
    for(auto &p:targetCorners) p=targetTransform.map(p);
    QVector<PerspectivePlane> planes{PerspectivePlane(sourceCorners,domain),PerspectivePlane(targetCorners,domain)};
    const QPointF sourcePoint=sourceTransform.map(QPointF(20,20));
    const QPointF targetPoint=targetTransform.map(QPointF(30,30));
    QImage source=makeLayer();
    for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x) source.setPixelColor(x,y,QColor(x,y,80));
    QImage actual=makeLayer(),expected=makeLayer(); CloneTool tool,reference; readyTool(tool); readyTool(reference);
    tool.setHealMode(0); reference.setHealMode(0);
    QVERIFY(tool.pickSource(planes,source.size(),sourcePoint)); QVERIFY(reference.pickSource(planes,source.size(),sourcePoint));
    QVERIFY(!tool.begin(actual,source,planes,source.size(),targetPoint).isEmpty());
    reference.begin(expected,source,planes,source.size(),targetPoint);
    const QPoint pixel=targetPoint.toPoint();
    const QPointF targetSurface=targetTransform.inverted().map(QPointF(pixel)+QPointF(.5,.5));
    const QPointF sampled=sourceTransform.map(targetSurface+QPointF(-10,-10));
    const QColor center=actual.pixelColor(pixel);
    QVERIFY(qAbs(center.red()-(sampled.x()-.5))<=1);
    QVERIFY(qAbs(center.green()-(sampled.y()-.5))<=1); QCOMPARE(center.alpha(),255);
    planes[0].quad().setCanvasCorner(2,{80,80}); planes.clear();
    const QPointF finish=targetTransform.map(QPointF(45,35));
    tool.move(actual,finish); reference.move(expected,finish); QCOMPARE(actual,expected);
    tool.hover(planes,source.size(),finish);
    QVERIFY(QLineF(tool.marker(),sourceTransform.map(QPointF(35,25))).length()<1e-6);
    QCOMPARE(tool.previewBounds(planes,source.size(),finish),reference.previewBounds({},source.size(),finish));
}

void CloneToolTest::sourceSnapshotAndNextStrokeSampling()
{
    QImage source=makeSource(),layer=makeLayer(); CloneTool tool; readyTool(tool); tool.setHealMode(0);
    QVERIFY(tool.pickSource({},layer.size(),{60,45}));
    tool.begin(layer,source,{},layer.size(),{100,60}); source.fill(Qt::blue);
    tool.move(layer,{110,60}); QCOMPARE(layer.pixelColor(110,60),QColor(Qt::red));
    tool.end(); tool.begin(layer,source,{},layer.size(),{150,60});
    QCOMPARE(layer.pixelColor(150,60),QColor(Qt::blue));
    VpController controller; setupClone(controller); auto &doc=controller.document();
    // The first destination lands on the source of later dabs. It must not feed back into this stroke.
    QVERIFY(controller.pickCloneSource({20,20})); controller.beginClone({30,20}); controller.moveClone({60,20}); controller.endClone();
    QCOMPARE(doc.paintLayer().pixelColor(40,20),QColor(Qt::red));
    QCOMPARE(doc.paintLayer().pixelColor(60,20),QColor(Qt::white));
    QVERIFY(controller.pickCloneSource({60,20})); controller.beginClone({120,80}); controller.endClone();
    QCOMPARE(doc.paintLayer().pixelColor(120,80),QColor(Qt::white));
    QVERIFY(controller.pickCloneSource({40,20})); controller.beginClone({150,80}); controller.endClone();
    QCOMPARE(doc.paintLayer().pixelColor(150,80),QColor(Qt::red));
}

void CloneToolTest::transparentBilinearSampling_data()
{
    QTest::addColumn<qreal>("fraction"); QTest::addColumn<int>("opacity"); QTest::addColumn<int>("sourceAlpha");
    QTest::newRow("opaque-center") << qreal(0) << 100 << 255;
    QTest::newRow("transparent-edge") << qreal(.5) << 100 << 255;
    QTest::newRow("edge-with-opacity") << qreal(.5) << 50 << 255;
    QTest::newRow("source-alpha-and-opacity") << qreal(0) << 50 << 128;
    QTest::newRow("fully-transparent") << qreal(0) << 100 << 0;
}

void CloneToolTest::transparentBilinearSampling()
{
    QFETCH(qreal,fraction); QFETCH(int,opacity); QFETCH(int,sourceAlpha);
    QImage source=makeLayer(); source.setPixelColor(20,20,QColor(255,0,0,sourceAlpha)); const QImage original=source;
    QImage layer=makeLayer(); CloneTool tool; readyTool(tool); tool.setHealMode(0); tool.setOpacity(opacity);
    QVERIFY(tool.pickSource({},source.size(),{20.5+fraction,20.5}));
    const QRect dirty=tool.begin(layer,source,{},layer.size(),{100.5,60.5}); tool.end(&layer);
    QVERIFY(qAbs(alphaAt(layer,100,60)-qRound(sourceAlpha*(1-fraction)*opacity/100))<=1);
    if(sourceAlpha) QCOMPARE(layer.pixelColor(100,60).red(),255); // Premultiplied interpolation does not darken the hue.
    else { QVERIFY(dirty.isEmpty()); QCOMPARE(layer,makeLayer()); }
    QCOMPARE(source,original);
}

void CloneToolTest::clipsSourceAndDestinationBounds()
{
    QImage source=makeSource(); source.fill(Qt::red); const QImage original=source;
    for(const QPointF pick : {QPointF(0,60),QPointF(199,60),QPointF(-30,60)}) {
        QImage layer=makeLayer(); const QImage before=layer; CloneTool tool; readyTool(tool); tool.setHealMode(0);
        QVERIFY(tool.pickSource({},source.size(),pick)); const QRect dirty=tool.begin(layer,source,{},layer.size(),{100,60});
        if(pick.x()<0) { QVERIFY(dirty.isEmpty()); QCOMPARE(layer,before); }
        else { QVERIFY(layer!=before); QVERIFY(changesInside(before,layer,dirty));
               QVERIFY(alphaAt(layer,97,60)==0 || alphaAt(layer,103,60)==0); }
    }
    for(const QPointF target : {QPointF(0,0),QPointF(199,119),QPointF(-2,60),QPointF(201,60)}) {
        QImage layer=makeLayer(); const QImage before=layer; CloneTool tool; readyTool(tool); tool.setHealMode(0);
        tool.pickSource({},source.size(),{60,45});
        const QRect dirty=tool.begin(layer,source,{},layer.size(),target);
        QVERIFY(layer!=before); QVERIFY(changesInside(before,layer,dirty));
    }
    QCOMPARE(source,original);
}

void CloneToolTest::continuousStrokeAndRelease_data()
{
    QTest::addColumn<QPointF>("from"); QTest::addColumn<QPointF>("to");
    QTest::newRow("click") << QPointF(60,60) << QPointF(60,60);
    QTest::newRow("horizontal") << QPointF(60,60) << QPointF(140,60);
    QTest::newRow("vertical") << QPointF(60,30) << QPointF(60,90);
    QTest::newRow("diagonal") << QPointF(60,30) << QPointF(140,90);
    QTest::newRow("reverse") << QPointF(140,90) << QPointF(60,30);
}

void CloneToolTest::continuousStrokeAndRelease()
{
    QFETCH(QPointF,from); QFETCH(QPointF,to);
    VpController controller; setupClone(controller); controller.pickCloneSource(from);
    controller.pointerPress(from,1,Qt::NoModifier);
    controller.pointerMove(from+(to-from)*.4,Qt::NoModifier);
    const QImage middle=controller.document().paintLayer(); controller.moveClone(from+(to-from)*.4);
    QCOMPARE(controller.document().paintLayer(),middle);
    controller.pointerRelease(to,Qt::NoModifier); QVERIFY(!controller.cloneDrawing());
    const QImage painted=controller.document().paintLayer();
    for(int i=0;i<=100;++i) { const QPoint p=(from+(to-from)*(i/100.0)).toPoint(); QVERIFY(alphaAt(painted,p.x(),p.y())>0); }
    controller.undo(); QVERIFY(!controller.document().hasPaintContent()); QVERIFY(!controller.document().canUndo());
    controller.redo(); QCOMPARE(controller.document().paintLayer(),painted);
}

void CloneToolTest::invalidMovesRecover_data()
{
    QTest::addColumn<QPointF>("point");
    const qreal nan=std::numeric_limits<qreal>::quiet_NaN(),inf=std::numeric_limits<qreal>::infinity();
    QTest::newRow("nan") << QPointF(nan,40); QTest::newRow("infinity") << QPointF(40,inf);
    QTest::newRow("negative-infinity") << QPointF(-inf,40); QTest::newRow("long-segment") << QPointF(1e8,1e8);
}

void CloneToolTest::invalidMovesRecover()
{
    QFETCH(QPointF,point);
    QImage layer=makeLayer(),expected=makeLayer(); const QImage source=makeSource(); CloneTool tool,reference;
    for(CloneTool *t : {&tool,&reference}) { readyTool(*t); t->setHealMode(0); t->pickSource({},layer.size(),{60,45}); }
    tool.begin(layer,source,{},layer.size(),{100,60}); reference.begin(expected,source,{},layer.size(),{100,60});
    const QImage before=layer; QVERIFY(tool.move(layer,point).isEmpty()); QCOMPARE(layer,before); QVERIFY(tool.drawing());
    tool.move(layer,{120,60}); reference.move(expected,{120,60}); QCOMPARE(layer,expected);
}

void CloneToolTest::rejectsProjectionPoleAndRecovers()
{
    const PerspectiveQuad::Corners surface{{{0,0},{100,0},{100,100},{0,100}}};
    const QTransform transform(1,0,.005, 0,1,0, 10,10,1);
    auto canvas=surface; for(auto &p:canvas) p=transform.map(p); const PerspectivePlane plane(canvas,surface);
    QImage source=makeSource(),layer=makeLayer(),expected=makeLayer(); CloneTool tool,reference;
    const QPointF start=transform.map(QPointF(40,40)),finish=transform.map(QPointF(50,40));
    for(CloneTool *t : {&tool,&reference}) { readyTool(*t); t->setHealMode(0); QVERIFY(t->pickSource({plane},source.size(),start)); }
    tool.begin(layer,source,{plane},layer.size(),start); reference.begin(expected,source,{plane},layer.size(),start);
    const QImage before=layer; QVERIFY(tool.move(layer,{200,40}).isEmpty());
    QVERIFY(tool.previewBounds({},layer.size(),{200,40}).isEmpty()); QCOMPARE(layer,before);
    tool.move(layer,finish); reference.move(expected,finish); QCOMPARE(layer,expected);
}

void CloneToolTest::previewAndCoordinateCacheStayConsistent()
{
    QImage source=makeSource(); CloneTool reused; readyTool(reused); reused.setHealMode(0); reused.setDiameter(25);
    for(int stage=0;stage<6;++stage) {
        auto plane=makeTestPlane();
        if(stage>=3) plane.quad().setCanvasCorner(2,{145,95});
        const QVector<PerspectivePlane> planes=stage>=2 ? QVector<PerspectivePlane>{plane} : QVector<PerspectivePlane>{};
        if(stage==1) source.fill(Qt::blue);
        if(stage>=2) for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x)
            source.setPixelColor(x,y,QColor((x+stage*17)%256,(2*y+stage*7)%256,80));
        if(stage==5) source=source.scaled(180,100);
        const QPointF pick(stage>=4 ? 45 : 60,40);
        QVERIFY(reused.pickSource(planes,source.size(),pick));
        CloneTool fresh; readyTool(fresh); fresh.setHealMode(0); fresh.setDiameter(25); fresh.pickSource(planes,source.size(),pick);
        for(qreal x : {31.5,32.5,63.5,64.5,100.5}) {
            QImage preview=makeLayer(),expected=makeLayer(); const QPointF target(x,60.5);
            { QPainter painter(&preview); reused.renderPreview(painter,source,planes,source.size(),target); }
            { QPainter painter(&expected); fresh.renderPreview(painter,source,planes,source.size(),target); }
            QCOMPARE(preview,expected); QVERIFY(!reused.drawing()); QCOMPARE(reused.marker(),pick);
            QVERIFY(changesInside(makeLayer(),preview,reused.previewBounds(planes,source.size(),target).toAlignedRect()));
        }
        QImage actual=makeLayer(),expected=makeLayer();
        reused.begin(actual,source,planes,source.size(),{100,60}); fresh.begin(expected,source,planes,source.size(),{100,60});
        reused.move(actual,{120,60}); fresh.move(expected,{120,60}); QCOMPARE(actual,expected);
        reused.end(); fresh.end();
    }
}

void CloneToolTest::hardnessAndOpacityControlCoverage()
{
    QImage source=makeSource(); source.fill(Qt::red); int edges[3]; int i=0;
    for(int hardness : {0,50,100}) {
        QImage layer=makeLayer(); CloneTool tool; readyTool(tool); tool.setHealMode(0);
        tool.setDiameter(60); tool.setHardness(hardness); tool.setOpacity(40);
        tool.pickSource({},source.size(),{60.5,45.5}); tool.begin(layer,source,{},layer.size(),{100.5,60.5});
        QVERIFY(qAbs(alphaAt(layer,100,60)-102)<=1); edges[i++]=alphaAt(layer,125,60);
        QCOMPARE(alphaAt(layer,135,60),0); QVERIFY(alphaAt(layer,115,60)>=alphaAt(layer,125,60));
    }
    QVERIFY(edges[0]<edges[1]); QVERIFY(edges[1]<edges[2]); QCOMPARE(edges[2],102);
}

void CloneToolTest::changingParametersPreservesHealingReplay_data()
{
    QTest::addColumn<int>("mode"); for(int mode=0;mode<3;++mode) QTest::newRow(qPrintable(QString::number(mode))) << mode;
}

void CloneToolTest::changingParametersPreservesHealingReplay()
{
    QFETCH(int,mode);
    QImage source=makeSource(),actual=makeLayer(),expected=makeLayer(); CloneTool tool; tool.setHealMode(mode);
    tool.pickSource({},source.size(),{60,45});
    const QPointF points[]={{100,60},{100.5,60},{101,60}};
    const int diameters[]={20,30,10},hardnesses[]={100,40,0},opacities[]={35,70,50};
    for(int i=0;i<3;++i) {
        tool.setDiameter(diameters[i]); tool.setHardness(hardnesses[i]); tool.setOpacity(opacities[i]);
        if(i==0) tool.begin(actual,source,{},actual.size(),points[i]); else tool.move(actual,points[i]);
        CloneTool dab; dab.setHealMode(mode); dab.setDiameter(diameters[i]); dab.setHardness(hardnesses[i]); dab.setOpacity(opacities[i]);
        dab.pickSource({},source.size(),QPointF(60,45)+(points[i]-points[0]));
        dab.begin(expected,source,{},expected.size(),points[i]); dab.end(&expected);
    }
    tool.end(&actual); QCOMPARE(actual,expected); QVERIFY(!tool.drawing());
    QCOMPARE(tool.diameter(),10); QCOMPARE(tool.hardness(),0); QCOMPARE(tool.opacity(),50);
    const QImage before=actual; tool.end(&actual); tool.move(actual,{150,90}); QCOMPARE(actual,before);
}

void CloneToolTest::prepareRunsBeforeWriting()
{
    QImage source=makeSource(),layer=makeLayer(); layer.fill(QColor(40,80,120,90)); const QImage before=layer;
    QHash<int,QRgb> saved; QRect prepared; int calls=0;
    const auto prepare=[&](const QRect &rect) {
        ++calls; prepared=prepared.united(rect); const QRect clipped=rect.intersected(layer.rect());
        for(int y=clipped.top();y<=clipped.bottom();++y) for(int x=clipped.left();x<=clipped.right();++x) {
            const int key=y*layer.width()+x; if(!saved.contains(key)) saved.insert(key,layer.pixel(x,y));
        }
    };
    CloneTool tool; readyTool(tool); tool.setHealMode(0); tool.pickSource({},source.size(),{60,45});
    QRect dirty=tool.begin(layer,source,{},layer.size(),{100,60},prepare);
    dirty=dirty.united(tool.move(layer,{140,80},prepare)); tool.end();
    QVERIFY(calls>1); QVERIFY(layer!=before); QCOMPARE(dirty,prepared); QVERIFY(changesInside(before,layer,dirty));
    QImage restored=layer;
    for(auto it=saved.cbegin();it!=saved.cend();++it) restored.setPixel(it.key()%layer.width(),it.key()/layer.width(),it.value());
    QCOMPARE(restored,before);
}

void CloneToolTest::interruptionsAndHistory_data()
{
    QTest::addColumn<int>("interrupt");
    QTest::newRow("switch") << 0; QTest::newRow("cancel") << 1; QTest::newRow("undo") << 2;
}

void CloneToolTest::interruptionsAndHistory()
{
    QFETCH(int,interrupt);
    VpController controller; setupClone(controller); auto &doc=controller.document(); const QImage before=doc.paintLayer();
    controller.pickCloneSource({20,20}); controller.beginClone({60,60}); controller.moveClone({80,60}); const QImage painted=doc.paintLayer();
    if(interrupt==0) controller.setTool(VpTools::Hand); else if(interrupt==1) controller.cancelInteraction(); else controller.undo();
    QVERIFY(!controller.cloneDrawing()); QVERIFY(!doc.editActive()); controller.moveClone({100,80}); controller.endClone();
    if(interrupt==2) { QCOMPARE(doc.paintLayer(),before); QVERIFY(!doc.canUndo()); controller.redo(); }
    QCOMPARE(doc.paintLayer(),painted); controller.undo(); QCOMPARE(doc.paintLayer(),before); QVERIFY(!doc.canUndo());
    controller.redo(); QCOMPARE(doc.paintLayer(),painted);
    controller.setTool(VpTools::CloneStamp); QVERIFY(controller.pickCloneSource({20,20}));
    QVERIFY(controller.beginClone({140,90})); controller.endClone(); controller.undo(); QCOMPARE(doc.paintLayer(),painted);
}

void CloneToolTest::emptyStrokePreservesRedo()
{
    VpController controller; setupClone(controller); auto &doc=controller.document();
    controller.pickCloneSource({20,20}); controller.beginClone({60,60}); controller.endClone(); const QImage painted=doc.paintLayer();
    controller.undo(); const QImage before=doc.paintLayer();
    controller.moveClone({70,60}); controller.endClone();
    controller.pickCloneSource({-100,-100}); controller.beginClone({60,60}); controller.endClone();
    controller.pickCloneSource({20,20}); controller.beginClone({-100,-100}); controller.endClone();
    QCOMPARE(doc.paintLayer(),before); QVERIFY(doc.canRedo()); QVERIFY(!doc.canUndo()); controller.redo(); QCOMPARE(doc.paintLayer(),painted);
}

void CloneToolTest::pasteFinishesCloneStroke()
{
    VpController controller; setupClone(controller); auto &doc=controller.document(); const QImage before=doc.paintLayer();
    controller.pickCloneSource({20,20}); QVERIFY(controller.beginClone({60,60})); const QImage painted=doc.paintLayer();
    QImage image(20,20,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::blue);
    QGuiApplication::clipboard()->setImage(image); controller.pasteSelection();
    QVERIFY(!controller.cloneDrawing()); controller.moveClone({80,60}); controller.endClone(); QCOMPARE(doc.paintLayer(),painted);
    QCOMPARE(doc.floatingSelections().size(),1); controller.undo(); QVERIFY(doc.floatingSelections().isEmpty()); QCOMPARE(doc.paintLayer(),painted);
    controller.undo(); QCOMPARE(doc.paintLayer(),before); QVERIFY(!doc.canUndo()); controller.redo(); QCOMPARE(doc.paintLayer(),painted);
    controller.redo(); QCOMPARE(doc.floatingSelections().size(),1); QCOMPARE(doc.floatingSelection(0).bitmap,image);
}

QTEST_MAIN(CloneToolTest)
#include "tst_clonetool.moc"
