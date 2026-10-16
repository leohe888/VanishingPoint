#include <limits>
#include <QFocusEvent>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtTest>

#include "canvas/vpcanvas.h"
#include "cursorhelpers.h"
#include "testhelpers.h"

namespace {
class ZoomCanvas : public CursorCanvas {
public:
    using VpCanvas::focusOutEvent;
    using VpCanvas::updatePolish;
    void pointer(QEvent::Type type,QPointF point,Qt::KeyboardModifiers modifiers=Qt::NoModifier,
                 Qt::MouseButton button=Qt::LeftButton) {
        QMouseEvent event(type,point,point,type==QEvent::MouseMove ? Qt::NoButton : button,
                          type==QEvent::MouseButtonRelease ? Qt::NoButton : button,modifiers);
        if(type==QEvent::MouseButtonPress) mousePressEvent(&event);
        else if(type==QEvent::MouseMove) mouseMoveEvent(&event);
        else mouseReleaseEvent(&event);
    }
    void space(bool down) {
        QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);
        if(down) keyPressEvent(&event); else keyReleaseEvent(&event);
    }
};

QImage background(QSize size={1600,1200})
{
    QImage image(size,QImage::Format_ARGB32); image.fill(Qt::white); return image;
}

void setupZoom(VpController &controller,ZoomCanvas &canvas,QSize size={1600,1200})
{
    controller.document().setImage(background(size)); canvas.setController(&controller); canvas.setSize({400,300});
    canvas.setZoom(1); canvas.scrollTo(.3,.3); controller.setTool(VpTools::Zoom);
}

QPointF offset(const ZoomCanvas &canvas)
{
    const QSize size=canvas.controller()->document().background().size();
    const qreal width=size.width()*canvas.zoom(),height=size.height()*canvas.zoom();
    return {width<=canvas.width() ? (canvas.width()-width)/2 : -canvas.horizontalPosition()*width,
            height<=canvas.height() ? (canvas.height()-height)/2 : -canvas.verticalPosition()*height};
}

QPointF imageAt(const ZoomCanvas &canvas,QPointF widget) { return (widget-offset(canvas))/canvas.zoom(); }
}

class ZoomToolTest : public QObject {
    Q_OBJECT
private slots:
    void preservesAnchor_data();
    void preservesAnchor();
    void walksAllLevelsAndClamps();
    void stepsFromNonstandardScales_data();
    void stepsFromNonstandardScales();
    void invalidAndExtremeScales_data();
    void invalidAndExtremeScales();
    void clampsEdgesAndCentersSmallAxes_data();
    void clampsEdgesAndCentersSmallAxes();
    void fitAndFill_data();
    void fitAndFill();
    void deferredFitAndViewportResize();
    void repeatedZoomDoesNotDrift();
    void mouseGestureAndTemporaryHand();
    void interruptionsRecover_data();
    void interruptionsRecover();
    void preservesDocumentAndRedo();
    void cursorFollowsModifiers();
};

void ZoomToolTest::preservesAnchor_data()
{
    QTest::addColumn<int>("operation"); QTest::addColumn<bool>("out");
    for(int operation=0;operation<3;++operation) for(bool out : {false,true})
        QTest::newRow(qPrintable(QString("operation-%1-out-%2").arg(operation).arg(out))) << operation << out;
}

void ZoomToolTest::preservesAnchor()
{
    QFETCH(int,operation); QFETCH(bool,out); VpController controller; ZoomCanvas canvas; setupZoom(controller,canvas);
    const QPointF anchor=operation==0 ? QPointF(90.25,70.5) : QPointF(200,150);
    const QPointF before=imageAt(canvas,anchor);
    if(operation==0) { canvas.pointer(QEvent::MouseButtonPress,anchor,out ? Qt::AltModifier : Qt::NoModifier);
                      canvas.pointer(QEvent::MouseButtonRelease,anchor); }
    if(operation==1) canvas.setZoom(out ? .75 : 1.5);
    if(operation==2) canvas.zoomStep(out);
    QVERIFY(QLineF(imageAt(canvas,anchor),before).length()<1e-9);
    QCOMPARE(canvas.zoom(),operation==1 ? (out ? .75 : 1.5) : (out ? .667 : 2));
}

void ZoomToolTest::walksAllLevelsAndClamps()
{
    VpController controller; ZoomCanvas canvas; setupZoom(controller,canvas);
    const QList<qreal> expected{.063,.125,.25,.333,.5,.667,1,2,3,4,6,8,10,12,16};
    const auto levels=canvas.zoomLevels(); QCOMPARE(levels.size(),expected.size());
    canvas.setZoom(expected.first());
    for(int i=0;i<expected.size();++i) { QCOMPARE(levels[i].toReal(),expected[i]); QCOMPARE(canvas.zoom(),expected[i]); canvas.zoomStep(false); }
    QCOMPARE(canvas.zoom(),qreal(16)); canvas.zoomStep(false); QCOMPARE(canvas.zoom(),qreal(16));
    for(int i=expected.size()-1;i>=0;--i) { QCOMPARE(canvas.zoom(),expected[i]); canvas.zoomStep(true); }
    QCOMPARE(canvas.zoom(),qreal(.063)); canvas.zoomStep(true); QCOMPARE(canvas.zoom(),qreal(.063));
}

void ZoomToolTest::stepsFromNonstandardScales_data()
{
    QTest::addColumn<qreal>("scale"); QTest::addColumn<qreal>("zoomIn"); QTest::addColumn<qreal>("zoomOut");
    QTest::newRow("nearest-lower") << qreal(1.4) << qreal(2) << qreal(.667);
    QTest::newRow("nearest-upper") << qreal(1.6) << qreal(3) << qreal(1);
    QTest::newRow("tie-prefers-lower") << qreal(1.5) << qreal(2) << qreal(.667);
}

void ZoomToolTest::stepsFromNonstandardScales()
{
    QFETCH(qreal,scale); QFETCH(qreal,zoomIn); QFETCH(qreal,zoomOut);
    VpController controller; ZoomCanvas canvas; setupZoom(controller,canvas);
    canvas.setZoom(scale); canvas.zoomStep(false); QCOMPARE(canvas.zoom(),zoomIn);
    canvas.setZoom(scale); canvas.zoomStep(true); QCOMPARE(canvas.zoom(),zoomOut);
}

void ZoomToolTest::invalidAndExtremeScales_data()
{
    QTest::addColumn<qreal>("requested"); QTest::addColumn<qreal>("expected");
    QTest::newRow("zero") << qreal(0) << qreal(.063); QTest::newRow("negative") << qreal(-1) << qreal(.063);
    QTest::newRow("huge") << qreal(1e100) << qreal(16);
    QTest::newRow("nan") << std::numeric_limits<qreal>::quiet_NaN() << qreal(1);
    QTest::newRow("infinity") << std::numeric_limits<qreal>::infinity() << qreal(1);
    QTest::newRow("negative-infinity") << -std::numeric_limits<qreal>::infinity() << qreal(1);
}

void ZoomToolTest::invalidAndExtremeScales()
{
    QFETCH(qreal,requested); QFETCH(qreal,expected); VpController controller; ZoomCanvas canvas; setupZoom(controller,canvas);
    const QPointF before=offset(canvas); QSignalSpy changed(&canvas,&VpCanvas::viewChanged);
    canvas.setZoom(requested); QCOMPARE(canvas.zoom(),expected);
    if(!qIsFinite(requested)) { QCOMPARE(offset(canvas),before); QCOMPARE(changed.size(),0); }
}

void ZoomToolTest::clampsEdgesAndCentersSmallAxes_data()
{
    QTest::addColumn<QSize>("size"); QTest::addColumn<bool>("farEdge");
    for(const QSize size : {QSize(1000,800),QSize(200,800),QSize(1000,100),QSize(200,100)}) for(bool farEdge : {false,true})
        QTest::newRow(qPrintable(QString("%1x%2-far-%3").arg(size.width()).arg(size.height()).arg(farEdge))) << size << farEdge;
}

void ZoomToolTest::clampsEdgesAndCentersSmallAxes()
{
    QFETCH(QSize,size); QFETCH(bool,farEdge); VpController controller; ZoomCanvas canvas; setupZoom(controller,canvas,size);
    canvas.scrollTo(farEdge ? 1 : 0,farEdge ? 1 : 0);
    const QPointF anchor=farEdge ? QPointF(399,299) : QPointF(1,1),point=imageAt(canvas,anchor);
    canvas.pointer(QEvent::MouseButtonPress,anchor,Qt::AltModifier); canvas.pointer(QEvent::MouseButtonRelease,anchor);
    const qreal scale=canvas.zoom(); QPointF ideal=anchor-point*scale;
    ideal.setX(size.width()*scale<=400 ? (400-size.width()*scale)/2 : qBound(400-size.width()*scale,ideal.x(),0.0));
    ideal.setY(size.height()*scale<=300 ? (300-size.height()*scale)/2 : qBound(300-size.height()*scale,ideal.y(),0.0));
    QVERIFY(QLineF(offset(canvas),ideal).length()<1e-9);
    QVERIFY(canvas.horizontalPosition()>=0 && canvas.horizontalPosition()<=1-canvas.horizontalSize()+1e-9);
    QVERIFY(canvas.verticalPosition()>=0 && canvas.verticalPosition()<=1-canvas.verticalSize()+1e-9);
}

void ZoomToolTest::fitAndFill_data()
{
    QTest::addColumn<QSize>("size"); QTest::addColumn<bool>("fill");
    for(const QSize size : {QSize(1000,800),QSize(600,1200),QSize(10000,10),QSize(10,10000),QSize(1,1)}) for(bool fill : {false,true})
        QTest::newRow(qPrintable(QString("%1x%2-fill-%3").arg(size.width()).arg(size.height()).arg(fill))) << size << fill;
}

void ZoomToolTest::fitAndFill()
{
    QFETCH(QSize,size); QFETCH(bool,fill); VpController controller; ZoomCanvas canvas; setupZoom(controller,canvas,size);
    canvas.fitView(fill);
    const qreal sx=368.0/size.width(),sy=268.0/size.height();
    const qreal expected=qBound(.063,fill ? qMax(sx,sy) : qMin(sx,sy),16.0);
    QVERIFY(qAbs(canvas.zoom()-expected)<1e-12);
    QVERIFY(QLineF(imageAt(canvas,{200,150}),QPointF(size.width()/2.0,size.height()/2.0)).length()<1e-8);
    QCOMPARE(canvas.horizontalSize(),qMin(1.0,400/(size.width()*expected)));
    QCOMPARE(canvas.verticalSize(),qMin(1.0,300/(size.height()*expected)));
}

void ZoomToolTest::deferredFitAndViewportResize()
{
    VpController controller; ZoomCanvas canvas; canvas.setController(&controller);
    canvas.setZoom(2); QCOMPARE(canvas.zoom(),qreal(1)); // No image yet.
    canvas.fitView(); QVERIFY(controller.document().setImage(background()));
    canvas.setSize({400,300}); canvas.updatePolish();
    QVERIFY(qAbs(canvas.zoom()-268.0/1200)<1e-12);
    canvas.setZoom(1.5); canvas.scrollTo(.3,.3); const QPointF center=imageAt(canvas,{200,150});
    canvas.setSize({600,450}); QCOMPARE(canvas.zoom(),qreal(1.5));
    QVERIFY(QLineF(imageAt(canvas,{300,225}),center).length()<1e-9);
    canvas.fitView(); QVERIFY(qAbs(canvas.zoom()-418.0/1200)<1e-12);
}

void ZoomToolTest::repeatedZoomDoesNotDrift()
{
    VpController controller; ZoomCanvas canvas; setupZoom(controller,canvas);
    const QPointF anchor(90.25,70.5),before=imageAt(canvas,anchor);
    for(int i=0;i<40;++i) {
        canvas.pointer(QEvent::MouseButtonPress,anchor); canvas.pointer(QEvent::MouseButtonRelease,anchor);
        canvas.pointer(QEvent::MouseButtonPress,anchor,Qt::AltModifier); canvas.pointer(QEvent::MouseButtonRelease,anchor);
    }
    QCOMPARE(canvas.zoom(),qreal(1)); QVERIFY(QLineF(imageAt(canvas,anchor),before).length()<1e-8);
}

void ZoomToolTest::mouseGestureAndTemporaryHand()
{
    VpController controller; ZoomCanvas canvas; setupZoom(controller,canvas);
    canvas.pointer(QEvent::MouseMove,{90,90},Qt::NoModifier,Qt::NoButton); QCOMPARE(canvas.zoom(),qreal(1));
    canvas.pointer(QEvent::MouseButtonPress,{100,100},Qt::NoModifier,Qt::RightButton);
    canvas.pointer(QEvent::MouseButtonRelease,{100,100},Qt::NoModifier,Qt::RightButton); QCOMPARE(canvas.zoom(),qreal(1));
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); QCOMPARE(canvas.zoom(),qreal(2));
    canvas.pointer(QEvent::MouseMove,{130,130}); canvas.pointer(QEvent::MouseButtonRelease,{140,140}); QCOMPARE(canvas.zoom(),qreal(2));
    const QPointF before=offset(canvas); canvas.space(true); QVERIFY(canvas.temporaryHandActive());
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseMove,{120,110}); canvas.pointer(QEvent::MouseButtonRelease,{140,130});
    QCOMPARE(canvas.zoom(),qreal(2)); QCOMPARE(offset(canvas),before+QPointF(40,30)); canvas.space(false);
    QCOMPARE(controller.tool(),VpTools::Zoom); QVERIFY(!canvas.temporaryHandActive());
    canvas.pointer(QEvent::MouseButtonPress,{100,100},Qt::AltModifier); canvas.pointer(QEvent::MouseButtonRelease,{100,100});
    QCOMPARE(canvas.zoom(),qreal(1));
}

void ZoomToolTest::interruptionsRecover_data()
{
    QTest::addColumn<int>("interrupt");
    const char *names[]={"focus","tool","image","controller"}; for(int i=0;i<4;++i) QTest::newRow(names[i]) << i;
}

void ZoomToolTest::interruptionsRecover()
{
    QFETCH(int,interrupt); VpController controller,replacement; ZoomCanvas canvas; setupZoom(controller,canvas);
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); QCOMPARE(canvas.zoom(),qreal(2));
    if(interrupt==0) { QFocusEvent lost(QEvent::FocusOut); canvas.focusOutEvent(&lost); }
    if(interrupt==1) controller.setTool(VpTools::Marquee);
    if(interrupt==2) QVERIFY(controller.document().setImage(background()));
    if(interrupt==3) { replacement.document().setImage(background()); canvas.setController(&replacement); }
    auto *active=canvas.controller();
    const PerspectiveQuad::Corners corners{{{0,0},{1600,0},{1600,1200},{0,1200}}};
    active->document().appendPlane(PerspectivePlane(corners,corners)); active->setTool(VpTools::Marquee);
    canvas.setZoom(1); canvas.scrollTo(.3,.3);
    // A lost mouse release from the zoom gesture must not consume the next gesture.
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseMove,{130,120});
    QVERIFY(!active->selectionOutline().isEmpty()); canvas.pointer(QEvent::MouseButtonRelease,{140,130});
    QCOMPARE(active->selectionOutline().boundingRect().size(),QSizeF(40,30));
    active->setTool(VpTools::Zoom); canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseButtonRelease,{100,100});
    QCOMPARE(canvas.zoom(),qreal(2));
}

void ZoomToolTest::preservesDocumentAndRedo()
{
    VpController controller; ZoomCanvas canvas; setupZoom(controller,canvas); auto &doc=controller.document();
    doc.appendPlane(makeTestPlane()); doc.paintLayer().setPixelColor(12,12,Qt::red); doc.resetHistory();
    controller.setTool(VpTools::Marquee); controller.pointerPress({25,25},1,Qt::NoModifier); controller.pointerRelease({60,60},Qt::NoModifier);
    controller.setTool(VpTools::Zoom); doc.setSelectedPlane(0);
    QImage bitmap(12,12,QImage::Format_ARGB32); bitmap.fill(Qt::red); doc.addFloatingSelection(makeTestSelection(bitmap));
    bitmap.fill(Qt::blue); doc.addFloatingSelection(makeTestSelection(bitmap)); QVERIFY(doc.undo());
    const auto planes=doc.planes(); const auto floating=doc.floatingSelections(); const auto outline=controller.selectionOutline();
    const QImage paint=doc.paintLayer(),image=doc.background(); const int selected=doc.selectedFloatingSelection(),plane=doc.selectedPlane();
    canvas.setZoom(2); canvas.zoomStep(true); canvas.fitView();
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseButtonRelease,{100,100});
    QVERIFY(doc.planes()==planes); QVERIFY(doc.floatingSelections()==floating); QCOMPARE(doc.paintLayer(),paint); QCOMPARE(doc.background(),image);
    QCOMPARE(controller.selectionOutline(),outline); QCOMPARE(doc.selectedFloatingSelection(),selected); QCOMPARE(doc.selectedPlane(),plane);
    QVERIFY(!doc.editActive()); QVERIFY(doc.canRedo()); QVERIFY(doc.redo()); QCOMPARE(doc.floatingSelections().size(),2);
    QVERIFY(doc.undo()); QVERIFY(doc.undo()); QVERIFY(doc.floatingSelections().isEmpty()); QVERIFY(!doc.canUndo());
}

void ZoomToolTest::cursorFollowsModifiers()
{
    VpController controller; ZoomCanvas canvas; setupZoom(controller,canvas);
    canvas.setZoomCursorDirectory(QUrl::fromLocalFile(QStringLiteral(VP_CURSOR_DIR)+"/"));
    const QImage zoomIn=canvas.cursor().pixmap().toImage(); QVERIFY(!zoomIn.isNull()); checkCursorSize(canvas.cursor());
    QKeyEvent press(QEvent::KeyPress,Qt::Key_Alt,Qt::AltModifier); canvas.keyPressEvent(&press);
    const QImage zoomOut=canvas.cursor().pixmap().toImage(); QVERIFY(!zoomOut.isNull() && zoomOut!=zoomIn); checkCursorSize(canvas.cursor());
    canvas.pointer(QEvent::MouseButtonPress,{100,100},Qt::AltModifier); QCOMPARE(canvas.zoom(),qreal(.667));
    canvas.pointer(QEvent::MouseButtonRelease,{100,100},Qt::AltModifier); QCOMPARE(canvas.cursor().pixmap().toImage(),zoomOut);
    QKeyEvent release(QEvent::KeyRelease,Qt::Key_Alt,Qt::NoModifier); canvas.keyReleaseEvent(&release); QCOMPARE(canvas.cursor().pixmap().toImage(),zoomIn);
    QHoverEvent enter(QEvent::HoverEnter,{10,10},{},Qt::AltModifier); canvas.hoverEnterEvent(&enter); QCOMPARE(canvas.cursor().pixmap().toImage(),zoomOut);
    canvas.space(true); QCOMPARE(canvas.cursor().shape(),Qt::OpenHandCursor); canvas.space(false);
    QCOMPARE(canvas.cursor().pixmap().toImage(),zoomIn);
    controller.setTool(VpTools::Hand); QCOMPARE(canvas.cursor().shape(),Qt::OpenHandCursor);
    controller.setTool(VpTools::Zoom); QCOMPARE(canvas.cursor().pixmap().toImage(),zoomIn);
}

QTEST_MAIN(ZoomToolTest)
#include "tst_zoomtool.moc"
