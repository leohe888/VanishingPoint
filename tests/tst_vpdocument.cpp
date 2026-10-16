#include <QCoreApplication>
#include <QTemporaryDir>
#include <QtTest>

#include "canvas/vpcontroller.h"
#include "canvas/vpdocument.h"
#include "testhelpers.h"

class VpDocumentTest : public QObject
{
    Q_OBJECT

private slots:
    void deletesDescendantsAndRemapsSharedEdges();
    void rejectsInvalidPlane();
    void commitsUndoAndRedo();
    void cancelEditRestoresPlane();
    void loadingImageResetsDocument();
    void removesSelectedPlane();
    void removesPlaneReleasesSharedEdge();
    void planeLinkageTracksSharedEdge();
    void pastesImageAsSelectedFloatingSelection();
    void removesSelectedFloatingSelection();
    void sharedEdgeExtensionRequiresExplicitPermission();
};

void VpDocumentTest::sharedEdgeExtensionRequiresExplicitPermission()
{
    VpDocument document;
    auto original = makeTestPlane(); original.setEdgeLocked(0, true);
    document.appendPlane(original); document.resetHistory();
    const auto extended = resizePlaneFromEdge(original, 1, {130,45}, {110,45});
    QVERIFY(extended.quad().canvasCorners() != original.quad().canvasCorners());
    QVERIFY(!document.setPlane(0, extended));
    document.beginEdit(); QVERIFY(document.setPlane(0, extended, true));
    document.cancelEdit(); QVERIFY(document.planes()[0] == original);
    QVERIFY(!document.canUndo());
    auto bent = extended; bent.quad().setCanvasCorner(1, {130,20});
    QVERIFY(!document.setPlane(0, bent, true));
    QVERIFY(document.planes()[0] == original);
}

void VpDocumentTest::rejectsInvalidPlane()
{
    VpDocument document;
    QCOMPARE(document.appendPlane(makeSelfIntersectingPlane()), -1);
    QVERIFY(document.planes().isEmpty());
}

void VpDocumentTest::commitsUndoAndRedo()
{
    VpDocument document;
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

void VpDocumentTest::cancelEditRestoresPlane()
{
    VpDocument document;
    document.beginEdit();
    document.appendPlane(makeTestPlane());
    document.cancelEdit();
    QVERIFY(document.planes().isEmpty());
    QVERIFY(!document.canUndo());
}

void VpDocumentTest::loadingImageResetsDocument()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("background.png"));
    QImage image(64, 48, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QVERIFY(image.save(path));

    VpDocument document;
    document.beginEdit();
    document.appendPlane(makeTestPlane());
    document.commitEdit(true);
    QVERIFY(document.setImage(QImage(path)));

    QCOMPARE(document.background().size(), QSize(64, 48));
    QCOMPARE(document.paintLayer().size(), QSize(64, 48));
    QVERIFY(document.planes().isEmpty());
    QVERIFY(!document.canUndo());
}

// 删除选中的平面：平面数减一，选中项落到相邻平面，且可撤销。
void VpDocumentTest::removesSelectedPlane()
{
    VpDocument document;
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

// 当前模块删除父平面及全部后代；删除子平面则解除剩余父平面的共边锁定。
void VpDocumentTest::removesPlaneReleasesSharedEdge()
{
    VpDocument document;
    document.beginEdit();
    PerspectivePlane parent = makeTestPlane();              // 父平面：下标 0
    parent.setEdgeLocked(1, true);
    document.appendPlane(parent);
    PerspectivePlane child = makeTestPlane();               // 子平面：下标 1
    child.setParent(0, 1);
    child.setEdgeLocked(0, true);              // 第 0 条边是与父平面共用的边
    document.appendPlane(child);
    document.commitEdit(true);

    document.removePlane(0);
    QVERIFY(document.planes().isEmpty());

    QVERIFY(document.undo());
    QCOMPARE(document.planes().size(), 2);
    QCOMPARE(document.planes()[1].parentPlaneIndex(), 0);
    QVERIFY(document.planes()[0].isEdgeLocked(1));
    document.removePlane(1);                     // 删子平面：解锁父平面上被它共用的那条边
    QCOMPARE(document.planes().size(), 1);
    QCOMPARE(document.planes()[0].parentPlaneIndex(), -1);
    QCOMPARE(int(document.planes()[0].lockedEdgeMask()), 0);
}

// 共边关系：父子平面互为共边，独立平面不算共边；删掉子平面后父平面解除共边。
void VpDocumentTest::planeLinkageTracksSharedEdge()
{
    VpDocument document;
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
void VpDocumentTest::pastesImageAsSelectedFloatingSelection()
{
    VpDocument document;
    QImage image(32, 24, QImage::Format_ARGB32);
    image.fill(Qt::red);

    QCOMPARE(document.addFloatingSelection(makeTestSelection(image)), 0);
    QCOMPARE(document.floatingSelections().size(), 1);
    QCOMPARE(document.selectedFloatingSelection(), 0);
    QCOMPARE(document.floatingSelection(0).bitmap.size(), QSize(32, 24));
    QCOMPARE(document.floatingSelection(0).placementOrigin, QPointF(0, 0));
    QVERIFY(!document.floatingSelection(0).surfaceAttached);

    QVERIFY(document.undo());
    QVERIFY(document.floatingSelections().isEmpty());
    QCOMPARE(document.selectedFloatingSelection(), -1);
}

// 删除选中的浮动图像：选中项落到删除位置上的下一张，且可撤销。
void VpDocumentTest::removesSelectedFloatingSelection()
{
    VpDocument document;
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::red);
    document.addFloatingSelection(makeTestSelection(image));
    document.addFloatingSelection(makeTestSelection(image));
    document.setSelectedFloatingSelection(0);

    document.removeFloatingSelection(0);
    QCOMPARE(document.floatingSelections().size(), 1);
    QCOMPARE(document.selectedFloatingSelection(), 0);

    QVERIFY(document.undo());
    QCOMPARE(document.floatingSelections().size(), 2);
}

void VpDocumentTest::deletesDescendantsAndRemapsSharedEdges()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    QImage background(400,400,QImage::Format_ARGB32_Premultiplied); background.fill(Qt::white);
    const PerspectiveQuad::Corners corners{{QPointF(50,50),QPointF(250,50),QPointF(250,250),QPointF(50,250)}};
    QVector<PerspectivePlane> planes;
    for (int i=0; i<9; ++i) {
        PerspectivePlane plane(corners,corners); plane.setSurfaceGroupId(i);
        planes.append(plane);
    }
    const auto link = [&](int child, int parent, int edge) {
        planes[child].setParent(parent,edge);
        planes[child].setEdgeLocked(0,true);
        planes[parent].setEdgeLocked(edge,true);
    };
    link(1,0,1); link(3,1,2); link(4,0,1); link(5,2,3); link(6,3,2); link(7,1,3);
    const auto populate = [&](VpDocument &doc) {
        doc.setImage(background);
        for (const auto &plane : planes) doc.appendPlane(plane);
        doc.setSelectedPlane(5); doc.commitHistory();
    };
    VpDocument doc; populate(doc);
    doc.removePlane(1);
    check(doc.planes().size()==5, "deleting child also removes its entire descendant subtree");
    if (doc.planes().size()==5) {
        check(doc.planes()[0].surfaceGroupId()==0 && doc.planes()[1].surfaceGroupId()==2
              && doc.planes()[2].surfaceGroupId()==4 && doc.planes()[3].surfaceGroupId()==5
              && doc.planes()[4].surfaceGroupId()==8, "siblings and unrelated trees survive in order");
        check(doc.planes()[2].parentPlaneIndex()==0 && doc.planes()[3].parentPlaneIndex()==1,
              "surviving parent indices remap across non-contiguous removals");
        check(doc.planes()[0].isEdgeLocked(1), "shared edge remains locked while sibling survives");
        check(doc.selectedPlane()==3, "surviving selected plane index remaps");
    }
    const auto after = doc.planes();
    doc.undo(); check(doc.planes()==planes && doc.selectedPlane()==5, "single undo restores all descendants and locks");
    doc.redo(); check(doc.planes()==after, "single redo removes complete subtree again");
    doc.removePlane(2);
    check(!doc.planes()[0].isEdgeLocked(1), "last child removal unlocks surviving parent edge");
    doc.removePlane(0);
    check(doc.planes().size()==3 && doc.planes()[1].parentPlaneIndex()==0,
          "unrelated parent-child links survive root index removal");
    doc.removePlane(0);
    check(doc.planes().size()==1 && doc.planes()[0].surfaceGroupId()==8,
          "removing second root removes its child only");
    doc.removePlane(0); check(doc.planes().isEmpty() && doc.selectedPlane()==-1, "empty tree clears selection");

    VpDocument root; populate(root); root.setSelectedPlane(6); root.removePlane(0);
    check(root.planes().size()==3 && root.planes()[0].surfaceGroupId()==2
          && root.planes()[1].surfaceGroupId()==5 && root.planes()[2].surfaceGroupId()==8,
          "root deletion removes all generations and branches");
    check(root.selectedPlane()>=0 && root.selectedPlane()<root.planes().size(), "deleted descendant selection falls back safely");
    root.undo(); check(root.planes()==planes, "root undo restores complete hierarchy");
    root.beginEdit(); root.removePlane(1); root.cancelEdit();
    check(root.planes()==planes, "cancelled transaction restores entire subtree");

    VpController controller; populate(controller.document()); controller.setTool(VpTools::EditPlane);
    controller.document().setSelectedPlane(0); controller.deleteSelection();
    check(controller.document().planes().size()==3, "Delete tool command uses recursive removal");
    controller.undo(); check(controller.document().planes()==planes, "controller undo restores deletion in one step");
    controller.clearSelection();
    check(controller.document().planes()==planes, "Ctrl D does not delete selected plane");
    controller.setTool(VpTools::CreatePlane);
    controller.pointerPress(QPointF(10,10),1,Qt::NoModifier);
    controller.clearSelection();
    check(controller.creationPoints().size()==1, "Ctrl D does not remove creation corners");
    controller.setTool(VpTools::Marquee);
    controller.pointerPress(QPointF(100,100),1,Qt::NoModifier);
    controller.pointerRelease(QPointF(180,180),Qt::NoModifier);
    check(!controller.selectionOutline().isEmpty(), "marquee fixture is selected");
    controller.clearSelection();
    check(controller.selectionOutline().isEmpty(), "Ctrl D clears normal marquee");
    FloatingSelection floating; floating.bitmap=QImage(20,20,QImage::Format_ARGB32_Premultiplied);
    floating.bitmap.fill(Qt::red);
    controller.document().addFloatingSelection(floating);
    controller.clearSelection();
    check(controller.document().floatingSelections().size()==1
          && controller.document().floatingSelection(0)==floating
          && controller.document().selectedFloatingSelection()==0,
          "Ctrl D preserves floating content and its selection");
    controller.pointerPress(QPointF(5,5),1,Qt::NoModifier);
    controller.clearSelection();
    controller.pointerRelease(QPointF(15,5),Qt::NoModifier);
    check(controller.document().floatingSelection(0).placementOrigin==QPointF(10,0),
          "Ctrl D does not interrupt floating selection drag");
}

QTEST_MAIN(VpDocumentTest)
#include "tst_vpdocument.moc"
