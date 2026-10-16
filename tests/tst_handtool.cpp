#include <QFocusEvent>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtTest>

#include "canvas/vpcanvas.h"
#include "testhelpers.h"

namespace {
class HandCanvas : public VpCanvas {
public:
    using VpCanvas::focusOutEvent;
    using VpCanvas::hoverMoveEvent;
    void pointer(QEvent::Type type,QPointF point,Qt::MouseButton button=Qt::LeftButton,
                 Qt::MouseButtons buttons=Qt::LeftButton) {
        QMouseEvent event(type,point,point,type==QEvent::MouseMove ? Qt::NoButton : button,
                          type==QEvent::MouseButtonRelease ? Qt::NoButton : buttons,Qt::NoModifier);
        if(type==QEvent::MouseButtonPress) mousePressEvent(&event);
        else if(type==QEvent::MouseMove) mouseMoveEvent(&event);
        else mouseReleaseEvent(&event);
    }
    void space(bool down,bool repeat=false) {
        QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier,QString(),repeat);
        if(down) keyPressEvent(&event); else keyReleaseEvent(&event);
    }
};

void setupHand(VpController &controller,HandCanvas &canvas,QSize imageSize={1000,800})
{
    QImage image(imageSize,QImage::Format_ARGB32); image.fill(Qt::white);
    controller.document().setImage(image);
    canvas.setController(&controller); canvas.setSize({400,300}); canvas.setZoom(1); canvas.scrollTo(.3,.3);
    controller.setTool(VpTools::Hand);
}

QPointF scrollPosition(const HandCanvas &canvas) { return {canvas.horizontalPosition(),canvas.verticalPosition()}; }
}

class HandToolTest : public QObject {
    Q_OBJECT
private slots:
    void pansInDisplayPixels_data();
    void pansInDisplayPixels();
    void clampsAndCenters_data();
    void clampsAndCenters();
    void preservesDocumentAndHistory_data();
    void preservesDocumentAndHistory();
    void temporaryHandPreservesCreationAndStopsOnSpaceRelease();
    void protectsActiveTool_data();
    void protectsActiveTool();
    void interruptionsResetPan_data();
    void interruptionsResetPan();
    void buttonsAndOutsideRelease();
    void spaceNotificationsAndAutoRepeat();
};

void HandToolTest::pansInDisplayPixels_data()
{
    QTest::addColumn<qreal>("zoom"); QTest::addColumn<QPointF>("delta");
    for(qreal zoom : {.5,1.0,2.0}) for(const QPointF delta : {QPointF(25,0),QPointF(-25,0),QPointF(0,20),QPointF(0,-20),QPointF(25.5,-17.25)})
        QTest::newRow(qPrintable(QString("zoom-%1-delta-%2-%3").arg(zoom).arg(delta.x()).arg(delta.y()))) << zoom << delta;
}

void HandToolTest::pansInDisplayPixels()
{
    QFETCH(qreal,zoom); QFETCH(QPointF,delta);
    VpController controller; HandCanvas canvas; setupHand(controller,canvas,{1600,1200});
    canvas.setZoom(zoom); canvas.scrollTo(.2,.2); const QPointF before=scrollPosition(canvas);
    QCOMPARE(canvas.cursor().shape(),Qt::OpenHandCursor);
    QHoverEvent hover(QEvent::HoverMove,{100,100},{80,80}); canvas.hoverMoveEvent(&hover); QCOMPARE(scrollPosition(canvas),before);
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); QCOMPARE(canvas.cursor().shape(),Qt::ClosedHandCursor);
    canvas.pointer(QEvent::MouseMove,QPointF(100,100)+delta*.4);
    const QPointF middle=scrollPosition(canvas);
    canvas.pointer(QEvent::MouseMove,QPointF(100,100)+delta*.4); QCOMPARE(scrollPosition(canvas),middle);
    canvas.pointer(QEvent::MouseButtonRelease,QPointF(100,100)+delta);
    QVERIFY(qAbs(canvas.horizontalPosition()-(before.x()-delta.x()/(1600*zoom)))<1e-10);
    QVERIFY(qAbs(canvas.verticalPosition()-(before.y()-delta.y()/(1200*zoom)))<1e-10);
    QCOMPARE(canvas.cursor().shape(),Qt::OpenHandCursor);
    const QPointF after=scrollPosition(canvas);
    canvas.pointer(QEvent::MouseMove,{160,160},Qt::NoButton,Qt::NoButton); QCOMPARE(scrollPosition(canvas),after);
}

void HandToolTest::clampsAndCenters_data()
{
    QTest::addColumn<QSize>("imageSize");
    QTest::newRow("both-scroll") << QSize(1000,800); QTest::newRow("vertical-only") << QSize(200,800);
    QTest::newRow("horizontal-only") << QSize(1000,200); QTest::newRow("smaller") << QSize(200,100);
    QTest::newRow("equal") << QSize(400,300);
}

void HandToolTest::clampsAndCenters()
{
    QFETCH(QSize,imageSize); VpController controller; HandCanvas canvas; setupHand(controller,canvas,imageSize);
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseMove,{10000,10000});
    QCOMPARE(scrollPosition(canvas),QPointF());
    canvas.pointer(QEvent::MouseMove,{-10000,-10000});
    const QPointF maximum(qMax(0.0,1-400.0/imageSize.width()),qMax(0.0,1-300.0/imageSize.height()));
    QCOMPARE(scrollPosition(canvas),maximum);
    canvas.pointer(QEvent::MouseButtonRelease,{-9990,-9990});
    const QPointF expected(imageSize.width()>400 ? maximum.x()-10.0/imageSize.width() : 0,
                           imageSize.height()>300 ? maximum.y()-10.0/imageSize.height() : 0);
    QVERIFY(QLineF(scrollPosition(canvas),expected).length()<1e-10);
    QCOMPARE(canvas.horizontalSize(),qMin(1.0,400.0/imageSize.width()));
    QCOMPARE(canvas.verticalSize(),qMin(1.0,300.0/imageSize.height()));
    // A centered small image keeps its displayed center even after excessive drags.
    if(imageSize.width()<=400 && imageSize.height()<=300) {
        QImage rendered(400,300,QImage::Format_ARGB32); rendered.fill(Qt::transparent);
        { QPainter painter(&rendered); canvas.paint(&painter); }
        const QPoint topLeft((400-imageSize.width())/2,(300-imageSize.height())/2);
        QCOMPARE(rendered.pixelColor(topLeft),QColor(Qt::white));
        if(topLeft.x()>0) QCOMPARE(qAlpha(rendered.pixel(topLeft-QPoint(1,0))),0);
    }
}

void HandToolTest::preservesDocumentAndHistory_data()
{
    QTest::addColumn<bool>("temporary"); QTest::newRow("hand") << false; QTest::newRow("temporary") << true;
}

void HandToolTest::preservesDocumentAndHistory()
{
    QFETCH(bool,temporary); VpController controller; HandCanvas canvas; setupHand(controller,canvas);
    auto &doc=controller.document(); doc.appendPlane(makeTestPlane()); doc.paintLayer().setPixelColor(12,12,Qt::red); doc.resetHistory();
    controller.setTool(VpTools::Marquee); controller.pointerPress({25,25},1,Qt::NoModifier); controller.pointerRelease({60,60},Qt::NoModifier);
    if(!temporary) controller.setTool(VpTools::Hand);
    doc.setSelectedPlane(0); QImage first(12,12,QImage::Format_ARGB32); first.fill(Qt::red);
    doc.addFloatingSelection(makeTestSelection(first));
    auto image=makeTestSelection(QImage(10,10,QImage::Format_ARGB32)); image.bitmap.fill(Qt::blue);
    doc.addFloatingSelection(image); QVERIFY(doc.undo());
    const auto planes=doc.planes(); const auto floating=doc.floatingSelections(); const auto outline=controller.selectionOutline();
    const QImage background=doc.background(),paint=doc.paintLayer(); const int plane=doc.selectedPlane(),selected=doc.selectedFloatingSelection();
    QVERIFY(doc.canRedo()); QVERIFY(!outline.isEmpty());
    if(temporary) canvas.space(true);
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseMove,{130,120}); canvas.pointer(QEvent::MouseButtonRelease,{140,130});
    if(temporary) canvas.space(false);
    QVERIFY(doc.planes()==planes); QVERIFY(doc.floatingSelections()==floating); QCOMPARE(doc.background(),background); QCOMPARE(doc.paintLayer(),paint);
    QCOMPARE(doc.selectedPlane(),plane); QCOMPARE(doc.selectedFloatingSelection(),selected); QCOMPARE(controller.selectionOutline(),outline);
    QVERIFY(!doc.editActive()); QVERIFY(doc.canRedo()); QVERIFY(doc.redo()); QCOMPARE(doc.floatingSelections().size(),2);
    QVERIFY(doc.undo()); QVERIFY(doc.undo()); QVERIFY(doc.floatingSelections().isEmpty()); QVERIFY(!doc.canUndo());
}

void HandToolTest::temporaryHandPreservesCreationAndStopsOnSpaceRelease()
{
    VpController controller; HandCanvas canvas; setupHand(controller,canvas); controller.setTool(VpTools::CreatePlane);
    for(const QPointF p : {QPointF(100,100),QPointF(800,100),QPointF(800,700)}) controller.pointerPress(p,1,Qt::NoModifier);
    const auto points=controller.creationPoints(); canvas.space(true);
    QVERIFY(canvas.temporaryHandActive()); QCOMPARE(controller.tool(),VpTools::CreatePlane);
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseMove,{150,100});
    const QPointF moved=scrollPosition(canvas); canvas.space(false); QVERIFY(!canvas.temporaryHandActive());
    canvas.pointer(QEvent::MouseMove,{180,120}); canvas.pointer(QEvent::MouseButtonRelease,{190,130});
    QCOMPARE(scrollPosition(canvas),moved); QCOMPARE(controller.creationPoints(),points);
    canvas.space(true); canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseButtonRelease,{120,110});
    QVERIFY(scrollPosition(canvas)!=moved); canvas.space(false); QCOMPARE(controller.creationPoints(),points);
}

void HandToolTest::protectsActiveTool_data()
{
    QTest::addColumn<int>("tool");
    QTest::newRow("brush") << int(VpTools::Brush); QTest::newRow("clone") << int(VpTools::CloneStamp);
    QTest::newRow("plane-edit") << int(VpTools::EditPlane); QTest::newRow("marquee-create") << int(VpTools::Marquee);
    QTest::newRow("marquee-move") << -1;
}

void HandToolTest::protectsActiveTool()
{
    QFETCH(int,tool); VpController controller; HandCanvas canvas; setupHand(controller,canvas,{1000,1000});
    const PerspectiveQuad::Corners corners{{{0,0},{1000,0},{1000,1000},{0,1000}}};
    controller.document().appendPlane(PerspectivePlane(corners,corners));
    controller.setTool(tool<0 ? VpTools::Marquee : static_cast<VpTools::Tool>(tool));
    if(tool<0) { controller.pointerPress({350,350},1,Qt::NoModifier); controller.pointerRelease({550,550},Qt::NoModifier); }
    if(tool==VpTools::CloneStamp) QVERIFY(controller.pickCloneSource({100,100}));
    QSignalSpy changed(&canvas,&VpCanvas::temporaryHandActiveChanged);
    const QPointF before=scrollPosition(canvas); canvas.pointer(QEvent::MouseButtonPress,{100,100});
    if(tool==VpTools::Brush) QVERIFY(controller.brushDrawing());
    if(tool==VpTools::CloneStamp) QVERIFY(controller.cloneDrawing());
    if(tool==VpTools::EditPlane) QVERIFY(controller.document().editActive());
    canvas.space(true); QVERIFY(!canvas.temporaryHandActive()); QCOMPARE(changed.size(),0);
    canvas.pointer(QEvent::MouseMove,{125,120}); canvas.pointer(QEvent::MouseButtonRelease,{140,130});
    QCOMPARE(scrollPosition(canvas),before); QVERIFY(!controller.brushDrawing()); QVERIFY(!controller.cloneDrawing());
    QVERIFY(canvas.temporaryHandActive()); QCOMPARE(canvas.cursor().shape(),Qt::OpenHandCursor);
    QCOMPARE(changed.size(),1);
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseButtonRelease,{120,110});
    QVERIFY(scrollPosition(canvas)!=before); QCOMPARE(changed.size(),1);
    canvas.space(false); QVERIFY(!canvas.temporaryHandActive()); QCOMPARE(changed.size(),2);
}

void HandToolTest::interruptionsResetPan_data()
{
    QTest::addColumn<int>("interrupt"); QTest::addColumn<bool>("temporary");
    for(int interrupt=0;interrupt<4;++interrupt) for(bool temporary : {false,true})
        QTest::newRow(qPrintable(QString("interrupt-%1-temporary-%2").arg(interrupt).arg(temporary))) << interrupt << temporary;
}

void HandToolTest::interruptionsResetPan()
{
    QFETCH(int,interrupt); QFETCH(bool,temporary);
    VpController controller,replacement; HandCanvas canvas; setupHand(controller,canvas);
    QImage image(1000,800,QImage::Format_ARGB32); image.fill(Qt::white); replacement.document().setImage(image);
    if(temporary) { controller.setTool(VpTools::CreatePlane); canvas.space(true); }
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseMove,{120,110});
    QCOMPARE(canvas.cursor().shape(),Qt::ClosedHandCursor);
    if(interrupt==0) { QFocusEvent lost(QEvent::FocusOut); canvas.focusOutEvent(&lost); }
    if(interrupt==1) controller.setTool(VpTools::Marquee);
    if(interrupt==2) QVERIFY(controller.document().setImage(image));
    if(interrupt==3) canvas.setController(&replacement);
    QVERIFY(!canvas.temporaryHandActive()); QVERIFY(canvas.cursor().shape()!=Qt::ClosedHandCursor);
    const QPointF before=scrollPosition(canvas);
    canvas.pointer(QEvent::MouseMove,{160,140},Qt::NoButton,Qt::NoButton);
    canvas.pointer(QEvent::MouseButtonRelease,{180,150}); QCOMPARE(scrollPosition(canvas),before);
    auto *active=canvas.controller(); QVERIFY(active->creationPoints().isEmpty()); QVERIFY(active->selectionOutline().isEmpty());
    active->setTool(VpTools::Hand); canvas.setZoom(1); canvas.scrollTo(.3,.3);
    const QPointF reset=scrollPosition(canvas); canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseButtonRelease,{110,110});
    QVERIFY(scrollPosition(canvas)!=reset); QCOMPARE(canvas.cursor().shape(),Qt::OpenHandCursor);
}

void HandToolTest::buttonsAndOutsideRelease()
{
    VpController controller; HandCanvas canvas; setupHand(controller,canvas);
    const QPointF before=scrollPosition(canvas);
    QCOMPARE(canvas.acceptedMouseButtons(),Qt::MouseButtons(Qt::LeftButton));
    canvas.pointer(QEvent::MouseMove,{120,120},Qt::NoButton,Qt::NoButton); QCOMPARE(scrollPosition(canvas),before);
    canvas.pointer(QEvent::MouseButtonPress,{100,100},Qt::RightButton,Qt::RightButton);
    canvas.pointer(QEvent::MouseMove,{130,130},Qt::NoButton,Qt::RightButton); canvas.pointer(QEvent::MouseButtonRelease,{130,130},Qt::RightButton);
    QCOMPARE(scrollPosition(canvas),before); QCOMPARE(canvas.cursor().shape(),Qt::OpenHandCursor);
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseMove,{-50,-50}); canvas.pointer(QEvent::MouseButtonRelease,{-60,-60});
    const QPointF after=scrollPosition(canvas); QVERIFY(after!=before); QCOMPARE(canvas.cursor().shape(),Qt::OpenHandCursor);
    canvas.pointer(QEvent::MouseMove,{100,100},Qt::NoButton,Qt::NoButton); QCOMPARE(scrollPosition(canvas),after);
}

void HandToolTest::spaceNotificationsAndAutoRepeat()
{
    VpController controller; HandCanvas canvas; setupHand(controller,canvas); controller.setTool(VpTools::CreatePlane);
    QSignalSpy changed(&canvas,&VpCanvas::temporaryHandActiveChanged);
    canvas.space(true); QVERIFY(canvas.temporaryHandActive()); QCOMPARE(changed.size(),1);
    canvas.space(true,true); canvas.space(false,true); canvas.space(true); QCOMPARE(changed.size(),1);
    canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseButtonRelease,{110,110});
    QVERIFY(canvas.temporaryHandActive()); QCOMPARE(changed.size(),1);
    canvas.space(false); QVERIFY(!canvas.temporaryHandActive()); QCOMPARE(changed.size(),2);
    canvas.space(false); QCOMPARE(changed.size(),2);
    canvas.space(true); QFocusEvent lost(QEvent::FocusOut); canvas.focusOutEvent(&lost);
    QVERIFY(!canvas.temporaryHandActive()); QCOMPARE(changed.size(),4);
    canvas.space(true); QVERIFY(controller.document().setImage(controller.document().background()));
    QVERIFY(!canvas.temporaryHandActive()); QCOMPARE(changed.size(),6);
}

QTEST_MAIN(HandToolTest)
#include "tst_handtool.moc"
