#include "canvas/vpcontroller.h"
#include "testhelpers.h"
#include <QFile>
#include <QImageReader>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

class ImageSavingTest : public QObject
{
    Q_OBJECT
private slots:
    void rejectsInvalidRequests();
    void savesCompositeWithoutGuidesOrChangingSelections();
    void formats_data();
    void formats();
    void pathsAndFailures();
    void finishesActiveStroke();
};

void ImageSavingTest::rejectsInvalidRequests()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    VpController controller;
    QSignalSpy status(&controller, &VpController::statusMessage);
    QVERIFY(!controller.canSave());
    QVERIFY(!controller.saveImage(QUrl::fromLocalFile(dir.filePath("empty.png"))));
    QVERIFY(!QFile::exists(dir.filePath("empty.png")));
    QVERIFY(status.last().at(1).toBool());
    QImage background(64,48,QImage::Format_ARGB32); background.fill(Qt::white);
    QVERIFY(controller.document().setImage(background));
    QVERIFY(controller.canSave());
    QVERIFY(!controller.saveImage(QUrl("https://example.com/image.png")));
    QVERIFY(!controller.saveImage(QUrl()));
    QVERIFY(controller.savedImageUrl().isEmpty());
    QVERIFY(status.last().at(1).toBool());
}

void ImageSavingTest::savesCompositeWithoutGuidesOrChangingSelections()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    VpController controller; auto &doc = controller.document();
    QImage background(64,48,QImage::Format_ARGB32_Premultiplied);
    background.fill(Qt::transparent);
    { QPainter p(&background); p.fillRect(QRect(4,4,55,40),Qt::white); }
    QVERIFY(doc.setImage(background));
    { QPainter p(&doc.paintLayer()); p.fillRect(QRect(12,12,10,10),Qt::red); }
    const PerspectiveQuad::Corners corners{{{8,8},{56,8},{56,40},{8,40}}};
    doc.appendPlane(PerspectivePlane(corners,corners));
    FloatingSelection floating;
    floating.bitmap = QImage(8,8,QImage::Format_ARGB32_Premultiplied);
    floating.bitmap.fill(Qt::blue);
    floating.placementOrigin = QPointF(38,20); floating.opacity = 50;
    doc.addFloatingSelection(floating);
    doc.setSelectedFloatingSelection(-1);
    controller.setTool(VpTools::Marquee);
    controller.pointerPress({12,12},1,Qt::NoModifier);
    controller.pointerRelease({24,24},Qt::NoModifier);
    QVERIFY(!controller.selectionRect().isEmpty());
    doc.setSelectedPlane(0);
    const QImage paintBefore = doc.paintLayer();
    const QRectF selectionBefore = controller.selectionRect();
    const bool undoBefore = doc.canUndo();
    QSignalSpy history(&controller, &VpController::historyChanged);
    QSignalSpy structure(&doc, &VpDocument::structureChanged);
    QImage expected = background;
    { QPainter p(&expected); p.drawImage(QPoint(),paintBefore);
      p.setOpacity(.5); p.drawImage(floating.placementOrigin,floating.bitmap); }
    const QString path = dir.filePath("composite.png");
    QVERIFY(controller.saveImage(QUrl::fromLocalFile(path)));
    const QImage saved(path); QVERIFY(!saved.isNull());
    QCOMPARE(saved.convertToFormat(QImage::Format_ARGB32),expected.convertToFormat(QImage::Format_ARGB32));
    QCOMPARE(saved.pixelColor(0,0).alpha(),0);
    QCOMPARE(doc.paintLayer(),paintBefore);
    QCOMPARE(doc.floatingSelections().size(),1);
    QCOMPARE(doc.floatingSelection(0),floating);
    QCOMPARE(doc.selectedFloatingSelection(),-1);
    QCOMPARE(doc.planes().size(),1); QCOMPARE(doc.selectedPlane(),0);
    QCOMPARE(controller.selectionRect(),selectionBefore);
    QCOMPARE(doc.canUndo(),undoBefore);
    QVERIFY(history.isEmpty()); QVERIFY(structure.isEmpty());
}

void ImageSavingTest::formats_data()
{
    QTest::addColumn<QString>("suffix");
    QTest::addColumn<bool>("transparent");
    QTest::newRow("png") << QString("png") << true;
    QTest::newRow("jpg") << QString("jpg") << false;
    QTest::newRow("jpeg") << QString("jpeg") << false;
    QTest::newRow("bmp") << QString("bmp") << false;
}

void ImageSavingTest::formats()
{
    QFETCH(QString,suffix); QFETCH(bool,transparent);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    VpController controller;
    QImage image(64,48,QImage::Format_ARGB32); image.fill(Qt::transparent);
    { QPainter p(&image); p.fillRect(QRect(24,16,20,20),Qt::red); }
    QVERIFY(controller.document().setImage(image));
    const QString path = dir.filePath("image." + suffix);
    QVERIFY(controller.saveImage(QUrl::fromLocalFile(path)));
    const QImage saved(path); QVERIFY(!saved.isNull());
    QCOMPARE(saved.size(),image.size());
    const QColor corner = saved.pixelColor(0,0);
    if (transparent) QCOMPARE(corner.alpha(),0);
    else QVERIFY(corner.alpha()==255 && corner.red()>245 && corner.green()>245 && corner.blue()>245);
    const QColor red = saved.pixelColor(32,24);
    QVERIFY(red.red()>240 && red.green()<15 && red.blue()<15);
    QCOMPARE(controller.savedImageUrl(),QUrl::fromLocalFile(path));
}

void ImageSavingTest::pathsAndFailures()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QImage image(32,24,QImage::Format_ARGB32); image.fill(Qt::white);
    const QString original = dir.filePath("original.png"); QVERIFY(image.save(original));
    VpController controller;
    QVERIFY(controller.openImage(QUrl::fromLocalFile(original)));
    QCOMPARE(controller.suggestedSaveUrl(),QUrl::fromLocalFile(dir.filePath("original_edited.png")));
    QSignalSpy savedChanged(&controller, &VpController::savedImageUrlChanged);
    const QString path = dir.filePath("export");
    QVERIFY(controller.saveImage(QUrl::fromLocalFile(path)));
    QVERIFY(QFile::exists(path + ".png"));
    QCOMPARE(controller.savedImageUrl(),QUrl::fromLocalFile(path + ".png"));
    QCOMPARE(controller.suggestedSaveUrl(),controller.savedImageUrl());
    QCOMPARE(savedChanged.size(),1);
    const QUrl successfulTarget = controller.savedImageUrl();
    // Unsupported formats and unwritable paths cannot replace the successful target.
    const QString unsupported = dir.filePath("preserve.xyz");
    { QFile file(unsupported); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("keep these bytes"); }
    QVERIFY(!controller.saveImage(QUrl::fromLocalFile(unsupported)));
    { QFile file(unsupported); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),QByteArray("keep these bytes")); }
    QVERIFY(!controller.saveImage(QUrl::fromLocalFile(dir.filePath("missing/export.png"))));
    QCOMPARE(controller.savedImageUrl(),successfulTarget);
    QCOMPARE(savedChanged.size(),1);
    QCOMPARE(QImage(original),image); // The default export does not overwrite the opened image.
    QVERIFY(controller.openImage(QUrl::fromLocalFile(original)));
    QVERIFY(controller.savedImageUrl().isEmpty());
    QCOMPARE(savedChanged.size(),2);
}

void ImageSavingTest::finishesActiveStroke()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    VpController controller;
    QImage image(120,90,QImage::Format_ARGB32); image.fill(Qt::white);
    QVERIFY(controller.document().setImage(image));
    controller.document().appendPlane(makeTestPlane());
    controller.document().resetHistory();
    controller.setTool(VpTools::Brush); controller.setBrushColor(Qt::red);
    controller.setBrushHealMode(0); controller.setBrushHardness(100);
    QVERIFY(controller.beginBrush({30,30}));
    QVERIFY(controller.brushDrawing());
    const QString path = dir.filePath("stroke.png");
    QVERIFY(controller.saveImage(QUrl::fromLocalFile(path)));
    QVERIFY(!controller.brushDrawing());
    QCOMPARE(QImage(path).pixelColor(30,30),QColor(Qt::red));
    QVERIFY(controller.canUndo());
    controller.undo();
    QVERIFY(!controller.document().hasPaintContent());
    QVERIFY(!controller.canUndo());
}

QTEST_MAIN(ImageSavingTest)
#include "tst_imagesaving.moc"
