#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QMetaProperty>
#include <QTemporaryDir>
#include <QtTest>

#include "canvas/vpcontroller.h"
#include "testhelpers.h"

namespace {
QImage controllerImage(QColor color=Qt::white,QSize size={400,300})
{
    QImage image(size,QImage::Format_ARGB32_Premultiplied); image.fill(color); return image;
}

PerspectivePlane controllerPlane()
{
    const PerspectiveQuad::Corners corners{{{0,0},{400,0},{400,300},{0,300}}}; return PerspectivePlane(corners,corners);
}

void setupController(VpController &controller)
{
    controller.document().setImage(controllerImage()); controller.document().appendPlane(controllerPlane()); controller.document().resetHistory();
    controller.setBrushHealMode(0); controller.setCloneHealMode(0); controller.setMarqueeHealMode(0); controller.setMarqueeFeather(0);
}

FloatingSelection controllerSelection(QColor color=Qt::red,QPointF origin={100,100})
{
    auto image=makeTestSelection(controllerImage(color,{40,30})); image.placementOrigin=origin; return image;
}
}

class VpControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void applyCallbackContract();
    void outputContainsOnlyPersistentContent();
    void applyingDuringInteraction_data();
    void applyingDuringInteraction();
    void availabilityMatrix_data();
    void availabilityMatrix();
    void unavailableToolPreservesInteraction();
    void propertyNotifications_data();
    void propertyNotifications();
    void dispatchPrefersTopmostAndHandles();
    void bakeClickDoesNotDispatchToTool_data();
    void bakeClickDoesNotDispatchToTool();
    void documentReplacementClearsInteraction_data();
    void documentReplacementClearsInteraction();
    void clipboardFailuresPreserveRedo();
    void deletePriorityAndOptionSynchronization();
    void notificationsExposeConsistentState();
    void persistentHintsFollowInteractionLifecycle();
    void clipboardPastePreservesPixelsAndCopyPriority();
    void qmlPropertiesNotifyAndClamp();
    void toolAvailabilityFollowsDocument();
};

void VpControllerTest::persistentHintsFollowInteractionLifecycle()
{
    VpController controller;
    QImage background(400, 300, QImage::Format_ARGB32); background.fill(Qt::white);
    QVERIFY(controller.document().setImage(background));
    QSignalSpy status(&controller, &VpController::statusMessage);
    QVERIFY(!controller.toolHint().isEmpty());
    controller.pointerPress(QPointF(10, 10), 1, Qt::NoModifier);
    QVERIFY(!controller.interactionHint().isEmpty());
    QVERIFY(status.isEmpty()); // Progress is persistent, not an expiring operation notification.
    controller.deleteSelection();
    QVERIFY(!controller.interactionHint().isEmpty()); // All points undone: keep the next-step instruction.
    controller.cancelInteraction();
    QVERIFY(controller.interactionHint().isEmpty());
    const auto corners = makeTestPlane().quad().canvasCorners();
    for (const QPointF &point : corners)
        controller.pointerPress(point, 1, Qt::NoModifier);
    QCOMPARE(controller.tool(), VpTools::EditPlane);
    QVERIFY(controller.interactionHint().isEmpty());
    QVERIFY(!status.isEmpty());
    QVERIFY(!status.last().at(1).toBool());

    // A Ctrl edge drag retains its instruction until release, including an uncommitted extrusion.
    controller.pointerPress(controller.document().planes()[0].quad().controlPoints()[4], 1, Qt::ControlModifier);
    controller.pointerMove(corners[0] + QPointF(30, -60), Qt::ControlModifier);
    QVERIFY(!controller.interactionHint().isEmpty());
    controller.pointerRelease(corners[0] + QPointF(30, -60), Qt::ControlModifier);
    QVERIFY(controller.interactionHint().isEmpty());
    controller.setTool(VpTools::CloneStamp);
    controller.pointerPress(QPointF(30, 30), 1, Qt::NoModifier);
    QVERIFY(status.last().at(1).toBool()); // Missing clone source is an error, not a tool hint.
}

void VpControllerTest::qmlPropertiesNotifyAndClamp()
{
    VpController controller;
    QCOMPARE(controller.property("marqueeHealMode").toInt(), int(HealingBlend::On));
    QCOMPARE(controller.property("cloneHealMode").toInt(), int(HealingBlend::On));
    QVERIFY(!controller.property("cloneAligned").toBool());
    controller.document().appendPlane(makeTestPlane());
    QSignalSpy toolChanged(&controller, &VpController::toolChanged);
    QSignalSpy brushChanged(&controller, &VpController::brushChanged);
    QSignalSpy gridChanged(&controller, &VpController::gridSizeChanged);

    QVERIFY(controller.setProperty("tool", VpTools::Brush));
    QCOMPARE(controller.tool(), VpTools::Brush);
    QCOMPARE(toolChanged.size(), 1);
    QVERIFY(controller.setProperty("brushDiameter", 37));
    QCOMPARE(controller.brushDiameter(), 37);
    QCOMPARE(brushChanged.size(), 1);
    QVERIFY(controller.setProperty("gridSize", 0));
    QCOMPARE(controller.gridSize(), 1);
    QCOMPARE(gridChanged.size(), 1);
}

void VpControllerTest::toolAvailabilityFollowsDocument()
{
    VpController controller;
    auto &doc = controller.document();
    QSignalSpy availability(&controller, &VpController::toolAvailabilityChanged);
    QSignalSpy status(&controller, &VpController::statusMessage);
    for (auto tool : {VpTools::EditPlane, VpTools::Marquee,
                      VpTools::CloneStamp, VpTools::Brush, VpTools::Transform}) {
        QVERIFY(!controller.isToolEnabled(tool));
        controller.setTool(tool);
        QCOMPARE(controller.tool(), VpTools::CreatePlane);
    }
    QCOMPARE(status.size(), 5);
    QVERIFY(controller.isToolEnabled(VpTools::CreatePlane));
    QVERIFY(controller.isToolEnabled(VpTools::Hand));
    QVERIFY(controller.isToolEnabled(VpTools::Zoom));
    QVERIFY(!controller.beginBrush({30, 30}));
    QVERIFY(!controller.pickCloneSource({30, 30}));
    QVERIFY(!controller.beginClone({30, 30}));
    doc.beginEdit();
    QVERIFY(doc.appendPlane(makeTestPlane()) >= 0);
    doc.commitEdit(true);
    QCOMPARE(availability.size(), 1);
    for (auto tool : {VpTools::EditPlane, VpTools::Marquee,
                      VpTools::CloneStamp, VpTools::Brush}) {
        QVERIFY(controller.isToolEnabled(tool));
        controller.setTool(tool);
        QCOMPARE(controller.tool(), tool);
    }
    controller.undo();
    QVERIFY(!controller.isToolEnabled(VpTools::Brush));
    QCOMPARE(controller.tool(), VpTools::CreatePlane);
    controller.redo();
    QVERIFY(controller.isToolEnabled(VpTools::Brush));
    controller.setTool(VpTools::Brush);
    doc.removePlane(0);
    QCOMPARE(controller.tool(), VpTools::CreatePlane);

    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    doc.addFloatingSelection(makeTestSelection(image));
    QVERIFY(controller.isToolEnabled(VpTools::Transform));
    doc.setSelectedFloatingSelection(-1);
    controller.setTool(VpTools::Transform);
    QCOMPARE(controller.tool(), VpTools::Transform);
    QCOMPARE(doc.selectedFloatingSelection(), 0);
    controller.undo();
    QVERIFY(!controller.isToolEnabled(VpTools::Transform));
    QCOMPARE(controller.tool(), VpTools::CreatePlane);
    controller.redo();
    QVERIFY(controller.isToolEnabled(VpTools::Transform));
    doc.appendPlane(makeTestPlane());
    controller.setTool(VpTools::Transform);
    doc.removeFloatingSelection(0);
    QCOMPARE(controller.tool(), VpTools::EditPlane);
}

void VpControllerTest::clipboardPastePreservesPixelsAndCopyPriority()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    QClipboard *clipboard=QGuiApplication::clipboard();
    QImage external(30,20,QImage::Format_ARGB32);
    external.fill(QColor(160,80,30,128)); external.setPixelColor(0,0,Qt::transparent);
    clipboard->setImage(external);
    VpController controller;
    controller.pasteSelection();
    check(controller.document().floatingSelections().isEmpty(),"paste without loaded canvas is ignored");
    QImage background(300,200,QImage::Format_ARGB32_Premultiplied); background.fill(Qt::white);
    controller.document().setImage(background);
    QImage stale(10,10,QImage::Format_ARGB32_Premultiplied); stale.fill(Qt::green);
    controller.setCopiedCanvasSelection(stale);
    controller.pasteSelection();
    check(controller.document().floatingSelections().size()==1,"clipboard image becomes floating selection");
    QVERIFY(!controller.document().floatingSelections().isEmpty());
    const auto pasted=controller.document().floatingSelection(0);
    check(pasted.bitmap==external.convertToFormat(QImage::Format_ARGB32_Premultiplied),
          "clipboard pixels and alpha are preserved instead of stale canvas copy");
    check(!pasted.surfaceAttached && pasted.placementOrigin==QPointF() && pasted.scaleFactors==QPointF(1,1),
          "external paste starts detached at canvas origin without scaling");
    check(controller.document().selectedFloatingSelection()==0 && controller.tool()==VpTools::Marquee,
          "new floating selection is selected and editable without a plane");
    const QImage output=controller.resultImage();
    check(output.pixelColor(0,0).alpha()==0 && output.pixelColor(15,10).alpha()==128,
          "external alpha survives rendering");
    controller.undo(); check(controller.document().floatingSelections().isEmpty(),"paste undo is one step");
    controller.redo(); check(controller.document().floatingSelection(0)==pasted,"redo restores exact pasted selection");
    check(controller.isToolEnabled(VpTools::Marquee),
          "redo enables marquee for floating selection without planes");
    controller.setTool(VpTools::Marquee);
    controller.pointerPress(QPointF(15,10),1,Qt::NoModifier);
    controller.pointerMove(QPointF(45,40),Qt::NoModifier);
    controller.pointerRelease(QPointF(45,40),Qt::NoModifier);
    check(controller.document().floatingSelection(0).placementOrigin==QPointF(30,30),
          "marquee can move pasted image without planes");
    controller.undo();
    check(controller.document().floatingSelection(0)==pasted,"undo restores pasted image after move");

    auto edited=pasted; edited.placementOrigin=QPointF(40,30); edited.scaleFactors=QPointF(1.2,.8);
    edited.rotationDegrees=15; edited.opacity=65;
    controller.document().setFloatingSelection(0,edited); controller.copySelection(); controller.pasteSelection();
    check(controller.document().floatingSelection(1)==edited,"internal copy preserves transform and opacity");
    QImage newer(18,12,QImage::Format_ARGB32_Premultiplied); newer.fill(Qt::blue);
    clipboard->setPixmap(QPixmap::fromImage(newer)); controller.pasteSelection();
    check(controller.document().floatingSelections().size()==3
          && controller.document().floatingSelection(2).bitmap==newer
          && controller.document().floatingSelection(2).opacity==100,
          "new external clipboard image supersedes internal snapshot");
    clipboard->setText(QStringLiteral("not an image")); controller.pasteSelection();
    check(controller.document().floatingSelections().size()==3,"text clipboard does not paste stale image");
    clipboard->clear(); controller.setCopiedCanvasSelection(stale); controller.pasteSelection();
    check(controller.document().floatingSelection(3).bitmap==stale,"empty clipboard retains legacy canvas copy fallback");

    QImage textPixels(24,16,QImage::Format_ARGB32_Premultiplied);textPixels.fill(Qt::transparent);
    textPixels.setPixelColor(8,8,QColor(20,40,80,128));
    QImage flattened(textPixels.size(),QImage::Format_ARGB32_Premultiplied);flattened.fill(Qt::white);
    auto *nativeCopy=new QMimeData;
    nativeCopy->setImageData(flattened);
    nativeCopy->setData(QStringLiteral("application/x-qt-windows-mime;value=\"HIMIRAGE_CUSTOM_FORMAT1\""),QByteArray("object"));
    clipboard->setMimeData(nativeCopy);
    controller.setCopiedCanvasSelection(textPixels);
    controller.pasteSelection();
    check(controller.document().floatingSelection(4).bitmap==textPixels,
          "native text copy uses canvas pixels and alpha instead of white compatibility bitmap");
    clipboard->setImage(newer);controller.pasteSelection();
    check(controller.document().floatingSelection(5).bitmap==newer,
          "new external clipboard image supersedes native canvas snapshot");
}

void VpControllerTest::applyCallbackContract()
{
    VpController controller; int calls=0; QImage received;
    controller.setApplyHandler([&](const QImage &image) { ++calls; received=image; return true; });
    QVERIFY(!controller.applyChanges()); QCOMPARE(calls,0); QVERIFY(controller.resultImage().isNull());
    setupController(controller); controller.setApplyHandler({}); QVERIFY(!controller.applyChanges());
    auto &doc=controller.document(); doc.addFloatingSelection(controllerSelection()); doc.addFloatingSelection(controllerSelection(Qt::blue)); controller.undo();
    const auto floating=doc.floatingSelections(); const QImage paint=doc.paintLayer(),expected=controller.resultImage();
    controller.setApplyHandler([&](const QImage &image) { ++calls; received=image; return false; });
    QVERIFY(!controller.applyChanges()); QCOMPARE(calls,1); QCOMPARE(received,expected);
    QVERIFY(doc.floatingSelections()==floating); QCOMPARE(doc.paintLayer(),paint); QVERIFY(doc.canRedo());
    controller.setApplyHandler([&](const QImage &image) { ++calls; received=image; return true; });
    QVERIFY(controller.applyChanges()); QCOMPARE(calls,2); QCOMPARE(received,expected); QVERIFY(doc.canRedo());
    QVERIFY(doc.floatingSelections()==floating); controller.redo(); QCOMPARE(doc.floatingSelections().size(),2);
}

void VpControllerTest::outputContainsOnlyPersistentContent()
{
    VpController controller; setupController(controller); auto &doc=controller.document();
    const QImage empty=controllerImage(Qt::transparent); QCOMPARE(controller.resultImage(),empty);
    controller.setTool(VpTools::Marquee); controller.pointerPress({20,20},1,Qt::NoModifier); controller.pointerRelease({60,60},Qt::NoModifier);
    { QPainter painter(&doc.paintLayer()); painter.fillRect(80,80,80,60,QColor(255,0,0,128)); }
    doc.addFloatingSelection(controllerSelection(QColor(0,255,0,128))); auto upper=controllerSelection(Qt::blue,{110,110}); upper.opacity=50;
    doc.addFloatingSelection(upper); doc.setSelectedFloatingSelection(-1);
    QImage expected=empty;
    { QPainter painter(&expected); painter.drawImage(0,0,doc.paintLayer()); painter.fillRect(100,100,40,30,QColor(0,255,0,128));
      painter.setOpacity(.5); painter.fillRect(110,110,40,30,Qt::blue); }
    const auto floating=doc.floatingSelections(); const QImage paint=doc.paintLayer();
    QCOMPARE(controller.resultImage(),expected); QCOMPARE(qAlpha(expected.pixel(5,5)),0);
    controller.pointerPress({40,40},1,Qt::ControlModifier); controller.pointerMove({50,40},Qt::ControlModifier);
    QVERIFY(!controller.selectionPreview().bitmap.isNull()); QCOMPARE(controller.resultImage(),expected);
    QVERIFY(doc.floatingSelections()==floating); QCOMPARE(doc.paintLayer(),paint);
    controller.cancelInteraction(); QCOMPARE(controller.resultImage(),expected);
}

void VpControllerTest::applyingDuringInteraction_data()
{
    QTest::addColumn<int>("interaction"); const char *names[]={"brush","clone","plane","transform","fill"};
    for(int i=0;i<5;++i) QTest::newRow(names[i]) << i;
}

void VpControllerTest::applyingDuringInteraction()
{
    QFETCH(int,interaction); VpController controller; setupController(controller); auto &doc=controller.document();
    if(interaction==0) { controller.setTool(VpTools::Brush); controller.setBrushHealMode(1); QVERIFY(controller.beginBrush({40,40})); controller.moveBrush({70,40}); }
    if(interaction==1) { controller.setTool(VpTools::CloneStamp); controller.setCloneHealMode(1); QVERIFY(controller.pickCloneSource({20,20}));
                        QVERIFY(controller.beginClone({100,60})); controller.moveClone({120,60}); }
    if(interaction==2) { controller.setTool(VpTools::EditPlane); controller.pointerPress({200,200},1,Qt::NoModifier); controller.pointerMove({230,220},Qt::NoModifier); }
    if(interaction==3) { doc.addFloatingSelection(controllerSelection()); controller.setTool(VpTools::Transform);
                        controller.pointerPress({140,130},1,Qt::NoModifier); controller.pointerMove({160,150},Qt::NoModifier); }
    if(interaction==4) { controller.setTool(VpTools::Marquee); controller.pointerPress({20,20},1,Qt::NoModifier); controller.pointerRelease({60,60},Qt::NoModifier);
                        controller.pointerPress({40,40},1,Qt::ControlModifier); controller.pointerMove({50,40},Qt::ControlModifier); }
    QImage received; int calls=0;
    controller.setApplyHandler([&](const QImage &image) { ++calls; received=image; return true; });
    QVERIFY(controller.applyChanges()); QCOMPARE(calls,1); QVERIFY(!controller.brushDrawing()); QVERIFY(!controller.cloneDrawing());
    QCOMPARE(received,controller.resultImage());
    if(interaction<2) { QVERIFY(doc.canUndo()); controller.undo(); QVERIFY(!doc.hasPaintContent()); }
    else { QVERIFY(doc.editActive()); if(interaction==4) QCOMPARE(received,controllerImage(Qt::transparent)); controller.cancelInteraction(); QVERIFY(!doc.editActive()); }
}

void VpControllerTest::availabilityMatrix_data()
{
    QTest::addColumn<int>("state"); QTest::addColumn<int>("requested");
    for(int state=0;state<5;++state) for(int tool=VpTools::CreatePlane;tool<=VpTools::Eyedropper;++tool)
        QTest::newRow(qPrintable(QString("state%1-tool%2").arg(state).arg(tool))) << state << tool;
}

void VpControllerTest::availabilityMatrix()
{
    QFETCH(int,state); QFETCH(int,requested); VpController controller; auto &doc=controller.document();
    if(state>0) QVERIFY(doc.setImage(controllerImage()));
    if(state==2 || state==4) doc.appendPlane(controllerPlane());
    if(state==3 || state==4) doc.addFloatingSelection(controllerSelection());
    int expected=(1<<VpTools::CreatePlane)|(1<<VpTools::Hand)|(1<<VpTools::Zoom);
    if(state>0) expected|=1<<VpTools::Eyedropper;
    if(state==2 || state==4) expected|=(1<<VpTools::EditPlane)|(1<<VpTools::Marquee)|(1<<VpTools::Brush)|(1<<VpTools::CloneStamp);
    if(state==3 || state==4) expected|=(1<<VpTools::Transform)|(1<<VpTools::Marquee);
    QCOMPARE(controller.availableTools(),expected);
    const auto tool=static_cast<VpTools::Tool>(requested); const bool enabled=bool(expected&(1<<requested));
    QCOMPARE(controller.isToolEnabled(tool),enabled); const auto before=controller.tool();
    controller.setTool(tool); QCOMPARE(controller.tool(),enabled ? tool : before);
    QVERIFY(!controller.isToolEnabled(static_cast<VpTools::Tool>(-1))); QVERIFY(!controller.isToolEnabled(static_cast<VpTools::Tool>(100)));
}

void VpControllerTest::unavailableToolPreservesInteraction()
{
    VpController controller; setupController(controller); controller.setTool(VpTools::Brush); QVERIFY(controller.beginBrush({40,40}));
    const QImage before=controller.document().paintLayer(); QSignalSpy status(&controller,&VpController::statusMessage);
    controller.setTool(VpTools::Transform); QCOMPARE(controller.tool(),VpTools::Brush); QVERIFY(controller.brushDrawing());
    QCOMPARE(controller.document().paintLayer(),before); QCOMPARE(status.size(),1); QVERIFY(status.last()[1].toBool());
    controller.moveBrush({60,40}); controller.endBrush(); controller.undo(); QVERIFY(!controller.document().hasPaintContent());
}

void VpControllerTest::propertyNotifications_data()
{
    QTest::addColumn<QString>("name"); QTest::addColumn<int>("requested"); QTest::addColumn<int>("expected");
    const auto row=[](const char *name,int requested,int expected,const char *suffix) { QTest::newRow(qPrintable(QString::fromLatin1(name)+suffix)) << QString::fromLatin1(name) << requested << expected; };
    for(const char *name : {"brushDiameter","cloneDiameter"}) { row(name,-5,1,"-low"); row(name,999,500,"-high"); }
    for(const char *name : {"brushHardness","cloneHardness"}) { row(name,-5,0,"-low"); row(name,999,100,"-high"); }
    for(const char *name : {"brushOpacity","cloneOpacity"}) { row(name,0,1,"-low"); row(name,999,100,"-high"); }
    row("marqueeFeather",-1,0,"-low"); row("marqueeFeather",999,50,"-high");
    row("marqueeOpacity",-1,0,"-low"); row("marqueeOpacity",999,100,"-high");
    row("gridSize",0,1,"-low"); row("gridSize",9999,1000,"-high");
    for(const char *name : {"brushDiameter","cloneDiameter","brushHardness","cloneHardness","brushOpacity","cloneOpacity","marqueeOpacity","gridSize"}) row(name,37,37,"-valid");
    row("marqueeFeather",7,7,"-valid"); row("cloneAligned",1,1,"-enabled"); row("cloneAligned",0,0,"-unchanged");
    for(const char *name : {"brushHealMode","cloneHealMode","marqueeHealMode"}) {
        const int defaultMode=QString::fromLatin1(name)=="brushHealMode" ? 0 : 2;
        row(name,1,1,"-valid"); row(name,-1,defaultMode,"-invalid-low"); row(name,3,defaultMode,"-invalid-high");
    }
}

void VpControllerTest::propertyNotifications()
{
    QFETCH(QString,name); QFETCH(int,requested); QFETCH(int,expected); VpController controller;
    const QByteArray propertyName=name.toLatin1(); const auto property=controller.metaObject()->property(controller.metaObject()->indexOfProperty(propertyName.constData()));
    QSignalSpy notified(&controller,property.notifySignal()); const QVariant before=property.read(&controller);
    QSignalSpy brush(&controller,&VpController::brushChanged),clone(&controller,&VpController::cloneChanged),marquee(&controller,&VpController::marqueeChanged),grid(&controller,&VpController::gridSizeChanged);
    QVERIFY(property.write(&controller,requested)); QCOMPARE(property.read(&controller).toInt(),expected);
    const int count=before.toInt()==expected ? 0 : 1; QCOMPARE(notified.size(),count);
    QVERIFY(property.write(&controller,requested)); QVERIFY(property.write(&controller,expected)); QCOMPARE(notified.size(),count);
    const auto signal=property.notifySignal().name();
    QCOMPARE(brush.size(),signal=="brushChanged" ? count : 0); QCOMPARE(clone.size(),signal=="cloneChanged" ? count : 0);
    QCOMPARE(marquee.size(),signal=="marqueeChanged" ? count : 0); QCOMPARE(grid.size(),signal=="gridSizeChanged" ? count : 0);
    QCOMPARE(controller.document().canUndo(),false);
}

void VpControllerTest::dispatchPrefersTopmostAndHandles()
{
    VpController controller; setupController(controller); auto &doc=controller.document();
    doc.addFloatingSelection(controllerSelection(Qt::red)); doc.addFloatingSelection(controllerSelection(Qt::blue)); doc.setSelectedFloatingSelection(0); doc.resetHistory();
    controller.setTool(VpTools::Brush); controller.pointerPress({120,115},1,Qt::NoModifier);
    QCOMPARE(doc.selectedFloatingSelection(),1); QVERIFY(!controller.brushDrawing());
    controller.pointerRelease({135,125},Qt::NoModifier);
    QCOMPARE(doc.floatingSelection(0).placementOrigin,QPointF(100,100)); QCOMPARE(doc.floatingSelection(1).placementOrigin,QPointF(115,110));
    QVERIFY(!doc.hasPaintContent()); controller.undo(); QCOMPARE(doc.floatingSelection(1).placementOrigin,QPointF(100,100));
    controller.setTool(VpTools::Transform); doc.setSelectedFloatingSelection(1);
    controller.pointerPress({140,130},1,Qt::NoModifier); controller.pointerRelease({160,150},Qt::NoModifier);
    QCOMPARE(doc.floatingSelection(1).displaySize(),QSizeF(60,50)); QCOMPARE(doc.floatingSelection(1).placementOrigin,QPointF(100,100));
}

void VpControllerTest::bakeClickDoesNotDispatchToTool_data()
{
    QTest::addColumn<int>("tool"); for(auto tool : {VpTools::Brush,VpTools::CreatePlane,VpTools::Marquee}) QTest::newRow(qPrintable(QString::number(tool))) << int(tool);
}

void VpControllerTest::bakeClickDoesNotDispatchToTool()
{
    QFETCH(int,tool); VpController controller; setupController(controller); auto &doc=controller.document();
    doc.addFloatingSelection(controllerSelection(Qt::red,{20,20})); doc.addFloatingSelection(controllerSelection(Qt::blue,{20,20}));
    doc.addFloatingSelection(controllerSelection(Qt::green,{20,20})); doc.setSelectedFloatingSelection(1); doc.resetHistory();
    const QImage before=controller.resultImage(); controller.setTool(static_cast<VpTools::Tool>(tool));
    controller.pointerPress({250,200},1,Qt::NoModifier); controller.pointerRelease({250,200},Qt::NoModifier);
    QCOMPARE(controller.resultImage(),before); QCOMPARE(doc.floatingSelections().size(),1); QVERIFY(doc.hasPaintContent());
    QCOMPARE(qAlpha(doc.paintLayer().pixel(250,200)),0); QVERIFY(controller.creationPoints().isEmpty());
    QVERIFY(controller.selectionOutline().isEmpty()); QVERIFY(!controller.brushDrawing());
    controller.undo(); QCOMPARE(doc.floatingSelections().size(),3); QVERIFY(!doc.hasPaintContent()); QCOMPARE(controller.resultImage(),before);
}

void VpControllerTest::documentReplacementClearsInteraction_data()
{
    QTest::addColumn<int>("interaction"); const char *names[]={"creation","clone-source","fill","healing-brush","transform","plane"};
    for(int i=0;i<6;++i) QTest::newRow(names[i]) << i;
}

void VpControllerTest::documentReplacementClearsInteraction()
{
    QFETCH(int,interaction); VpController controller; setupController(controller); auto &doc=controller.document();
    QGuiApplication::clipboard()->clear(); controller.setCopiedCanvasSelection(controllerImage(Qt::blue,{10,10}));
    if(interaction==0) controller.pointerPress({20,20},1,Qt::NoModifier);
    if(interaction==1) { controller.setTool(VpTools::CloneStamp); QVERIFY(controller.pickCloneSource({20,20})); controller.hoverClone({30,30}); }
    if(interaction==2) { controller.setTool(VpTools::Marquee); controller.pointerPress({20,20},1,Qt::NoModifier); controller.pointerRelease({60,60},Qt::NoModifier);
                        controller.pointerPress({40,40},1,Qt::ControlModifier); controller.pointerMove({50,40},Qt::ControlModifier); }
    if(interaction==3) { controller.setTool(VpTools::Brush); controller.setBrushHealMode(1); controller.beginBrush({40,40}); controller.moveBrush({60,40}); }
    if(interaction==4) { doc.addFloatingSelection(controllerSelection()); controller.setTool(VpTools::Transform);
                        controller.pointerPress({140,130},1,Qt::NoModifier); controller.pointerMove({160,150},Qt::NoModifier); }
    if(interaction==5) { controller.setTool(VpTools::EditPlane); controller.pointerPress({200,200},1,Qt::NoModifier); controller.pointerMove({220,210},Qt::NoModifier); }
    const auto points=controller.creationPoints(); const QImage output=controller.resultImage(); const bool editing=doc.editActive();
    QSignalSpy replaced(&controller,&VpController::documentReplaced);
    QVERIFY(!doc.setImage({})); QCOMPARE(replaced.size(),0); QCOMPARE(controller.creationPoints(),points); QCOMPARE(controller.resultImage(),output); QCOMPARE(doc.editActive(),editing);
    const QImage replacement=controllerImage(Qt::yellow,{96,80}); QVERIFY(doc.setImage(replacement)); QCOMPARE(replaced.size(),1);
    QVERIFY(controller.creationPoints().isEmpty()); QVERIFY(controller.selectionOutline().isEmpty()); QVERIFY(controller.selectionPreview().bitmap.isNull());
    QVERIFY(!controller.hasCloneSource()); QVERIFY(!controller.brushDrawing()); QVERIFY(!controller.cloneDrawing()); QVERIFY(controller.interactionHint().isEmpty());
    QVERIFY(!doc.editActive()); QVERIFY(!doc.canUndo()); QVERIFY(!doc.canRedo()); QVERIFY(doc.floatingSelections().isEmpty()); QVERIFY(doc.planes().isEmpty());
    QCOMPARE(doc.paintLayer(),controllerImage(Qt::transparent,replacement.size()).convertToFormat(QImage::Format_ARGB32));
    controller.pointerMove({50,50},Qt::NoModifier); controller.pointerRelease({60,50},Qt::NoModifier); QVERIFY(!doc.hasPaintContent()); QVERIFY(!doc.canUndo());
    controller.pasteSelection(); QVERIFY(doc.floatingSelections().isEmpty()); // No copy from the previous document.
}

void VpControllerTest::clipboardFailuresPreserveRedo()
{
    VpController controller; setupController(controller); auto &doc=controller.document();
    doc.addFloatingSelection(controllerSelection()); doc.addFloatingSelection(controllerSelection(Qt::blue)); controller.undo();
    doc.setSelectedFloatingSelection(-1); const QImage before=controller.resultImage(); QSignalSpy status(&controller,&VpController::statusMessage);
    controller.copySelection(); QVERIFY(!status.isEmpty()); QVERIFY(status.last()[1].toBool());
    QGuiApplication::clipboard()->setText("not an image"); controller.pasteSelection(); QCOMPARE(controller.resultImage(),before); QVERIFY(doc.canRedo());
    QGuiApplication::clipboard()->clear(); controller.pasteSelection(); QCOMPARE(controller.resultImage(),before); QVERIFY(doc.canRedo());
    controller.redo(); QCOMPARE(doc.floatingSelections().size(),2);
    controller.copySelection(); const auto copied=doc.floatingSelection(1); controller.pasteSelection(); QCOMPARE(doc.floatingSelection(2),copied);
    const QImage external=controllerImage(Qt::green,{8,8}); QGuiApplication::clipboard()->setImage(external); controller.pasteSelection();
    QCOMPARE(doc.floatingSelection(3).bitmap,external);
}

void VpControllerTest::deletePriorityAndOptionSynchronization()
{
    VpController controller; setupController(controller); auto &doc=controller.document();
    auto image=controllerSelection(); image.opacity=35; image.feather=4; image.healMode=1; image.flippedHorizontally=true;
    doc.addFloatingSelection(image); controller.setTool(VpTools::Marquee);
    QCOMPARE(controller.marqueeOpacity(),35); QCOMPARE(controller.marqueeFeather(),4); QCOMPARE(controller.marqueeHealMode(),1);
    controller.setTool(VpTools::Transform); QVERIFY(controller.flippedHorizontally());
    doc.setSelectedPlane(0); doc.resetHistory(); controller.deleteSelection(); QVERIFY(doc.floatingSelections().isEmpty()); QCOMPARE(doc.planes().size(),1);
    QCOMPARE(controller.tool(),VpTools::EditPlane); QVERIFY(!controller.transformEditable()); QVERIFY(!controller.flippedHorizontally());
    controller.undo(); QVERIFY(controller.transformEditable()); QVERIFY(controller.flippedHorizontally());
    controller.setTool(VpTools::Marquee); QCOMPARE(controller.marqueeOpacity(),35);
    controller.redo(); QVERIFY(!controller.transformEditable());
    controller.setTool(VpTools::CreatePlane); controller.pointerPress({20,20},1,Qt::NoModifier); doc.setSelectedPlane(0);
    controller.deleteSelection(); QVERIFY(controller.creationPoints().isEmpty()); QCOMPARE(doc.planes().size(),1);
}

void VpControllerTest::notificationsExposeConsistentState()
{
    VpController controller; setupController(controller); auto &doc=controller.document(); bool consistent=true;
    connect(&controller,&VpController::toolChanged,&controller,[&] { consistent &= controller.isToolEnabled(controller.tool()) && !controller.toolHint().isEmpty(); });
    connect(&controller,&VpController::transformOptionsChanged,&controller,[&] {
        consistent &= controller.transformEditable()==(doc.selectedFloatingSelection()>=0);
        if(controller.transformEditable()) consistent &= controller.flippedHorizontally()==doc.floatingSelection(doc.selectedFloatingSelection()).flippedHorizontally;
    });
    connect(&controller,&VpController::toolAvailabilityChanged,&controller,[&] {
        for(int tool=VpTools::CreatePlane;tool<=VpTools::Eyedropper;++tool)
            consistent &= controller.isToolEnabled(static_cast<VpTools::Tool>(tool))==bool(controller.availableTools()&(1<<tool));
    });
    doc.addFloatingSelection(controllerSelection()); controller.setTool(VpTools::Transform); controller.setFlippedHorizontally(true);
    controller.undo(); controller.redo(); controller.deleteSelection(); QVERIFY(consistent);
    QVERIFY(doc.setImage(controllerImage(Qt::green))); QVERIFY(consistent);
}

QTEST_MAIN(VpControllerTest)
#include "tst_vpcontroller.moc"
