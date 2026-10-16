#include <QGuiApplication>
#include <QClipboard>
#include <QHash>
#include <QPainter>
#include <QtTest>
#include <limits>

#include "canvas/vpcontroller.h"
#include "testhelpers.h"
#include "tools/brushtool.h"

namespace {
void setupBrush(VpController &controller)
{
    QImage background(200,120,QImage::Format_ARGB32); background.fill(Qt::white);
    controller.document().setImage(background);
    controller.document().appendPlane(makeTestPlane());
    controller.document().resetHistory();
    controller.setTool(VpTools::Brush);
    controller.setBrushDiameter(12); controller.setBrushHardness(100);
    controller.setBrushOpacity(100); controller.setBrushHealMode(0);
}

void configureBrush(BrushTool &tool)
{
    tool.setDiameter(12); tool.setHardness(100); tool.setOpacity(100);
}

bool changesInside(const QImage &before, const QImage &after, const QRect &dirty)
{
    for(int y=0;y<after.height();++y) for(int x=0;x<after.width();++x)
        if(before.pixel(x,y)!=after.pixel(x,y) && !dirty.contains(x,y)) return false;
    return true;
}
}

class BrushToolTest : public QObject
{
    Q_OBJECT

private slots:
    void targetPlaneTracksHoverAndLocksDuringStroke();
    void clampsParameters();
    void refusesToStart();
    void paintsOnPlane();
    void paintsAtCanvasScale();
    void appliesOpacity();
    void previewDrawsDab();
    void undoRemovesWholeStroke();
    void lifecycleDoesNotLeakBetweenStrokes();
    void releasePaintsFinalPosition_data();
    void releasePaintsFinalPosition();
    void continuousStroke_data();
    void continuousStroke();
    void hardnessControlsRadialFalloff();
    void locksActualMappingAndKeepsGeometrySnapshot();
    void clipsAtCanvasEdges_data();
    void clipsAtCanvasEdges();
    void invalidMovesPreservePixelsAndRecover_data();
    void invalidMovesPreservePixelsAndRecover();
    void rejectsProjectionPoleAndRecovers();
    void prepareRunsBeforeWritingAndDirtyCoversPixels();
    void interruptedStrokeCommitsOnce_data();
    void interruptedStrokeCommitsOnce();
    void emptyStrokePreservesRedo();
    void pasteFinishesStroke();
    void previewBoundsCoverPixelsAndLocalTextures_data();
    void previewBoundsCoverPixelsAndLocalTextures();
    void changingParametersPreservesHealingReplay_data();
    void changingParametersPreservesHealingReplay();

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
    VpController controller; setupBrush(controller); auto &document=controller.document();
    const QImage before=document.paintLayer();
    QVERIFY(controller.beginBrush({30,30})); controller.moveBrush({60,30}); controller.endBrush();
    const QImage first=document.paintLayer();
    QCOMPARE(alphaAt(first,30,30),255); QCOMPARE(alphaAt(first,50,30),255);
    QVERIFY(document.canUndo());
    QVERIFY(controller.beginBrush({100,60})); controller.moveBrush({140,60}); controller.endBrush();
    const QImage second=document.paintLayer();
    QCOMPARE(alphaAt(second,100,60),255); QCOMPARE(alphaAt(second,140,60),255);
    controller.undo(); QCOMPARE(document.paintLayer(),first);
    controller.undo(); QCOMPARE(document.paintLayer(),before); QVERIFY(!document.canUndo());
    controller.redo(); QCOMPARE(document.paintLayer(),first);
    controller.redo(); QCOMPARE(document.paintLayer(),second); QVERIFY(!document.canRedo());
}

void BrushToolTest::targetPlaneTracksHoverAndLocksDuringStroke()
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
    const auto tool = VpTools::Brush;
    {
        controller.setTool(tool);
        const bool undoBefore=controller.canUndo();
        check(controller.paintTargetPlane(QPointF(50,100))==0,"hover resolves lower non-overlapping plane");
        check(controller.paintTargetPlane(QPointF(120,100))==1,"overlap highlights topmost target plane");
        check(controller.paintTargetPlane(QPointF(5,5))==-1,"canvas fallback highlights no plane");
        check(controller.document().selectedPlane()==-1 && controller.canUndo()==undoBefore,
              "hover target does not select plane or add history");
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

void BrushToolTest::lifecycleDoesNotLeakBetweenStrokes()
{
    QImage layer=makeLayer(); const QImage before=layer;
    BrushTool tool; configureBrush(tool); tool.setOpacity(50);
    QVERIFY(tool.move(layer,{60,45}).isEmpty()); QVERIFY(tool.end(&layer).isEmpty());
    QCOMPARE(layer,before); QVERIFY(!tool.drawing());
    QVERIFY(!tool.begin(layer,{},layer.size(),{60,45}).isEmpty());
    const QImage dab=layer;
    QVERIFY(tool.move(layer,{60,45}).isEmpty()); QCOMPARE(layer,dab);
    tool.end(&layer); QVERIFY(!tool.drawing());
    QVERIFY(tool.move(layer,{160,90}).isEmpty()); QVERIFY(tool.end(&layer).isEmpty());
    QCOMPARE(layer,dab);
    QVERIFY(!tool.begin(layer,{},layer.size(),{160,90}).isEmpty()); tool.end(&layer);
    QCOMPARE(alphaAt(layer,110,65),0); QCOMPARE(alphaAt(layer,60,45),alphaAt(dab,60,45));
    QVERIFY(alphaAt(layer,160,90)>0);
}

void BrushToolTest::releasePaintsFinalPosition_data()
{
    QTest::addColumn<bool>("moved");
    QTest::newRow("click") << false; QTest::newRow("release-after-move") << true;
}

void BrushToolTest::releasePaintsFinalPosition()
{
    QFETCH(bool,moved);
    VpController controller; setupBrush(controller);
    controller.pointerPress({30,30},1,Qt::NoModifier);
    if(moved) controller.pointerMove({55,30},Qt::NoModifier);
    controller.pointerRelease(moved ? QPointF(90,30) : QPointF(30,30),Qt::NoModifier);
    QVERIFY(!controller.brushDrawing());
    const auto &layer=controller.document().paintLayer();
    QCOMPARE(alphaAt(layer,30,30),255);
    if(moved) for(int x=30;x<=90;++x) QVERIFY2(alphaAt(layer,x,30)>0,"release must complete the last segment");
    controller.undo(); QVERIFY(!controller.document().hasPaintContent());
    QVERIFY(!controller.document().canUndo());
}

void BrushToolTest::continuousStroke_data()
{
    QTest::addColumn<QPointF>("from"); QTest::addColumn<QPointF>("to");
    QTest::newRow("horizontal") << QPointF(20,30) << QPointF(180,30);
    QTest::newRow("vertical") << QPointF(90,15) << QPointF(90,105);
    QTest::newRow("diagonal") << QPointF(20,20) << QPointF(180,100);
    QTest::newRow("reverse") << QPointF(180,100) << QPointF(20,20);
}

void BrushToolTest::continuousStroke()
{
    QFETCH(QPointF,from); QFETCH(QPointF,to);
    QImage layer=makeLayer(); BrushTool tool; configureBrush(tool); tool.setDiameter(4);
    tool.begin(layer,{},layer.size(),from); QVERIFY(!tool.move(layer,to).isEmpty());
    for(int i=0;i<=200;++i) {
        const QPoint point=(from+(to-from)*(i/200.0)).toPoint();
        QVERIFY2(alphaAt(layer,point.x(),point.y())>0,"interpolated centerline has no gaps");
    }
    const QImage before=layer; QVERIFY(tool.move(layer,to).isEmpty()); QCOMPARE(layer,before);
}

void BrushToolTest::hardnessControlsRadialFalloff()
{
    int center[3],middle[3],edge[3]; int i=0;
    for(int hardness : {0,50,100}) {
        QImage layer=makeLayer(); BrushTool tool; tool.setDiameter(60); tool.setHardness(hardness);
        tool.setColor(Qt::blue); tool.begin(layer,{},layer.size(),{100.5,60.5}); tool.end();
        center[i]=alphaAt(layer,100,60); middle[i]=alphaAt(layer,115,60); edge[i]=alphaAt(layer,125,60);
        QVERIFY(center[i]>=250); QCOMPARE(alphaAt(layer,135,60),0);
        QVERIFY(center[i]>=middle[i]); QVERIFY(middle[i]>=edge[i]); ++i;
    }
    QVERIFY(middle[0]<middle[1]); QVERIFY(edge[0]<edge[1]); QVERIFY(edge[1]<edge[2]);
    QCOMPARE(center[2],255); QCOMPARE(edge[2],255);
}

void BrushToolTest::locksActualMappingAndKeepsGeometrySnapshot()
{
    const PerspectiveQuad::Corners canvas{{{10,10},{110,10},{110,110},{10,110}}};
    const PerspectiveQuad::Corners surface{{{0,0},{50,0},{50,50},{0,50}}};
    QVector<PerspectivePlane> planes{PerspectivePlane(canvas,canvas),PerspectivePlane(canvas,surface)};
    QImage actual=makeLayer(),expected=makeLayer();
    BrushTool tool,reference; configureBrush(tool); configureBrush(reference);
    tool.begin(actual,planes,actual.size(),{60,60});
    reference.begin(expected,{planes[1]},expected.size(),{60,60});
    QCOMPARE(actual,expected); QVERIFY(alphaAt(actual,70,60)>0); // The upper face doubles the diameter.
    planes[1].quad().setCanvasCorner(2,{140,115}); planes.clear();
    QVERIFY(!tool.move(actual,{150,60}).isEmpty()); reference.move(expected,{150,60});
    QCOMPARE(actual,expected); // Extrapolation beyond the old face retains the original mapping.
    const auto bounds=tool.previewBounds(planes,actual.size(),{150,60});
    QCOMPARE(bounds,reference.previewBounds({},actual.size(),{150,60}));
    QVERIFY(bounds.width()>20);
    tool.end(); reference.end();
    QCOMPARE(tool.previewBounds({},actual.size(),{150,60}).width(),qreal(12));
}

void BrushToolTest::clipsAtCanvasEdges_data()
{
    QTest::addColumn<QPointF>("point");
    for(const QPointF point : {QPointF(0,60),QPointF(199,60),QPointF(100,0),QPointF(100,119),
                              QPointF(0,0),QPointF(199,0),QPointF(0,119),QPointF(199,119),
                              QPointF(-3,60),QPointF(202,60)})
        QTest::newRow(qPrintable(QString("%1,%2").arg(point.x()).arg(point.y()))) << point;
}

void BrushToolTest::clipsAtCanvasEdges()
{
    QFETCH(QPointF,point);
    QImage layer=makeLayer(); layer.fill(QColor(20,30,40,80)); const QImage before=layer;
    BrushTool tool; configureBrush(tool); tool.setDiameter(20); tool.setColor(Qt::red);
    QRect dirty=tool.begin(layer,{},layer.size(),point);
    QVERIFY(layer!=before); QVERIFY(changesInside(before,layer,dirty));
    dirty=dirty.united(tool.move(layer,{100,60}));
    dirty=dirty.united(tool.move(layer,{-30,60})); tool.end();
    QVERIFY(changesInside(before,layer,dirty)); QCOMPARE(layer.size(),before.size());
    const int untouchedY=point.y()>=60 ? 10 : 110;
    QCOMPARE(layer.pixel(100,untouchedY),before.pixel(100,untouchedY));
}

void BrushToolTest::invalidMovesPreservePixelsAndRecover_data()
{
    QTest::addColumn<QPointF>("point");
    const qreal nan=std::numeric_limits<qreal>::quiet_NaN(),inf=std::numeric_limits<qreal>::infinity();
    QTest::newRow("nan-x") << QPointF(nan,40); QTest::newRow("nan-y") << QPointF(40,nan);
    QTest::newRow("infinity") << QPointF(inf,40); QTest::newRow("negative-infinity") << QPointF(40,-inf);
    QTest::newRow("too-many-dabs") << QPointF(1e8,1e8);
}

void BrushToolTest::invalidMovesPreservePixelsAndRecover()
{
    QFETCH(QPointF,point);
    QImage layer=makeLayer(),expected=makeLayer(); BrushTool tool,reference;
    configureBrush(tool); configureBrush(reference);
    tool.begin(layer,{},layer.size(),{40,40}); reference.begin(expected,{},expected.size(),{40,40});
    const QImage before=layer;
    QVERIFY(tool.move(layer,point).isEmpty()); QCOMPARE(layer,before); QVERIFY(tool.drawing());
    QVERIFY(!tool.move(layer,{60,40}).isEmpty()); reference.move(expected,{60,40});
    QCOMPARE(layer,expected); tool.end();
    if(!qIsFinite(point.x()) || !qIsFinite(point.y())) {
        QImage blank=makeLayer(); const QImage initial=blank;
        QVERIFY(tool.begin(blank,{},blank.size(),point).isEmpty());
        QVERIFY(!tool.drawing()); QCOMPARE(blank,initial);
    }
}

void BrushToolTest::rejectsProjectionPoleAndRecovers()
{
    const PerspectiveQuad::Corners surface{{{0,0},{100,0},{100,100},{0,100}}};
    const QTransform transform(1,0,.005, 0,1,0, 10,10,1);
    auto canvas=surface; for(auto &point:canvas) point=transform.map(point);
    const PerspectivePlane plane(canvas,surface);
    QImage layer=makeLayer(),expected=makeLayer(); BrushTool tool,reference;
    configureBrush(tool); configureBrush(reference);
    const QPointF start=transform.map(QPointF(40,40)),finish=transform.map(QPointF(50,40));
    QVERIFY(!tool.begin(layer,{plane},layer.size(),start).isEmpty());
    reference.begin(expected,{plane},expected.size(),start); const QImage before=layer;
    const QPointF pole(200,40); // The inverse transform has w=0 at canvas x=200.
    QVERIFY(tool.move(layer,pole).isEmpty()); QVERIFY(tool.previewBounds({},layer.size(),pole).isEmpty());
    QCOMPARE(layer,before); QVERIFY(!tool.move(layer,finish).isEmpty()); reference.move(expected,finish);
    QCOMPARE(layer,expected);
}

void BrushToolTest::prepareRunsBeforeWritingAndDirtyCoversPixels()
{
    QImage layer=makeLayer(); layer.fill(QColor(40,80,120,90)); const QImage before=layer;
    QHash<int,QRgb> saved; QRect prepared; int calls=0;
    const auto prepare=[&](const QRect &rect) {
        ++calls; prepared=prepared.united(rect);
        const QRect clipped=rect.intersected(layer.rect());
        for(int y=clipped.top();y<=clipped.bottom();++y) for(int x=clipped.left();x<=clipped.right();++x) {
            const int key=y*layer.width()+x;
            if(!saved.contains(key)) saved.insert(key,layer.pixel(x,y));
        }
    };
    BrushTool tool; configureBrush(tool); tool.setDiameter(30); tool.setHardness(40);
    QRect dirty=tool.begin(layer,{makeTestPlane()},layer.size(),{15,15},prepare);
    dirty=dirty.united(tool.move(layer,{140,80},prepare)); tool.end();
    QVERIFY(calls>1); QVERIFY(layer!=before); QCOMPARE(dirty,prepared);
    QVERIFY(changesInside(before,layer,dirty));
    QImage restored=layer;
    for(auto it=saved.cbegin();it!=saved.cend();++it) restored.setPixel(it.key()%layer.width(),it.key()/layer.width(),it.value());
    QCOMPARE(restored,before); // Capturing after a dab would fail to restore its original pixels.
}

void BrushToolTest::interruptedStrokeCommitsOnce_data()
{
    QTest::addColumn<int>("interrupt");
    QTest::newRow("tool-switch") << 0; QTest::newRow("cancel") << 1; QTest::newRow("undo") << 2;
}

void BrushToolTest::interruptedStrokeCommitsOnce()
{
    QFETCH(int,interrupt);
    VpController controller; setupBrush(controller); auto &doc=controller.document(); const QImage before=doc.paintLayer();
    QVERIFY(controller.beginBrush({30,30})); controller.moveBrush({60,30}); const QImage painted=doc.paintLayer();
    if(interrupt==0) controller.setTool(VpTools::Hand);
    else if(interrupt==1) controller.cancelInteraction();
    else controller.undo();
    QVERIFY(!controller.brushDrawing()); QVERIFY(!doc.editActive());
    controller.moveBrush({100,80}); controller.endBrush();
    if(interrupt==2) { QCOMPARE(doc.paintLayer(),before); QVERIFY(!doc.canUndo()); controller.redo(); }
    QCOMPARE(doc.paintLayer(),painted);
    controller.undo(); QCOMPARE(doc.paintLayer(),before); QVERIFY(!doc.canUndo());
    controller.redo(); QCOMPARE(doc.paintLayer(),painted);
    controller.setTool(VpTools::Brush); QVERIFY(controller.beginBrush({140,90})); controller.endBrush();
    controller.undo(); QCOMPARE(doc.paintLayer(),painted);
}

void BrushToolTest::emptyStrokePreservesRedo()
{
    VpController controller; setupBrush(controller); auto &doc=controller.document();
    controller.beginBrush({40,40}); controller.endBrush(); const QImage painted=doc.paintLayer();
    controller.undo(); const QImage before=doc.paintLayer(); QVERIFY(doc.canRedo());
    controller.moveBrush({70,40}); controller.endBrush();
    controller.beginBrush({-100,-100}); controller.endBrush();
    QCOMPARE(doc.paintLayer(),before); QVERIFY(!doc.canUndo()); QVERIFY(doc.canRedo());
    controller.redo(); QCOMPARE(doc.paintLayer(),painted);
}

void BrushToolTest::pasteFinishesStroke()
{
    VpController controller; setupBrush(controller); auto &doc=controller.document(); const QImage before=doc.paintLayer();
    QVERIFY(controller.beginBrush({50,50})); const QImage painted=doc.paintLayer();
    QImage image(20,20,QImage::Format_ARGB32_Premultiplied); image.fill(Qt::red);
    QGuiApplication::clipboard()->setImage(image); controller.pasteSelection();
    QVERIFY(!controller.brushDrawing()); controller.moveBrush({80,50}); controller.endBrush();
    QCOMPARE(doc.paintLayer(),painted); QCOMPARE(doc.floatingSelections().size(),1);
    controller.undo(); QCOMPARE(doc.paintLayer(),painted); QVERIFY(doc.floatingSelections().isEmpty());
    controller.undo(); QCOMPARE(doc.paintLayer(),before); QVERIFY(!doc.canUndo());
    controller.redo(); QCOMPARE(doc.paintLayer(),painted);
    controller.redo(); QCOMPARE(doc.floatingSelections().size(),1); QCOMPARE(doc.floatingSelection(0).bitmap,image);
}

void BrushToolTest::previewBoundsCoverPixelsAndLocalTextures_data()
{
    QTest::addColumn<bool>("perspective"); QTest::addColumn<qreal>("zoom");
    for(bool perspective : {false,true}) for(qreal zoom : {.5,1.0,2.0})
        QTest::newRow(qPrintable(QString("perspective-%1-zoom-%2").arg(perspective).arg(zoom))) << perspective << zoom;
}

void BrushToolTest::previewBoundsCoverPixelsAndLocalTextures()
{
    QFETCH(bool,perspective); QFETCH(qreal,zoom);
    auto plane=makeTestPlane();
    if(perspective) plane.quad().setCanvasCorner(2,{160,95});
    const QVector<PerspectivePlane> planes=perspective ? QVector<PerspectivePlane>{plane} : QVector<PerspectivePlane>{};
    const QPointF point(60,45); BrushTool tool; configureBrush(tool); tool.setDiameter(30); tool.setHardness(45);
    const QRectF bounds=tool.previewBounds(planes,{200,120},point); QVERIFY(!bounds.isEmpty());
    QImage actual=makeLayer(); tool.begin(actual,planes,actual.size(),point); tool.end();
    QImage preview=makeLayer(); { QPainter painter(&preview); tool.renderPreview(painter,planes,preview.size(),point); }
    QCOMPARE(preview,actual);
    QImage blank=makeLayer(); QVERIFY(changesInside(blank,preview,bounds.toAlignedRect().adjusted(-1,-1,1,1)));
    const QRect deviceBounds=QRectF(bounds.topLeft()*zoom,bounds.size()*zoom).toAlignedRect().adjusted(-2,-2,2,2);
    QImage local(deviceBounds.size(),QImage::Format_ARGB32); local.fill(Qt::transparent);
    { QPainter painter(&local); painter.translate(-deviceBounds.topLeft()); painter.scale(zoom,zoom);
      tool.renderPreview(painter,planes,preview.size(),point); }
    QImage full(QSize(400,240),QImage::Format_ARGB32); full.fill(Qt::transparent);
    { QPainter painter(&full); painter.scale(zoom,zoom); tool.renderPreview(painter,planes,preview.size(),point); }
    // Rasterizing a projective path in a translated device can round its edge
    // coverage differently. Compare premultiplied channels within 2/255.
    const QImage localPixels=local.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage fullPixels=full.copy(deviceBounds).convertToFormat(localPixels.format());
    QCOMPARE(localPixels.size(),fullPixels.size());
    int maximumError=0;
    for(int y=0;y<localPixels.height();++y) for(int x=0;x<localPixels.width();++x) {
        const QRgb a=localPixels.pixel(x,y),b=fullPixels.pixel(x,y);
        for(int shift : {0,8,16,24}) maximumError=qMax(maximumError,qAbs(int((a>>shift)&255)-int((b>>shift)&255)));
    }
    QVERIFY2(maximumError<=2,qPrintable(QString("local preview channel error: %1/255").arg(maximumError)));
    QVERIFY(qAlpha(local.pixel((point*zoom-deviceBounds.topLeft()).toPoint()))>0);
}

void BrushToolTest::changingParametersPreservesHealingReplay_data()
{
    QTest::addColumn<int>("mode"); for(int mode=0;mode<3;++mode) QTest::newRow(qPrintable(QString::number(mode))) << mode;
}

void BrushToolTest::changingParametersPreservesHealingReplay()
{
    QFETCH(int,mode);
    QImage actual=makeLayer(),expected=makeLayer(); QImage reference=makeLayer();
    for(int y=0;y<reference.height();++y) for(int x=0;x<reference.width();++x)
        reference.setPixelColor(x,y,QColor(40+x/2,80+y,150));
    BrushTool tool; tool.setHealMode(mode);
    const QPointF points[]={{40,40},{40.5,40},{41,40}};
    const int diameters[]={20,30,10},hardnesses[]={100,40,0},opacities[]={35,70,50};
    const QColor colors[]={Qt::red,Qt::blue,Qt::green};
    for(int i=0;i<3;++i) {
        tool.setDiameter(diameters[i]); tool.setHardness(hardnesses[i]); tool.setOpacity(opacities[i]); tool.setColor(colors[i]);
        if(i==0) tool.begin(actual,{},actual.size(),points[i],{},reference);
        else tool.move(actual,points[i]);
        // Each short move adds one dab. Independent healed dabs provide a reference
        // without replaying the tested stroke's saved parameter list.
        BrushTool dab; dab.setHealMode(mode); dab.setDiameter(diameters[i]); dab.setHardness(hardnesses[i]);
        dab.setOpacity(opacities[i]); dab.setColor(colors[i]);
        dab.begin(expected,{},expected.size(),points[i],{},reference); dab.end(&expected);
    }
    tool.end(&actual); QCOMPARE(actual,expected); QVERIFY(!tool.drawing());
    QCOMPARE(tool.diameter(),10); QCOMPARE(tool.hardness(),0); QCOMPARE(tool.opacity(),50); QCOMPARE(tool.color(),QColor(Qt::green));
    const QImage completed=actual; tool.end(&actual); QCOMPARE(actual,completed);
}

QTEST_MAIN(BrushToolTest)
#include "tst_brushtool.moc"
