#include <QtTest>
#include <QTemporaryDir>

#include "core/canvasdocument.h"
#include "testhelpers.h"

class CanvasDocumentTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsInvalidPlane();
    void commitsUndoAndRedo();
    void cancelEditRestoresPlane();
    void loadingImageResetsDocument();
};

void CanvasDocumentTest::rejectsInvalidPlane()
{
    CanvasDocument document;
    QCOMPARE(document.appendPlane(makeSelfIntersectingPlane()), -1);
    QVERIFY(document.planes().isEmpty());
}

void CanvasDocumentTest::commitsUndoAndRedo()
{
    CanvasDocument document;
    document.beginEdit();
    QCOMPARE(document.appendPlane(makeTestPlane()), 0);
    document.setSelectedPlane(0);
    document.commitEdit(true);

    QCOMPARE(document.planes().size(), 1);
    QVERIFY(document.canUndo());
    QVERIFY(document.undo());
    QVERIFY(document.planes().isEmpty());
    QVERIFY(document.canRedo());
    QVERIFY(document.redo());
    QCOMPARE(document.planes().size(), 1);
    QCOMPARE(document.selectedPlane(), 0);
}

void CanvasDocumentTest::cancelEditRestoresPlane()
{
    CanvasDocument document;
    document.beginEdit();
    document.appendPlane(makeTestPlane());
    document.cancelEdit();
    QVERIFY(document.planes().isEmpty());
    QVERIFY(!document.canUndo());
}

void CanvasDocumentTest::loadingImageResetsDocument()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("background.png"));
    QImage image(64, 48, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QVERIFY(image.save(path));

    CanvasDocument document;
    document.beginEdit();
    document.appendPlane(makeTestPlane());
    document.commitEdit(true);
    QVERIFY(document.loadImage(path));

    QCOMPARE(document.background().size(), QSize(64, 48));
    QCOMPARE(document.paintLayer().size(), QSize(64, 48));
    QVERIFY(document.planes().isEmpty());
    QVERIFY(!document.canUndo());
}

QTEST_APPLESS_MAIN(CanvasDocumentTest)
#include "tst_canvasdocument.moc"
