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
    void removesSelectedPlane();
    void removesPlaneReleasesSharedEdge();
    void planeLinkageTracksSharedEdge();
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

// 删除选中的平面：平面数减一，选中项落到相邻平面，且可撤销。
void CanvasDocumentTest::removesSelectedPlane()
{
    CanvasDocument document;
    document.beginEdit();
    document.appendPlane(makeTestPlane());
    document.appendPlane(makeTestPlane());
    document.setSelectedPlane(1);
    document.commitEdit(true);

    document.removePlane(1);
    QCOMPARE(document.planes().size(), 1);
    QCOMPARE(document.selectedPlane(), 0);
    QVERIFY(document.undo());
    QCOMPARE(document.planes().size(), 2);
}

// 删平面按 parentPlane / parentEdge 解除共用边的锁定，不依赖两端点是否仍然几何重合。
void CanvasDocumentTest::removesPlaneReleasesSharedEdge()
{
    CanvasDocument document;
    document.beginEdit();
    Plane parent = makeTestPlane();              // 父平面：下标 0
    parent.lockedEdges = quint8(1u << 1);
    document.appendPlane(parent);
    Plane child = makeTestPlane();               // 子平面：下标 1
    child.parentPlane = 0;
    child.parentEdge = 1;
    child.lockedEdges = quint8(1u << 0);         // 第 0 条边是与父平面共用的边
    document.appendPlane(child);
    document.commitEdit(true);

    document.removePlane(0);                     // 删父平面：子平面解除父子关系并解锁共用边
    QCOMPARE(document.planes().size(), 1);
    QCOMPARE(document.planes()[0].parentPlane, -1);
    QCOMPARE(document.planes()[0].parentEdge, -1);
    QCOMPARE(int(document.planes()[0].lockedEdges), 0);

    QVERIFY(document.undo());
    document.removePlane(1);                     // 删子平面：解锁父平面上被它共用的那条边
    QCOMPARE(document.planes().size(), 1);
    QCOMPARE(document.planes()[0].parentPlane, -1);
    QCOMPARE(int(document.planes()[0].lockedEdges), 0);
}

// 共边关系：父子平面互为共边，独立平面不算共边；删掉子平面后父平面解除共边。
void CanvasDocumentTest::planeLinkageTracksSharedEdge()
{
    CanvasDocument document;
    document.beginEdit();
    document.appendPlane(makeTestPlane());       // 父平面：下标 0
    Plane child = makeTestPlane();               // 子平面：下标 1
    child.parentPlane = 0;
    child.parentEdge = 1;
    document.appendPlane(child);
    document.appendPlane(makeTestPlane());       // 独立平面：下标 2
    document.commitEdit(true);

    QVERIFY(document.isPlaneLinked(0));
    QVERIFY(document.isPlaneLinked(1));
    QVERIFY(!document.isPlaneLinked(2));
    QVERIFY(!document.isPlaneLinked(-1));

    document.removePlane(1);                     // 父子关系随之解除
    QVERIFY(!document.isPlaneLinked(0));
}

QTEST_APPLESS_MAIN(CanvasDocumentTest)
#include "tst_canvasdocument.moc"
