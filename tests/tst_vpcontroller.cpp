#include <QtTest>
#include <QPainter>
#include <QTemporaryDir>

#include "canvas/vpcontroller.h"
#include "testhelpers.h"

class VpControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void qmlPropertiesNotifyAndClamp();
    void toolAvailabilityFollowsDocument();
    void brushStrokeCommitsAsOneEdit();
    void cloneStrokeUsesDocumentContent();
};

void VpControllerTest::qmlPropertiesNotifyAndClamp()
{
    VpController controller;
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
    doc.addFloatingImage(image);
    QVERIFY(controller.isToolEnabled(VpTools::Transform));
    doc.setSelectedFloatingImage(-1);
    controller.setTool(VpTools::Transform);
    QCOMPARE(controller.tool(), VpTools::Transform);
    QCOMPARE(doc.selectedFloatingImage(), 0);
    controller.undo();
    QVERIFY(!controller.isToolEnabled(VpTools::Transform));
    QCOMPARE(controller.tool(), VpTools::CreatePlane);
    controller.redo();
    QVERIFY(controller.isToolEnabled(VpTools::Transform));
    doc.appendPlane(makeTestPlane());
    controller.setTool(VpTools::Transform);
    doc.removeFloatingImage(0);
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
    QVERIFY(controller.document().loadImage(path));
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
    QVERIFY(controller.document().loadImage(path));
    controller.document().appendPlane(makeTestPlane());
    controller.document().resetHistory();
    controller.setCloneDiameter(8);
    controller.setCloneHardness(100);
    QVERIFY(controller.pickCloneSource(QPointF(20, 20)));
    QVERIFY(controller.beginClone(QPointF(60, 60)));
    controller.endClone();

    QCOMPARE(QColor(controller.document().paintLayer().pixel(60, 60)).rgb(), QColor(Qt::red).rgb());
    QVERIFY(controller.document().undo());
    QCOMPARE(qAlpha(controller.document().paintLayer().pixel(60, 60)), 0);
}

QTEST_GUILESS_MAIN(VpControllerTest)
#include "tst_vpcontroller.moc"
