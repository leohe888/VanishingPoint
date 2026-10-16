#include "canvas/vpcanvas.h"
#include "core/scenerenderer.h"
#include "canvas/floatingcontentitem.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QHoverEvent>
#include <QMouseEvent>
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

class CloneCanvas final : public VpCanvas {
public:
    using VpCanvas::VpCanvas;
    std::atomic<bool> measuring{false};
    std::atomic<int> paints{0};
    void paint(QPainter *p) override { VpCanvas::paint(p); if(measuring.load()) ++paints; }
    QPointF widgetPoint(const QPointF &image) const {
        const QSizeF shown=QSizeF(controller()->document().background().size())*zoom();
        return QPointF((width()-shown.width())/2,(height()-shown.height())/2)+image*zoom();
    }
    void press(const QPointF &image) { const auto p=widgetPoint(image); QMouseEvent e(QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier); VpCanvas::mousePressEvent(&e); }
    void move(const QPointF &image) { const auto p=widgetPoint(image); QMouseEvent e(QEvent::MouseMove,p,Qt::NoButton,Qt::LeftButton,Qt::NoModifier); VpCanvas::mouseMoveEvent(&e); }
    void release(const QPointF &image) { const auto p=widgetPoint(image); QMouseEvent e(QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton,Qt::NoModifier); VpCanvas::mouseReleaseEvent(&e); }
protected:
    void mousePressEvent(QMouseEvent *) override {}
    void mouseMoveEvent(QMouseEvent *) override {}
    void mouseReleaseEvent(QMouseEvent *) override {}
    void hoverMoveEvent(QHoverEvent *) override {}
    void hoverLeaveEvent(QHoverEvent *) override {}
};
static double p95(QVector<double> v) { if(v.isEmpty())return 0;std::sort(v.begin(),v.end());return v[int(std::ceil(v.size()*.95))-1]; }
static QString hash(const QImage &image) {
    QCryptographicHash h(QCryptographicHash::Sha256);
    for(int y=0;y<image.height();++y)h.addData(reinterpret_cast<const char *>(image.constScanLine(y)),image.width()*4);
    return QString::fromLatin1(h.result().toHex());
}
static int maximumChannelDifference(const QImage &a, const QImage &b) {
    if(a.size()!=b.size())return 255;
    int delta=0;
    for(int y=0;y<a.height();++y)for(int x=0;x<a.width();++x){const QRgb p=a.pixel(x,y),q=b.pixel(x,y);
        delta=qMax(delta,qAbs(qRed(p)-qRed(q)));delta=qMax(delta,qAbs(qGreen(p)-qGreen(q)));
        delta=qMax(delta,qAbs(qBlue(p)-qBlue(q)));delta=qMax(delta,qAbs(qAlpha(p)-qAlpha(q)));}
    return delta;
}
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);if(argc<2)return 2;
    const QString output=QString::fromLocal8Bit(argv[1]);
    const int side=argc>2?QByteArray(argv[2]).toInt():4096;
    QJsonArray failures, hashes, metrics;
    auto check=[&](bool value,const QString &name){if(!value)failures.append(name);};
    // Fixed input paths compare pixels independently of asynchronous event counts.
    for(int c=0;c<6;++c) {
        VpController controller;
        QImage background(512,512,QImage::Format_RGB32);for(int y=0;y<512;++y){auto *row=reinterpret_cast<QRgb *>(background.scanLine(y)); for(int x=0;x<512;++x)row[x]=qRgb(x%256,y%256,(x+y)%256);}
        controller.document().setImage(background);
        const PerspectivePlane::Corners quad{{QPointF(35,50),QPointF(470,20),QPointF(410,470),QPointF(60,420)}};
        const PerspectivePlane::Corners surface{{QPointF(0,0),QPointF(450,0),QPointF(450,450),QPointF(0,450)}};
        controller.document().appendPlane(PerspectivePlane(quad,surface));
        // Nonzero initial pixels reveal lost/overwritten undo snapshots at tile edges.
        auto &layer=controller.document().paintLayer();
        for(int y=0;y<512;++y){auto *row=reinterpret_cast<QRgb *>(layer.scanLine(y));
            for(int x=0;x<512;++x)row[x]=qPremultiply(qRgba(x%256,y%256,(x+y)%256,30+(x+y)%100));}
        const QString before=hash(layer);controller.document().resetHistory();controller.setTool(VpTools::CloneStamp);
        controller.setCloneDiameter(c==0?1:c==5?500:35+c*30); controller.setCloneAligned(c%2==0); controller.pickCloneSource(QPointF(350,120));
        controller.setCloneHardness(c%2?0:100);controller.setCloneOpacity(15+c*17);
        controller.pointerPress(QPointF(90,100),1,Qt::NoModifier);
        const auto sampleBuilds=controller.sampleBuildCount();
        for(int i=1;i<=80;++i)controller.pointerMove(QPointF(90+i*3.7,220+120*std::sin(i*.17)),Qt::NoModifier);
        controller.pointerRelease(QPointF(430,440),Qt::NoModifier);
        check(controller.sampleBuildCount()==sampleBuilds,"sampling-source-frozen-during-stroke");
        const QString after=hash(layer);hashes.append(after);check(after!=before,"stroke-writes-pixels");
        controller.undo();check(hash(layer)==before,"undo-restores-every-pixel-"+QString::number(c));
        controller.redo();check(hash(layer)==after,"redo-restores-every-pixel-"+QString::number(c));
        controller.pickCloneSource(QPointF(400,100));
        controller.pointerPress(QPointF(230,230),1,Qt::NoModifier);
        controller.pointerMove(QPointF(260,260),Qt::NoModifier);controller.setTool(VpTools::Hand);
        check(!controller.cloneDrawing(),"tool-switch-finishes-stroke");
        controller.undo();check(hash(layer)==after,"tool-switch-stroke-undo");
        controller.setTool(VpTools::CloneStamp);
        controller.pointerPress(QPointF(140,160),1,Qt::NoModifier);
        controller.pointerRelease(QPointF(160,170),Qt::NoModifier);
        hashes.append(hash(layer));
    }
    VpController controller;
    QImage background(side,side,QImage::Format_RGB32);background.fill(QColor(96,96,96));
    controller.document().setImage(background);const qreal k=side/4096.0;
    const PerspectivePlane::Corners corners{{QPointF(400,400)*k,QPointF(3600,400)*k,QPointF(3400,3600)*k,QPointF(600,3400)*k}};
    const PerspectivePlane::Corners surface{{QPointF(0,0),QPointF(3200,0)*k,QPointF(3200,3000)*k,QPointF(0,3000)*k}};
    controller.document().appendPlane(PerspectivePlane(corners,surface));controller.document().resetHistory();controller.setTool(VpTools::CloneStamp); controller.pickCloneSource(QPointF(800,900)*k);
    QQuickWindow window;window.setFlags(Qt::Tool|Qt::WindowStaysOnTopHint|Qt::WindowDoesNotAcceptFocus);
    window.resize(1600,1000);window.setTitle("Vanishing point clone checks");
    auto *canvas=new CloneCanvas(window.contentItem());canvas->setSize(QSizeF(1600,1000));canvas->setController(&controller);window.show();
    auto settle=[&]{QEventLoop loop;QTimer::singleShot(120,&loop,&QEventLoop::quit);loop.exec();};settle();
    PaintLayerItem *paintItem=nullptr;
    for(auto *child:canvas->childItems())if(auto *item=dynamic_cast<PaintLayerItem *>(child))paintItem=item;
    check(paintItem!=nullptr,"paint-layer-item-exists");
    QElapsedTimer clock;clock.start();qint64 id=0,synced=0,submitted=0;double input=0,syncTime=0,previous=0;
    QMutex mutex;QVector<double> latency,intervals;
    QObject::connect(&window,&QQuickWindow::beforeSynchronizing,&window,[&]{synced=id;syncTime=input;},Qt::DirectConnection);
    QObject::connect(&window,&QQuickWindow::frameSwapped,&window,[&]{if(canvas->measuring.load()&&synced>submitted){QMutexLocker lock(&mutex);
        const double now=clock.nsecsElapsed()/1e6;latency.append(now-syncTime);if(previous)intervals.append(now-previous);previous=now;}submitted=synced;},Qt::DirectConnection);
    for(const int diameter:{42,200}) {
        controller.setCloneDiameter(diameter);controller.setCloneHardness(diameter==42?75:0);controller.setCloneOpacity(diameter==42?100:35);
        const uchar *pixels=controller.document().paintLayer().constBits();
        QElapsedTimer first;first.start();canvas->press(QPointF(1200,1600)*k);const double pressMs=first.nsecsElapsed()/1e6;
#if VP_CLONE_STAGE >= 2
        check(pixels==controller.document().paintLayer().constBits(),"no-whole-paint-layer-detach");
#else
        Q_UNUSED(pixels);
#endif
        settle();QVector<double> updates;QElapsedTimer duration;duration.start();QEventLoop loop;QTimer timer;timer.setInterval(4);timer.setTimerType(Qt::PreciseTimer);
        quint64 startArea=0,startPaints=0;bool started=false;
        QObject::connect(&timer,&QTimer::timeout,&loop,[&]{
            const double t=duration.elapsed()/1000.0;
            if(t>=3.7){canvas->measuring=false;timer.stop();loop.quit();return;}
            if(t>=.3&&!started){started=true;canvas->paints=0;QMutexLocker lock(&mutex);latency.clear();intervals.clear();previous=0;
                startArea=paintItem->paintedArea();startPaints=paintItem->paintCount();
            }
            canvas->measuring=t>=.3;input=clock.nsecsElapsed()/1e6;++id;
            QElapsedTimer cost;cost.start();canvas->move(QPointF(2100+650*std::sin(t*4),2000+500*std::cos(t*5))*k);
            if(t>=.3)updates.append(cost.nsecsElapsed()/1e6);
        });timer.start();loop.exec();settle();
        const quint64 area=paintItem->paintedArea()-startArea,paintCount=paintItem->paintCount()-startPaints;
#if VP_CLONE_STAGE >= 1
        check(canvas->paints==0,"clone-does-not-repaint-background");
        check(area<paintCount*1600*1000/5,"clone-paint-is-local");
#endif
        QMutexLocker lock(&mutex);check(latency.size()>=20,"active-frames");
        metrics.append(QJsonObject{{"diameter",diameter},{"pressMs",pressMs},{"latencyP95Ms",p95(latency)},
            {"updateP95Ms",p95(updates)},{"intervalP95Ms",p95(intervals)},{"backgroundPaintCount",canvas->paints.load()},
            {"paintLayerPaintCount",qint64(paintCount)},{"paintedLogicalPixels",qint64(area)},{"frames",latency.size()},{"inputCount",updates.size()}});
        lock.unlock();canvas->release(QPointF(1600,1800)*k);settle();
        const QImage shot=window.grabWindow();shot.save(output+".clone"+QString::number(diameter)+".png");
    }
    // View changes must refresh cached paint pixels even if image cacheKey is unchanged.
    const QImage partialShot=window.grabWindow();
    canvas->fitView();settle();const QImage fitShot=window.grabWindow();
    // Qt smooth scaling/composition rounds differently for different clip regions.
    // Permit two 8-bit levels across scaling/composition, including strong minification.
    // Exported paint-layer pixels above still require exact SHA256 equality.
    const int screenDelta=maximumChannelDifference(partialShot,fitShot);
    const int screenTolerance=2;
    check(screenDelta<=screenTolerance,"dirty-refresh-matches-full-refresh-within-rounding");
    partialShot.save(output+".partial.png");fitShot.save(output+".full.png");
    canvas->setZoom(canvas->zoom()*1.2);settle();canvas->fitView();settle();
    check(window.grabWindow()==fitShot,"zoom-fit-restores-paint-alignment");
    const QString painted=hash(controller.document().paintLayer());const QImage paintedShot=window.grabWindow();
    controller.undo();settle();check(window.grabWindow()!=paintedShot,"undo-refreshes-visible-pixels");
    controller.redo();settle();check(window.grabWindow()==paintedShot,"redo-restores-visible-pixels");
    check(hash(controller.document().paintLayer())==painted,"large-image-undo-redo");
    const QJsonObject result{{"passed",failures.isEmpty()},{"stage",VP_CLONE_STAGE},{"failures",failures},
        {"hashes",hashes},{"imageSide",side},{"dpr",window.devicePixelRatio()},{"screenMaxChannelDifference",screenDelta},{"metrics",metrics}};
    QFile file(output);if(!file.open(QIODevice::WriteOnly))return 3;file.write(QJsonDocument(result).toJson());return failures.isEmpty()?0:5;
}
