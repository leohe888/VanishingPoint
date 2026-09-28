#include <QtTest>
#include <QPainter>
#include <QTemporaryDir>

#include "canvas/vpcontroller.h"

class VpControllerTest : public QObject
{
    Q_OBJECT

private slots:
    void qmlPropertiesNotifyAndClamp();
    void transformRequiresSelectedImage();
    void brushStrokeCommitsAsOneEdit();
    void cloneStrokeUsesDocumentContent();
};

void VpControllerTest::qmlPropertiesNotifyAndClamp()
{
    VpController controller;
    QSignalSpy toolChanged(&controller, &VpController::toolChanged);
    QSignalSpy brushChanged(&controller, &VpController::brushChanged);
    QSignalSpy gridChanged(&controller, &VpController::gridSizeChanged);

    QVERIFY(controller.setProperty("tool", VpController::Brush));
    QCOMPARE(controller.tool(), VpController::Brush);
    QCOMPARE(toolChanged.size(), 1);
    QVERIFY(controller.setProperty("brushDiameter", 37));
    QCOMPARE(controller.brushDiameter(), 37);
    QCOMPARE(brushChanged.size(), 1);
    QVERIFY(controller.setProperty("gridSize", 0));
    QCOMPARE(controller.gridSize(), 1);
    QCOMPARE(gridChanged.size(), 1);
}

void VpControllerTest::transformRequiresSelectedImage()
{
    VpController controller;
    QSignalSpy status(&controller, &VpController::statusMessage);
    controller.setTool(VpController::Transform);
    QCOMPARE(controller.tool(), VpController::CreatePlane);
    QCOMPARE(status.size(), 1);
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
