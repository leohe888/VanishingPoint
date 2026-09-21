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
    void pastesImageAsSelectedFloatingImage();
    void removesSelectedFloatingImage();
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

// 删平面按 parentPlaneIndex / parentEdgeIndex 解除共用边的锁定，不依赖两端点是否仍然几何重合。
void CanvasDocumentTest::removesPlaneReleasesSharedEdge()
{
    CanvasDocument document;
    document.beginEdit();
    PerspectivePlane parent = makeTestPlane();              // 父平面：下标 0
    parent.setEdgeLocked(1, true);
    document.appendPlane(parent);
    PerspectivePlane child = makeTestPlane();               // 子平面：下标 1
    child.setParent(0, 1);
    child.setEdgeLocked(0, true);              // 第 0 条边是与父平面共用的边
    document.appendPlane(child);
    document.commitEdit(true);

    document.removePlane(0);                     // 删父平面：子平面解除父子关系并解锁共用边
    QCOMPARE(document.planes().size(), 1);
    QCOMPARE(document.planes()[0].parentPlaneIndex(), -1);
    QCOMPARE(document.planes()[0].parentEdgeIndex(), -1);
    QCOMPARE(int(document.planes()[0].lockedEdgeMask()), 0);

    QVERIFY(document.undo());
    document.removePlane(1);                     // 删子平面：解锁父平面上被它共用的那条边
    QCOMPARE(document.planes().size(), 1);
    QCOMPARE(document.planes()[0].parentPlaneIndex(), -1);
    QCOMPARE(int(document.planes()[0].lockedEdgeMask()), 0);
}

// 共边关系：父子平面互为共边，独立平面不算共边；删掉子平面后父平面解除共边。
void CanvasDocumentTest::planeLinkageTracksSharedEdge()
{
    CanvasDocument document;
    document.beginEdit();
    document.appendPlane(makeTestPlane());       // 父平面：下标 0
    PerspectivePlane child = makeTestPlane();               // 子平面：下标 1
    child.setParent(0, 1);
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

// 粘贴：图像追加到画布左上角、未吸附，并自动成为选中项；可撤销。
void CanvasDocumentTest::pastesImageAsSelectedFloatingImage()
{
    CanvasDocument document;
    QImage image(32, 24, QImage::Format_ARGB32);
    image.fill(Qt::red);

    QCOMPARE(document.addFloatingImage(image), 0);
    QCOMPARE(document.floatingImages().size(), 1);
    QCOMPARE(document.selectedFloatingImage(), 0);
    QCOMPARE(document.floatingImage(0).bitmap.size(), QSize(32, 24));
    QCOMPARE(document.floatingImage(0).placementOrigin, QPointF(0, 0));
    QVERIFY(!document.floatingImage(0).surfaceAttached);

    QVERIFY(document.undo());
    QVERIFY(document.floatingImages().isEmpty());
    QCOMPARE(document.selectedFloatingImage(), -1);
}

// 删除选中的浮动图像：选中项落到删除位置上的下一张，且可撤销。
void CanvasDocumentTest::removesSelectedFloatingImage()
{
    CanvasDocument document;
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    document.addFloatingImage(image);
    document.addFloatingImage(image);
    document.setSelectedFloatingImage(0);

    document.removeFloatingImage(0);
    QCOMPARE(document.floatingImages().size(), 1);
    QCOMPARE(document.selectedFloatingImage(), 0);

    QVERIFY(document.undo());
    QCOMPARE(document.floatingImages().size(), 2);
}

QTEST_APPLESS_MAIN(CanvasDocumentTest)
#include "tst_canvasdocument.moc"
