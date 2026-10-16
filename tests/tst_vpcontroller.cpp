#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QTemporaryDir>
#include <QtTest>

#include "canvas/vpcontroller.h"
#include "testhelpers.h"

class VpControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void persistentHintsFollowInteractionLifecycle();
    void clipboardPastePreservesPixelsAndCopyPriority();
    void qmlPropertiesNotifyAndClamp();
    void toolAvailabilityFollowsDocument();
    void brushStrokeCommitsAsOneEdit();
    void cloneStrokeUsesDocumentContent();
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

void VpControllerTest::brushStrokeCommitsAsOneEdit()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage background(100, 100, QImage::Format_ARGB32);
    background.fill(Qt::white);
    const QString path = directory.filePath("background.png");
    QVERIFY(background.save(path));

    VpController controller;
    QVERIFY(controller.document().setImage(QImage(path)));
    controller.document().appendPlane(makeTestPlane());
    controller.document().resetHistory();
    QVERIFY(controller.beginBrush(QPointF(30, 30)));
    controller.moveBrush(QPointF(60, 30));
    controller.endBrush();

    QVERIFY(controller.document().canUndo());
    QVERIFY(qAlpha(controller.document().paintLayer().pixel(30, 30)) > 0);
    QVERIFY(qAlpha(controller.document().paintLayer().pixel(50, 30)) > 0);
    QVERIFY(controller.document().undo());
    QCOMPARE(qAlpha(controller.document().paintLayer().pixel(30, 30)), 0);
    QCOMPARE(qAlpha(controller.document().paintLayer().pixel(50, 30)), 0);
}

void VpControllerTest::cloneStrokeUsesDocumentContent()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QImage background(100, 100, QImage::Format_ARGB32);
    background.fill(Qt::white);
    {
        QPainter painter(&background);
        painter.fillRect(10, 10, 20, 20, Qt::red);
    }
    const QString path = directory.filePath("background.png");
    QVERIFY(background.save(path));

    VpController controller;
    QVERIFY(controller.document().setImage(QImage(path)));
    controller.document().appendPlane(makeTestPlane());
    controller.document().resetHistory();
    controller.setCloneDiameter(8);
    controller.setCloneHardness(100);
    controller.setCloneHealMode(HealingBlend::Off); // 此测试验证原始取样像素与撤销。
    QVERIFY(controller.pickCloneSource(QPointF(20, 20)));
    QVERIFY(controller.beginClone(QPointF(60, 60)));
    controller.endClone();

    QCOMPARE(QColor(controller.document().paintLayer().pixel(60, 60)).rgb(), QColor(Qt::red).rgb());
    QVERIFY(controller.document().undo());
    QCOMPARE(qAlpha(controller.document().paintLayer().pixel(60, 60)), 0);
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

QTEST_MAIN(VpControllerTest)
#include "tst_vpcontroller.moc"
