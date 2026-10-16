#include "canvas/vpcanvas.h"
#include "core/scenerenderer.h"
#if VP_SELECTION_STAGE >= 3
#include "canvas/floatingcontentitem.h"
#endif
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTimer>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QCryptographicHash>
#include <QMutexLocker>
#include <atomic>
#include <algorithm>
#include <cmath>
#ifndef VP_SELECTION_STAGE
#define VP_SELECTION_STAGE 0
#endif

class Canvas final : public VpCanvas {
public:
    using VpCanvas::VpCanvas;
    std::atomic<bool> measuring{false};
    std::atomic<int> paints{0};
    void paint(QPainter *p) override { VpCanvas::paint(p); if (measuring.load()) ++paints; }
protected:
    void mousePressEvent(QMouseEvent *) override {}
    void mouseMoveEvent(QMouseEvent *) override {}
    void mouseReleaseEvent(QMouseEvent *) override {}
    void hoverMoveEvent(QHoverEvent *) override {}
    void hoverLeaveEvent(QHoverEvent *) override {}
};
static double p95(QVector<double> v) {
    if (v.isEmpty()) return 0;
    std::sort(v.begin(), v.end()); return v[int(std::ceil(v.size() * .95)) - 1];
}
static QString hash(const QImage &image) {
    QCryptographicHash h(QCryptographicHash::Sha256);
    for (int y = 0; y < image.height(); ++y) h.addData(reinterpret_cast<const char *>(image.constScanLine(y)), image.width() * 4);
    return QString::fromLatin1(h.result().toHex());
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc < 2) return 2;
    const QString output = QString::fromLocal8Bit(argv[1]);
    const int side = argc > 2 ? QByteArray(argv[2]).toInt() : 4096;
    QJsonArray failures, metrics;
    auto check = [&](bool value, const QString &name) { if (!value) failures.append(name); };
    // Two joined faces with different projections, for pixel-equivalence checks.
    PerspectivePlane a({{QPointF(0,0),QPointF(200,0),QPointF(200,200),QPointF(0,200)}},
                       {{QPointF(0,0),QPointF(200,0),QPointF(200,200),QPointF(0,200)}});
    PerspectivePlane b({{QPointF(200,0),QPointF(400,-30),QPointF(400,230),QPointF(200,200)}},
                       {{QPointF(200,0),QPointF(400,0),QPointF(400,200),QPointF(200,200)}});
    a.setSurfaceGroupId(1); b.setSurfaceGroupId(1);
    QImage source(512,512,QImage::Format_ARGB32_Premultiplied);
    for(int y=0;y<512;++y) { auto *row=reinterpret_cast<QRgb *>(source.scanLine(y));
        for(int x=0;x<512;++x) row[x]=qPremultiply(qRgba(x%256,y%256,(x+y)%256,128+(x%128))); }
    MarqueeTool tool;
    check(tool.beginCreate({a,b},QPointF(50,50)),"cross-face-begin");
    QPointF end; b.quad().surfaceToCanvasTransform().mapForward(QPointF(350,150),&end);
    tool.update(end,Qt::NoModifier,50); tool.end();
    const QPainterPath outline=tool.outline();
    check(outline.contains(QPointF(100,100)) && outline.contains(QPointF(300,100)),"cross-face-outline");
#if VP_SELECTION_STAGE >= 1
    const auto count=tool.outlineBuildCount();
    tool.outline(); tool.contains(QPointF(100,100)); tool.outline();
    check(tool.outlineBuildCount()==count,"outline-cache-shared-by-hit-test");
#endif
    const FloatingSelection copied=tool.copy(source);
    QImage paint(512,512,QImage::Format_ARGB32_Premultiplied); paint.fill(Qt::transparent);
    check(tool.beginFill(QPointF(100,100),source,paint),"cross-face-clone-begin");
    tool.update(QPointF(125,90),Qt::ControlModifier,50,&paint);
    const FloatingSelection cloned=tool.clone();
    check(!copied.bitmap.isNull()&&!cloned.bitmap.isNull(),"cross-face-extraction");
    const QString copyHash=hash(copied.bitmap), cloneHash=hash(cloned.bitmap);
    QStringList variantHashes;
    for (int i=0;i<6;++i) {
        MarqueeTool variant;
        variant.beginCreate({a,b},QPointF(10+i*8,20+i*7));
        variant.update(QPointF(280+i*12,160-i*9),Qt::NoModifier,50);variant.end();
        variantHashes.append(hash(variant.copy(source).bitmap));
        variant.beginFill(QPointF(80,80),source,paint);
        variant.update(QPointF(80+(i-3)*65,80+(i-2)*37),Qt::ControlModifier,50,&paint);
        variantHashes.append(hash(variant.clone().bitmap));
    }
    copied.bitmap.save(output+".copy.png"); cloned.bitmap.save(output+".clone.png");
    tool.end();

    VpController controller;
    QImage background(side,side,QImage::Format_RGB32); background.fill(QColor(96,96,96));
    controller.document().setImage(background);
    const qreal k=side/4096.0;
    const PerspectivePlane::Corners corners{{QPointF(500,600)*k,QPointF(3200,600)*k,
        QPointF(3200,3200)*k,QPointF(500,3200)*k}};
    controller.document().appendPlane(PerspectivePlane(corners,corners));
    controller.document().resetHistory(); controller.setTool(VpTools::Marquee);
    QQuickWindow window;
    window.setFlags(Qt::Tool|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus);
    window.resize(1600,1000); window.setTitle("Vanishing point selection checks");
    auto *canvas=new Canvas(window.contentItem()); canvas->setSize(QSizeF(1600,1000)); canvas->setController(&controller);
    window.show();
    auto settle=[&] { QEventLoop loop; QTimer::singleShot(120,&loop,&QEventLoop::quit); loop.exec(); };
    settle();
    QElapsedTimer clock; clock.start();
    qint64 id=0,synced=0,submitted=0; double input=0,syncTime=0;
    QVector<double> latency, intervals; double previous=0;
    QMutex mutex;
    QObject::connect(&window,&QQuickWindow::beforeSynchronizing,&window,[&]{synced=id;syncTime=input;},Qt::DirectConnection);
    QObject::connect(&window,&QQuickWindow::frameSwapped,&window,[&]{
        if(canvas->measuring.load()&&synced>submitted) {QMutexLocker lock(&mutex); double now=clock.nsecsElapsed()/1e6;
            latency.append(now-syncTime); if(previous) intervals.append(now-previous); previous=now;}
        submitted=synced;
    },Qt::DirectConnection);
    auto run=[&](const QString &name, const std::function<void(double)> &tick, bool expectStatic) {
        canvas->paints=0; {QMutexLocker lock(&mutex);latency.clear();intervals.clear();previous=0;}
        QElapsedTimer duration; duration.start(); QEventLoop loop; QTimer timer;
        QVector<double> updateCosts;
        timer.setInterval(4);timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer,&QTimer::timeout,&loop,[&] {
            const double t=duration.elapsed()/1000.0;
            if(t>=1.7){canvas->measuring=false;timer.stop();loop.quit();return;}
            canvas->measuring=t>=.3; input=clock.nsecsElapsed()/1e6;++id;
            QElapsedTimer cost;cost.start();tick(t);
            if(t>=.3) updateCosts.append(cost.nsecsElapsed()/1e6);
        });timer.start();loop.exec();settle();
        QMutexLocker lock(&mutex);
        check(latency.size()>=20,name+":active-frames");
        if(expectStatic) check(canvas->paints==0,name+":no-base-repaint");
        metrics.append(QJsonObject{{"operation",name},{"contentPaintCount",int(canvas->paints)},
            {"latencyP95Ms",p95(latency)},{"intervalP95Ms",p95(intervals)},{"frames",latency.size()},
            {"updateP95Ms",p95(updateCosts)},{"inputCount",updateCosts.size()}});
    };
    const auto paintKey=controller.document().paintLayer().cacheKey();
    controller.pointerPress(QPointF(800,900)*k,canvas->zoom(),Qt::NoModifier);
    run("create",[&](double t){controller.pointerMove(QPointF(1800+160*std::sin(t*3),2000+160*std::cos(t*4))*k,Qt::NoModifier);},VP_SELECTION_STAGE>=1);
    controller.pointerRelease(QPointF(1800,2000)*k,Qt::NoModifier);settle();
    window.grabWindow().save(output+".selection.png");
    const QRectF startRect=controller.selectionRect();
    controller.pointerPress(QPointF(1100,1200)*k,canvas->zoom(),Qt::NoModifier);
    run("move_outline",[&](double t){controller.pointerMove(QPointF(1100+100*std::sin(t*3),1200+100*std::cos(t*4))*k,Qt::NoModifier);},VP_SELECTION_STAGE>=1);
    controller.pointerRelease(QPointF(1200,1200)*k,Qt::NoModifier);settle();
    check(controller.selectionRect()!=startRect,"outline-moved");
    check(controller.document().paintLayer().cacheKey()==paintKey,"outline-does-not-touch-pixels");
    controller.copySelection();
#if VP_SELECTION_STAGE >= 4
    const auto sampleBuilds=controller.sampleBuildCount();
#endif
    QElapsedTimer copyTimer; copyTimer.start();
    for(int i=0;i<8;++i) controller.copySelection();
    const double repeatCopyMs=copyTimer.nsecsElapsed()/1e6/8;
#if VP_SELECTION_STAGE >= 4
    check(controller.sampleBuildCount()==sampleBuilds,"repeat-copy-reuses-sampling-composite");
    controller.document().appendPlane(a);
    controller.copySelection();
    check(controller.sampleBuildCount()==sampleBuilds,"plane-guides-do-not-invalidate-sampling-composite");
    controller.document().removePlane(1);
    controller.document().paintLayer().setPixelColor(0,0,QColor(10,20,30));
    controller.copySelection();
    check(controller.sampleBuildCount()==sampleBuilds+1,"changed-pixels-invalidate-sampling-composite");
#endif
    const auto clonePaintKey=controller.document().paintLayer().cacheKey();
    controller.pointerPress(QPointF(1200,1300)*k,canvas->zoom(),Qt::ControlModifier);
    run("ctrl_clone",[&](double t){controller.pointerMove(QPointF(1200+100*std::sin(t*3),1300+100*std::cos(t*4))*k,Qt::ControlModifier);},VP_SELECTION_STAGE>=2);
#if VP_SELECTION_STAGE >= 2
    check(controller.document().paintLayer().cacheKey()==clonePaintKey,"clone-preview-does-not-touch-pixels");
#endif
    window.grabWindow().save(output+".ctrl.png");
    controller.pointerRelease(QPointF(1250,1350)*k,Qt::ControlModifier);settle();
    check(controller.document().floatingSelections().size()==1,"clone-commits-floating-selection");
    if(!controller.document().floatingSelections().isEmpty()) {
        const auto before=controller.document().floatingSelection(0);
        controller.setTool(VpTools::Transform);
        const QPointF center=before.mapPlacementToCanvas(before.placementOrigin+QPointF(before.bitmap.width()/2.0,before.bitmap.height()/2.0));
        controller.pointerPress(center,canvas->zoom(),Qt::NoModifier);
        run("floating_move",[&](double t){controller.pointerMove(center+QPointF(100*std::sin(t*3),100*std::cos(t*4))*k,Qt::NoModifier);},VP_SELECTION_STAGE>=3);
        controller.pointerRelease(center+QPointF(50,50)*k,Qt::NoModifier);settle();
        const auto after=controller.document().floatingSelection(0);
        check(!(after==before),"floating-position-changed");
        controller.pointerPress(after.mapPlacementToCanvas(after.transformHandlePositions()[2]),canvas->zoom(),Qt::NoModifier);
        const QPointF corner=after.mapPlacementToCanvas(after.transformHandlePositions()[2]);
        run("floating_scale",[&](double t){controller.pointerMove(corner+QPointF(100+80*std::sin(t*3),100)*k,Qt::NoModifier);},VP_SELECTION_STAGE>=3);
        controller.pointerRelease(corner+QPointF(100,100)*k,Qt::NoModifier);settle();
        check(controller.document().floatingSelection(0).scaleFactors!=after.scaleFactors,"floating-scale-changed");
        controller.undo();
        const QPointF rotateStart=after.mapPlacementToCanvas(after.transformHandlePositions()[1])+QPointF(18/canvas->zoom(),0);
        controller.pointerPress(rotateStart,canvas->zoom(),Qt::NoModifier);
        run("floating_rotate",[&](double t){controller.pointerMove(rotateStart+QPointF(0,100+80*std::sin(t*3))*k,Qt::NoModifier);},VP_SELECTION_STAGE>=3);
        controller.pointerRelease(rotateStart+QPointF(0,100)*k,Qt::NoModifier);settle();
        check(controller.document().floatingSelection(0).rotationDegrees!=after.rotationDegrees,"floating-rotation-changed");
        controller.undo();
        controller.undo();check(controller.document().floatingSelection(0)==before,"floating-undo");
        controller.redo();check(controller.document().floatingSelection(0)==after,"floating-redo");
        window.grabWindow().save(output+".floating.png");
#if VP_SELECTION_STAGE >= 3
        // A second item stays cached while the first selection is transformed.
        FloatingSelection second;
        second.bitmap=QImage(64,64,QImage::Format_ARGB32_Premultiplied);
        second.bitmap.fill(QColor(200,100,20,140));second.placementOrigin=QPointF(100,100)*k;
        controller.document().addFloatingSelection(second);
        controller.document().setSelectedFloatingSelection(0);
        FloatingContentItem *staticItem=nullptr;
        for(auto *child:canvas->childItems()) if(auto *item=dynamic_cast<FloatingContentItem *>(child)) staticItem=item;
        check(staticItem!=nullptr,"independent-floating-items-created");
        settle(); const quint64 staticPaints=staticItem->paintCount();
        controller.pointerPress(center+QPointF(50,50)*k,canvas->zoom(),Qt::NoModifier);
        controller.pointerMove(center+QPointF(70,70)*k,Qt::NoModifier);settle();
        check(staticItem->paintCount()==staticPaints,"static-floating-texture-retained");
        controller.cancelInteraction();settle();
        check(controller.document().floatingSelection(0)==after,"floating-cancel-restores-state");
        controller.document().removeFloatingSelection(1);
#if VP_SELECTION_STAGE >= 4
        controller.document().setSelectedFloatingSelection(-1);
        controller.copySelection(); const auto floatSamples=controller.sampleBuildCount();
        controller.copySelection();check(controller.sampleBuildCount()==floatSamples,"floating-sampling-cache-hit");
        controller.document().setFloatingSelectionOrigin(0,after.placementOrigin+QPointF(1,1));
        controller.document().setSelectedFloatingSelection(-1);
        controller.copySelection();check(controller.sampleBuildCount()==floatSamples+1,"floating-geometry-invalidates-sampling");
        const auto oldBackgroundSamples=controller.sampleBuildCount();
        controller.document().setImage(source);
        controller.copySelection();check(controller.sampleBuildCount()==oldBackgroundSamples+1,"document-replacement-invalidates-sampling");
#endif
#endif
    }
    QJsonObject result{{"passed",failures.isEmpty()},{"stage",VP_SELECTION_STAGE},{"failures",failures},
        {"copyHash",copyHash},{"cloneHash",cloneHash},{"variantHashes",QJsonArray::fromStringList(variantHashes)},{"dpr",window.devicePixelRatio()},
        {"imageSide",side},{"metrics",metrics},{"repeatCopyMs",repeatCopyMs}};
#if VP_SELECTION_STAGE >= 4
    result["sampleBuildCount"]=qint64(controller.sampleBuildCount());
#endif
    QFile file(output);if(!file.open(QIODevice::WriteOnly))return 3;file.write(QJsonDocument(result).toJson());
    return failures.isEmpty()?0:5;
}
