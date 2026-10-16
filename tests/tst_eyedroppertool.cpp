#include <limits>
#include <QCursor>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QtTest>

#include "canvas/vpcanvas.h"
#include "cursorhelpers.h"
#include "testhelpers.h"

namespace {
QImage solidImage(const QColor &color,QSize size=QSize(64,64))
{
    QImage image(size,QImage::Format_ARGB32); image.fill(color); return image;
}

void setupEyedropper(VpController &controller)
{
    controller.document().setImage(solidImage(QColor(30,80,150)));
    controller.setTool(VpTools::Eyedropper);
}

FloatingSelection floatingImage(const QColor &color)
{
    auto selection=makeTestSelection(solidImage(color,{16,16}));
    selection.placementOrigin={16,16}; return selection;
}

class EyedropperCanvas : public VpCanvas {
public:
    using VpCanvas::mousePressEvent;
    using VpCanvas::mouseMoveEvent;
    using VpCanvas::mouseReleaseEvent;
    using VpCanvas::hoverMoveEvent;
    void pointer(QEvent::Type type,const QPointF &point) {
        QMouseEvent event(type,point,point,type==QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type==QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,Qt::NoModifier);
        if(type==QEvent::MouseButtonPress) mousePressEvent(&event);
        else if(type==QEvent::MouseMove) mouseMoveEvent(&event);
        else mouseReleaseEvent(&event);
    }
};
}

class EyedropperToolTest : public QObject
{
    Q_OBJECT
private slots:
    void cursorArtworkPreservesSamplingHotspot();
    void availabilityAndFinalPosition();
    void pixelCoordinates_data();
    void pixelCoordinates();
    void transparentPixelsAndOpacity_data();
    void transparentPixelsAndOpacity();
    void compositeLayerOrder_data();
    void compositeLayerOrder();
    void transformedFloatingContent_data();
    void transformedFloatingContent();
    void sampleCacheInvalidation_data();
    void sampleCacheInvalidation();
    void unchangedCompositeReusesCache();
    void colorNotificationsAreExact();
    void samplingPreservesDocumentAndRedo();
    void canvasSamplingCoordinates_data();
    void canvasSamplingCoordinates();
    void switchingFromStrokeKeepsSettings_data();
    void switchingFromStrokeKeepsSettings();
};

void EyedropperToolTest::availabilityAndFinalPosition()
{
    VpController controller;
    QVERIFY(!controller.isToolEnabled(VpTools::Eyedropper));
    QVERIFY(!controller.pickBrushColor({0,0}));
    QImage background=solidImage(Qt::red); background.setPixelColor(2,2,Qt::green); background.setPixelColor(3,3,Qt::blue);
    QVERIFY(controller.document().setImage(background));
    QVERIFY(controller.isToolEnabled(VpTools::Eyedropper));
    controller.setTool(VpTools::Eyedropper);
    controller.pointerPress({1.2,1.8},1,Qt::NoModifier); QCOMPARE(controller.brushColor(),QColor(Qt::red));
    controller.pointerMove({2,2},Qt::NoModifier); QCOMPARE(controller.brushColor(),QColor(Qt::green));
    controller.pointerRelease({3,3},Qt::NoModifier); QCOMPARE(controller.brushColor(),QColor(Qt::blue));
    QVERIFY(!controller.document().canUndo());
}

void EyedropperToolTest::pixelCoordinates_data()
{
    QTest::addColumn<QPointF>("point"); QTest::addColumn<bool>("valid");
    for(const QPointF point : {QPointF(0,0),QPointF(63,0),QPointF(0,63),QPointF(63.999,63.999),
                              QPointF(.999,1.999),QPointF(1,2),QPointF(1.999,2.999)})
        QTest::newRow(qPrintable(QString("valid-%1-%2").arg(point.x()).arg(point.y()))) << point << true;
    const qreal nan=std::numeric_limits<qreal>::quiet_NaN(),inf=std::numeric_limits<qreal>::infinity();
    const QPointF invalid[]={{-.001,2},{2,-.001},{64,2},{2,64},{nan,2},{2,nan},{inf,2},{2,-inf}};
    for(int i=0;i<8;++i) QTest::newRow(qPrintable(QString("invalid-%1").arg(i))) << invalid[i] << false;
}

void EyedropperToolTest::pixelCoordinates()
{
    QFETCH(QPointF,point); QFETCH(bool,valid);
    VpController controller; QImage image=solidImage(Qt::white);
    for(int y=0;y<64;++y) for(int x=0;x<64;++x) image.setPixelColor(x,y,QColor(x*4,y*4,70));
    QVERIFY(controller.document().setImage(image)); controller.setBrushColor(Qt::magenta);
    QSignalSpy changed(&controller,&VpController::brushChanged); const auto builds=controller.sampleBuildCount();
    QCOMPARE(controller.pickBrushColor(point),valid);
    if(valid) { QCOMPARE(controller.brushColor(),image.pixelColor(qFloor(point.x()),qFloor(point.y()))); QCOMPARE(changed.size(),1); }
    else { QCOMPARE(controller.brushColor(),QColor(Qt::magenta)); QCOMPARE(changed.size(),0); QCOMPARE(controller.sampleBuildCount(),builds); }
}

void EyedropperToolTest::transparentPixelsAndOpacity_data()
{
    QTest::addColumn<bool>("paintLayer"); QTest::addColumn<int>("alpha");
    for(bool paint : {false,true}) for(int alpha : {0,1,128,255})
        QTest::newRow(qPrintable(QString("paint-%1-alpha-%2").arg(paint).arg(alpha))) << paint << alpha;
}

void EyedropperToolTest::transparentPixelsAndOpacity()
{
    QFETCH(bool,paintLayer); QFETCH(int,alpha);
    VpController controller; QVERIFY(controller.document().setImage(solidImage(paintLayer ? QColor(Qt::transparent) : QColor(255,0,0,alpha))));
    if(paintLayer) controller.document().paintLayer().fill(QColor(255,0,0,alpha));
    controller.setBrushColor(Qt::blue); controller.setBrushOpacity(37);
    QSignalSpy changed(&controller,&VpController::brushChanged);
    QCOMPARE(controller.pickBrushColor({20,20}),alpha>0);
    QCOMPARE(controller.brushColor(),QColor(alpha>0 ? Qt::red : Qt::blue));
    QCOMPARE(controller.brushColor().alpha(),255); QCOMPARE(controller.brushOpacity(),37);
    QCOMPARE(changed.size(),alpha>0 ? 1 : 0);
}

void EyedropperToolTest::compositeLayerOrder_data()
{
    QTest::addColumn<int>("opacity"); for(int opacity : {0,50,100}) QTest::newRow(qPrintable(QString::number(opacity))) << opacity;
}

void EyedropperToolTest::compositeLayerOrder()
{
    QFETCH(int,opacity);
    VpController controller; setupEyedropper(controller); auto &doc=controller.document();
    doc.paintLayer().fill(QColor(0,255,0,128));
    doc.addFloatingSelection(floatingImage(Qt::red)); auto upper=floatingImage(Qt::blue); upper.opacity=opacity;
    doc.addFloatingSelection(upper); doc.setSelectedFloatingSelection(0);
    QImage expected=doc.background().convertToFormat(QImage::Format_ARGB32_Premultiplied);
    { QPainter painter(&expected); painter.drawImage(0,0,doc.paintLayer());
      painter.fillRect(16,16,16,16,Qt::red); painter.setOpacity(opacity/100.0); painter.fillRect(16,16,16,16,Qt::blue); }
    QVERIFY(controller.pickBrushColor({20,20})); QCOMPARE(controller.brushColor(),expected.pixelColor(20,20));
    QVERIFY(controller.pickBrushColor({4,4})); QCOMPARE(controller.brushColor(),expected.pixelColor(4,4));
}

void EyedropperToolTest::transformedFloatingContent_data()
{
    QTest::addColumn<int>("transform");
    const char *names[]={"moved","scaled","rotated","flip-horizontal","flip-vertical","perspective"};
    for(int i=0;i<6;++i) QTest::newRow(names[i]) << i;
}

void EyedropperToolTest::transformedFloatingContent()
{
    QFETCH(int,transform);
    VpController controller; setupEyedropper(controller); auto selection=floatingImage(Qt::red);
    { QPainter painter(&selection.bitmap); painter.fillRect(8,0,8,8,Qt::green);
      painter.fillRect(0,8,8,8,Qt::blue); painter.fillRect(8,8,8,8,Qt::yellow); }
    if(transform==0) selection.placementOrigin={30,30};
    if(transform==1) selection.scaleFactors={1.5,2};
    if(transform==2) selection.rotationDegrees=90;
    if(transform==3) selection.flippedHorizontally=true;
    if(transform==4) selection.flippedVertically=true;
    if(transform==5) {
        const PerspectiveQuad::Corners domain{{{0,0},{64,0},{64,64},{0,64}}};
        const PerspectiveQuad::Corners canvas{{{5,5},{55,8},{58,60},{3,55}}};
        selection.surfaceAttached=true; selection.surfaceQuads={PerspectiveQuad(canvas,domain)}; selection.hostQuadIndex=0;
    }
    controller.document().addFloatingSelection(selection);
    const QPointF points[]={{3.5,3.5},{12.5,3.5},{3.5,12.5},{12.5,12.5}};
    const QColor colors[]={Qt::red,Qt::green,Qt::blue,Qt::yellow};
    for(int i=0;i<4;++i) {
        const QPointF placement=selection.bitmapToPlacementTransform().map(points[i]);
        const QPointF canvas=selection.mapPlacementToCanvas(placement);
        QVERIFY(controller.pickBrushColor(canvas)); QCOMPARE(controller.brushColor(),colors[i]);
    }
}

void EyedropperToolTest::sampleCacheInvalidation_data()
{
    QTest::addColumn<int>("change");
    const char *names[]={"paint","add","delete","move","opacity","bitmap","undo","redo","reload"};
    for(int i=0;i<9;++i) QTest::newRow(names[i]) << i;
}

void EyedropperToolTest::sampleCacheInvalidation()
{
    QFETCH(int,change);
    VpController controller; setupEyedropper(controller); auto &doc=controller.document();
    doc.addFloatingSelection(floatingImage(Qt::red));
    if(change==7) controller.undo();
    const QPointF point=change==0 ? QPointF(4,4) : QPointF(20,20);
    QVERIFY(controller.pickBrushColor(point)); const auto builds=controller.sampleBuildCount();
    const QColor before=controller.brushColor(); QColor expected(30,80,150);
    switch(change) {
    case 0: doc.paintLayer().setPixelColor(4,4,Qt::green); expected=Qt::green; break;
    case 1: doc.addFloatingSelection(floatingImage(Qt::blue)); expected=Qt::blue; break;
    case 2: doc.removeFloatingSelection(0); break;
    case 3: doc.setFloatingSelectionOrigin(0,{40,40}); break;
    case 4: { auto image=doc.floatingSelection(0); image.opacity=0; QVERIFY(doc.setFloatingSelection(0,image)); break; }
    case 5: { auto image=doc.floatingSelection(0); image.bitmap.fill(Qt::blue); QVERIFY(doc.setFloatingSelection(0,image)); expected=Qt::blue; break; }
    case 6: controller.undo(); break;
    case 7: controller.redo(); expected=Qt::red; break;
    case 8: QVERIFY(doc.setImage(solidImage(Qt::yellow))); expected=Qt::yellow; break;
    }
    QVERIFY(expected!=before); QVERIFY(controller.pickBrushColor(point)); QCOMPARE(controller.brushColor(),expected);
    QCOMPARE(controller.sampleBuildCount(),builds+1);
    QVERIFY(controller.pickBrushColor(point)); QCOMPARE(controller.sampleBuildCount(),builds+1);
}

void EyedropperToolTest::unchangedCompositeReusesCache()
{
    VpController controller; setupEyedropper(controller); auto &doc=controller.document();
    doc.appendPlane(makeTestPlane()); doc.addFloatingSelection(floatingImage(Qt::red));
    QVERIFY(controller.pickBrushColor({4,4})); const auto builds=controller.sampleBuildCount();
    controller.setBrushDiameter(17); controller.setBrushHardness(42); controller.setBrushOpacity(35);
    controller.setBrushColor(Qt::blue); controller.setBrushHealMode(1);
    doc.setSelectedFloatingSelection(-1); doc.setSelectedPlane(0);
    QVERIFY(controller.pickBrushColor({4,4})); QCOMPARE(controller.sampleBuildCount(),builds);
    doc.setSelectedFloatingSelection(0); QVERIFY(controller.pickBrushColor({20,20})); QCOMPARE(controller.sampleBuildCount(),builds);
    EyedropperCanvas canvas; canvas.setController(&controller); canvas.setSize({40,40});
    canvas.setZoom(2); canvas.scrollTo(.2,.3);
    QVERIFY(controller.pickBrushColor({4,4})); QCOMPARE(controller.sampleBuildCount(),builds);
}

void EyedropperToolTest::colorNotificationsAreExact()
{
    VpController controller; setupEyedropper(controller); controller.setBrushColor(Qt::blue);
    QSignalSpy changed(&controller,&VpController::brushChanged);
    QSignalSpy repaint(&controller,&VpController::repaintRequested);
    QVERIFY(controller.pickBrushColor({4,4})); QCOMPARE(changed.size(),1); QCOMPARE(repaint.size(),1);
    QVERIFY(controller.pickBrushColor({5,5})); QCOMPARE(changed.size(),1); QCOMPARE(repaint.size(),1);
    QVERIFY(!controller.pickBrushColor({-1,5})); QCOMPARE(changed.size(),1); QCOMPARE(repaint.size(),1);
    controller.document().paintLayer().setPixelColor(6,6,Qt::green);
    QVERIFY(controller.pickBrushColor({6,6})); QCOMPARE(changed.size(),2); QCOMPARE(repaint.size(),2);
    QVERIFY(controller.document().setImage(solidImage(Qt::transparent)));
    changed.clear(); repaint.clear(); const QColor before=controller.brushColor();
    QVERIFY(!controller.pickBrushColor({4,4})); QCOMPARE(controller.brushColor(),before);
    QCOMPARE(changed.size(),0); QCOMPARE(repaint.size(),0);
}

void EyedropperToolTest::samplingPreservesDocumentAndRedo()
{
    VpController controller; setupEyedropper(controller); auto &doc=controller.document(); doc.appendPlane(makeTestPlane());
    doc.paintLayer().setPixelColor(12,12,Qt::green); doc.resetHistory();
    controller.setTool(VpTools::Marquee); controller.pointerPress({25,25},1,Qt::NoModifier); controller.pointerRelease({40,40},Qt::NoModifier);
    controller.setTool(VpTools::Eyedropper); doc.setSelectedPlane(0);
    doc.addFloatingSelection(floatingImage(Qt::red)); doc.addFloatingSelection(floatingImage(Qt::blue));
    QVERIFY(doc.undo()); // Retain a redo branch and the ordinary marquee while picking.
    const auto planes=doc.planes(); const auto floating=doc.floatingSelections();
    const QImage background=doc.background(),paint=doc.paintLayer(); const auto outline=controller.selectionOutline();
    const int selectedPlane=doc.selectedPlane(),selectedFloating=doc.selectedFloatingSelection();
    QVERIFY(!outline.isEmpty()); QVERIFY(doc.canRedo());
    for(const QPointF point : {QPointF(4,4),QPointF(20,20),QPointF(30,30),QPointF(-1,2)}) {
        controller.pointerPress(point,1,Qt::NoModifier); controller.pointerMove(point+QPointF(1,0),Qt::NoModifier);
        controller.pointerRelease(point,Qt::NoModifier);
    }
    QVERIFY(doc.planes()==planes); QVERIFY(doc.floatingSelections()==floating); QCOMPARE(doc.background(),background); QCOMPARE(doc.paintLayer(),paint);
    QCOMPARE(doc.selectedPlane(),selectedPlane); QCOMPARE(doc.selectedFloatingSelection(),selectedFloating);
    QCOMPARE(controller.selectionOutline(),outline); QVERIFY(!doc.editActive()); QVERIFY(doc.canRedo());
    QVERIFY(doc.redo()); QCOMPARE(doc.floatingSelections().size(),2); QVERIFY(doc.undo());
    QVERIFY(doc.undo()); QVERIFY(doc.floatingSelections().isEmpty());
}

void EyedropperToolTest::canvasSamplingCoordinates_data()
{
    QTest::addColumn<qreal>("zoom"); QTest::addColumn<bool>("scrolled");
    for(qreal zoom : {.5,1.0,2.0}) for(bool scrolled : {false,true})
        QTest::newRow(qPrintable(QString("zoom-%1-scroll-%2").arg(zoom).arg(scrolled))) << zoom << scrolled;
}

void EyedropperToolTest::canvasSamplingCoordinates()
{
    QFETCH(qreal,zoom); QFETCH(bool,scrolled);
    VpController controller; QImage image=solidImage(Qt::white,{200,160});
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) image.setPixelColor(x,y,QColor(x,y,70));
    QVERIFY(controller.document().setImage(image)); controller.setTool(VpTools::Eyedropper);
    EyedropperCanvas canvas; canvas.setController(&controller); canvas.setSize({80,60}); canvas.setZoom(zoom);
    canvas.scrollTo(scrolled ? .3 : 0,scrolled ? .2 : 0);
    const QPointF offset(-canvas.horizontalPosition()*image.width()*zoom,-canvas.verticalPosition()*image.height()*zoom);
    const auto colorAt=[&](const QPointF &widget) {
        const QPointF p=(widget-offset)/zoom; return image.pixelColor(qFloor(p.x()),qFloor(p.y()));
    };
    controller.setBrushColor(Qt::magenta);
    QHoverEvent hover(QEvent::HoverMove,{20,20},{10,10}); canvas.hoverMoveEvent(&hover);
    QCOMPARE(controller.brushColor(),QColor(Qt::magenta)); QCOMPARE(controller.sampleBuildCount(),quint64(0));
    canvas.pointer(QEvent::MouseButtonPress,{20.25,20.25}); QCOMPARE(controller.brushColor(),colorAt({20.25,20.25}));
    const auto builds=controller.sampleBuildCount(); const QColor before=controller.brushColor();
    canvas.pointer(QEvent::MouseMove,offset-QPointF(2,2)); QCOMPARE(controller.brushColor(),before);
    canvas.pointer(QEvent::MouseMove,{30.25,25.25}); QCOMPARE(controller.brushColor(),colorAt({30.25,25.25}));
    canvas.pointer(QEvent::MouseButtonRelease,{40.25,30.25}); QCOMPARE(controller.brushColor(),colorAt({40.25,30.25}));
    QCOMPARE(controller.sampleBuildCount(),builds); QVERIFY(!controller.document().canUndo());
}

void EyedropperToolTest::switchingFromStrokeKeepsSettings_data()
{
    QTest::addColumn<bool>("clone"); QTest::newRow("brush") << false; QTest::newRow("clone") << true;
}

void EyedropperToolTest::switchingFromStrokeKeepsSettings()
{
    QFETCH(bool,clone);
    VpController controller; setupEyedropper(controller); auto &doc=controller.document(); doc.appendPlane(makeTestPlane()); doc.resetHistory();
    controller.setBrushDiameter(12); controller.setBrushHardness(100); controller.setBrushOpacity(65); controller.setBrushHealMode(0);
    controller.setBrushColor(Qt::red); controller.setCloneDiameter(8); controller.setCloneHealMode(0);
    controller.setTool(clone ? VpTools::CloneStamp : VpTools::Brush);
    if(clone) { QVERIFY(controller.pickCloneSource({4,4})); QVERIFY(controller.beginClone({25,25})); }
    else QVERIFY(controller.beginBrush({25,25}));
    const QImage painted=doc.paintLayer(); controller.setTool(VpTools::Eyedropper);
    QVERIFY(!controller.brushDrawing()); QVERIFY(!controller.cloneDrawing()); QCOMPARE(doc.paintLayer(),painted);
    QVERIFY(controller.pickBrushColor({4,4})); const QColor picked=controller.brushColor();
    controller.setTool(VpTools::Brush); QCOMPARE(controller.brushColor(),picked);
    QCOMPARE(controller.brushDiameter(),12); QCOMPARE(controller.brushHardness(),100); QCOMPARE(controller.brushOpacity(),65); QCOMPARE(controller.brushHealMode(),0);
    QVERIFY(controller.beginBrush({50,50})); controller.endBrush();
    const QColor pixel=doc.paintLayer().pixelColor(50,50);
    QVERIFY(qAbs(pixel.red()-picked.red())<=1); QVERIFY(qAbs(pixel.green()-picked.green())<=1); QVERIFY(qAbs(pixel.blue()-picked.blue())<=1);
    controller.undo(); QCOMPARE(doc.paintLayer(),painted); controller.undo(); QVERIFY(!doc.hasPaintContent()); QVERIFY(!doc.canUndo());
}

void EyedropperToolTest::cursorArtworkPreservesSamplingHotspot()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    VpController controller;
    QImage background(64,64,QImage::Format_ARGB32_Premultiplied);
    background.fill(Qt::white);
    QVERIFY(controller.document().setImage(background));
    controller.setTool(VpTools::Eyedropper);
    VpCanvas canvas;
    canvas.setController(&controller);
    canvas.setEyedropperCursorSource(QUrl::fromLocalFile(QStringLiteral(VP_EYEDROPPER_CURSOR)));
    const QPixmap source(QStringLiteral(VP_EYEDROPPER_CURSOR));
    check(!source.isNull(), "project cursor SVG loads");
    const qreal ratio = QGuiApplication::primaryScreen()->logicalDotsPerInchX() / 96.0;
    const int padding = qMax(1,qRound(ratio));
    check(canvas.cursor().shape() == Qt::BitmapCursor
              && canvas.cursor().hotSpot().x() <= padding + 1
              && canvas.cursor().hotSpot().y() >= canvas.cursor().pixmap().height() - padding - 2,
          "normalized eyedropper retains lower-left sampling hotspot");
    checkCursorSize(canvas.cursor());
    controller.setTool(VpTools::Hand);
    check(canvas.cursor().shape() == Qt::OpenHandCursor, "switching tool restores hand cursor");
    controller.setTool(VpTools::Eyedropper);
    check(canvas.cursor().shape() == Qt::BitmapCursor, "switching back restores eyedropper cursor");

}

QTEST_MAIN(EyedropperToolTest)
#include "tst_eyedroppertool.moc"
