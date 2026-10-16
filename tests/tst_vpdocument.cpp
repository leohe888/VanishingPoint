#include <QCoreApplication>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>
#include <limits>

#include "canvas/vpdocument.h"
#include "testhelpers.h"

namespace {
QImage imageOf(const QColor &color=Qt::white,QSize size={520,515})
{
    QImage image(size,QImage::Format_ARGB32); image.fill(color); return image;
}

FloatingSelection selectionOf(const QColor &color=Qt::red)
{
    return makeTestSelection(imageOf(color,{12,10}));
}

void paint(VpDocument &doc,const QRect &rect,const QColor &color)
{
    doc.preparePaintDirty(rect);
    { QPainter painter(&doc.paintLayer()); painter.setCompositionMode(QPainter::CompositionMode_Source); painter.fillRect(rect,color); }
    doc.addPaintDirty(rect);
}

struct Snapshot {
    QVector<PerspectivePlane> planes;
    QVector<FloatingSelection> floating;
    QImage background,paintLayer;
    int selectedPlane,selectedFloating;
    explicit Snapshot(const VpDocument &doc) : planes(doc.planes()),floating(doc.floatingSelections()),
        background(doc.background()),paintLayer(doc.paintLayer()),selectedPlane(doc.selectedPlane()),selectedFloating(doc.selectedFloatingSelection()) {}
    bool matches(const VpDocument &doc) const {
        return planes==doc.planes() && floating==doc.floatingSelections() && background==doc.background()
            && paintLayer==doc.paintLayer() && selectedPlane==doc.selectedPlane() && selectedFloating==doc.selectedFloatingSelection();
    }
};
}

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
    void failedLoadPreservesActiveEdit();
    void successfulLoadResetsAllState();
    void cancelRestoresCompleteTransaction();
    void transactionLifecycle();
    void pixelHistory_data();
    void pixelHistory();
    void backupModesProduceSameHistory();
    void noOpPreservesRedo_data();
    void noOpPreservesRedo();
    void mixedHistoryRestoresEveryState();
    void historyRetainsFortyCompleteStates();
    void invalidFloatingWrites_data();
    void invalidFloatingWrites();
    void invalidIndicesAndGeometryPreserveState();
    void attachmentAndHistoryOwnSnapshots();
    void floatingDeletionRemapsSelection_data();
    void floatingDeletionRemapsSelection();
    void notificationsExposeConsistentState();
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

}

void VpDocumentTest::failedLoadPreservesActiveEdit()
{
    VpDocument doc; QVERIFY(doc.setImage(imageOf())); doc.appendPlane(makeTestPlane()); doc.resetHistory();
    doc.addFloatingSelection(selectionOf()); doc.addFloatingSelection(selectionOf(Qt::blue)); QVERIFY(doc.undo());
    const Snapshot original(doc); doc.beginEdit(); doc.beginPaintTransaction(true);
    paint(doc,{250,250,20,20},Qt::green); doc.setFloatingSelectionOrigin(0,{30,40});
    const Snapshot editing(doc);
    QSignalSpy availability(&doc,&VpDocument::documentAvailabilityChanged),structure(&doc,&VpDocument::structureChanged);
    QVERIFY(!doc.setImage({})); QVERIFY(editing.matches(doc)); QVERIFY(doc.editActive()); QVERIFY(doc.canRedo());
    QCOMPARE(availability.size(),0); QCOMPARE(structure.size(),0);
    doc.cancelEdit(); QVERIFY(original.matches(doc)); QVERIFY(!doc.editActive()); QVERIFY(doc.canRedo());
    QVERIFY(doc.redo()); QCOMPARE(doc.floatingSelections().size(),2);
}

void VpDocumentTest::successfulLoadResetsAllState()
{
    VpDocument doc; doc.setImage(imageOf()); doc.appendPlane(makeTestPlane()); doc.setSelectedPlane(0);
    doc.addFloatingSelection(selectionOf()); doc.addFloatingSelection(selectionOf(Qt::blue)); QVERIFY(doc.undo());
    doc.beginEdit(); doc.beginPaintTransaction(true); paint(doc,{1,1,8,8},Qt::green);
    const QImage source=imageOf(QColor(100,150,200,128),{37,29}); QVERIFY(doc.setImage(source));
    QCOMPARE(doc.background(),source); QCOMPARE(doc.paintLayer(),imageOf(Qt::transparent,source.size()));
    QVERIFY(doc.planes().isEmpty()); QVERIFY(doc.floatingSelections().isEmpty());
    QCOMPARE(doc.selectedPlane(),-1); QCOMPARE(doc.selectedFloatingSelection(),-1);
    QVERIFY(!doc.editActive()); QVERIFY(!doc.canUndo()); QVERIFY(!doc.canRedo());
    doc.cancelEdit(); QVERIFY(!doc.undo()); QVERIFY(!doc.redo()); QCOMPARE(doc.background(),source);
    doc.beginPaintTransaction(true); paint(doc,{2,2,4,4},Qt::red); doc.commitHistory();
    QVERIFY(doc.undo()); QCOMPARE(doc.paintLayer(),imageOf(Qt::transparent,source.size()));
}

void VpDocumentTest::cancelRestoresCompleteTransaction()
{
    VpDocument doc; doc.setImage(imageOf()); doc.appendPlane(makeTestPlane()); doc.setSelectedPlane(0);
    doc.addFloatingSelection(selectionOf()); doc.resetHistory(); const Snapshot before(doc);
    doc.beginEdit(); doc.beginPaintTransaction(true);
    auto plane=doc.planes()[0]; plane.quad().setCanvasCorner(2,{125,90}); QVERIFY(doc.setPlane(0,plane));
    doc.setSelectedPlane(-1); doc.setFloatingSelectionOrigin(0,{90,80});
    doc.addFloatingSelection(selectionOf(Qt::blue)); doc.removeFloatingSelection(0);
    paint(doc,{250,250,30,30},QColor(20,60,100,120));
    QVERIFY(doc.editActive()); doc.cancelEdit(); QVERIFY(before.matches(doc));
    QVERIFY(!doc.editActive()); QVERIFY(!doc.canUndo()); QVERIFY(!doc.canRedo());
    doc.beginEdit(); doc.beginPaintTransaction(true); paint(doc,{1,1,2,2},Qt::red); doc.commitEdit(true);
    QVERIFY(doc.undo()); QVERIFY(before.matches(doc));
}

void VpDocumentTest::transactionLifecycle()
{
    VpDocument doc; doc.setImage(imageOf()); doc.appendPlane(makeTestPlane()); doc.resetHistory(); const Snapshot before(doc);
    doc.cancelEdit(); doc.commitEdit(true); doc.commitEdit(false); QVERIFY(before.matches(doc)); QVERIFY(!doc.canUndo());
    doc.beginEdit(); auto plane=doc.planes()[0]; plane.quad().setCanvasCorner(2,{120,85}); QVERIFY(doc.setPlane(0,plane));
    doc.beginEdit(); doc.setSelectedPlane(0); doc.cancelEdit(); QVERIFY(before.matches(doc)); QVERIFY(!doc.editActive());
    doc.beginEdit(); QVERIFY(doc.setPlane(0,plane)); doc.commitEdit(false);
    QVERIFY(doc.planes()[0]==plane); QVERIFY(!doc.canUndo()); QVERIFY(!doc.editActive()); // false does not mean rollback.
    doc.resetHistory(); const Snapshot baseline(doc); doc.beginEdit(); doc.appendPlane(makeTestPlane());
    doc.addFloatingSelection(selectionOf()); doc.addFloatingSelection(selectionOf(Qt::blue)); doc.removeFloatingSelection(0);
    doc.commitEdit(true);
    doc.commitEdit(true); QVERIFY(doc.undo()); QVERIFY(baseline.matches(doc)); QVERIFY(!doc.canUndo());
}

void VpDocumentTest::pixelHistory_data()
{
    QTest::addColumn<bool>("incremental"); QTest::newRow("whole-layer") << false; QTest::newRow("incremental-tiles") << true;
}

void VpDocumentTest::pixelHistory()
{
    QFETCH(bool,incremental); VpDocument doc; doc.setImage(imageOf());
    doc.paintLayer().fill(QColor(40,80,120,90)); doc.resetHistory(); const QImage before=doc.paintLayer();
    doc.beginPaintTransaction(incremental);
    paint(doc,{250,250,22,22},Qt::red); paint(doc,{255,255,5,5},QColor(0,255,0,128));
    paint(doc,{4,4,8,8},Qt::blue); paint(doc,{511,511,20,20},Qt::transparent);
    paint(doc,{-10,40,15,8},Qt::yellow); doc.preparePaintDirty({900,900,5,5}); doc.addPaintDirty({900,900,5,5});
    doc.commitHistory(); const QImage after=doc.paintLayer(); QVERIFY(after!=before);
    QCOMPARE(after.pixelColor(300,300),before.pixelColor(300,300)); QCOMPARE(qAlpha(after.pixel(519,514)),0);
    QVERIFY(doc.undo()); QCOMPARE(doc.paintLayer(),before); QVERIFY(!doc.canUndo());
    QVERIFY(doc.redo()); QCOMPARE(doc.paintLayer(),after); QVERIFY(!doc.canRedo());
}

void VpDocumentTest::backupModesProduceSameHistory()
{
    VpDocument whole,incremental;
    for(auto *doc : {&whole,&incremental}) { doc->setImage(imageOf()); doc->paintLayer().fill(QColor(20,50,90,80)); doc->resetHistory(); }
    for(int step=0;step<4;++step) {
        for(auto *doc : {&whole,&incremental}) {
            doc->beginPaintTransaction(doc==&incremental);
            paint(*doc,{245+step*3,250,20,22},QColor(50*step,80,150,120));
            paint(*doc,{510,510,20,20},step%2 ? QColor(Qt::transparent) : QColor(Qt::red)); doc->commitHistory();
        }
        QCOMPARE(whole.paintLayer(),incremental.paintLayer());
    }
    for(int i=0;i<4;++i) { QVERIFY(whole.undo()); QVERIFY(incremental.undo()); QCOMPARE(whole.paintLayer(),incremental.paintLayer()); }
    for(int i=0;i<4;++i) { QVERIFY(whole.redo()); QVERIFY(incremental.redo()); QCOMPARE(whole.paintLayer(),incremental.paintLayer()); }
}

void VpDocumentTest::noOpPreservesRedo_data()
{
    QTest::addColumn<int>("operation");
    const char *names[]={"empty-history","empty-edit","restored-structure","restored-pixels","rejected-write","empty-dirty"};
    for(int i=0;i<6;++i) QTest::newRow(names[i]) << i;
}

void VpDocumentTest::noOpPreservesRedo()
{
    QFETCH(int,operation); VpDocument doc; doc.setImage(imageOf());
    doc.addFloatingSelection(selectionOf()); doc.addFloatingSelection(selectionOf(Qt::blue)); QVERIFY(doc.undo()); const Snapshot before(doc);
    if(operation==0) doc.commitHistory();
    if(operation==1) { doc.beginEdit(); doc.commitEdit(true); }
    if(operation==2) { doc.beginEdit(); doc.setFloatingSelectionOrigin(0,{20,20}); doc.setFloatingSelectionOrigin(0,{0,0}); doc.commitEdit(true); }
    if(operation==3) { doc.beginPaintTransaction(true); paint(doc,{250,250,20,20},Qt::red); paint(doc,{250,250,20,20},Qt::transparent); doc.commitHistory(); }
    if(operation==4) QVERIFY(!doc.setFloatingSelection(0,makeTestSelection({})));
    if(operation==5) { doc.beginPaintTransaction(true); doc.preparePaintDirty({}); doc.addPaintDirty({}); doc.commitHistory(); }
    QVERIFY(before.matches(doc)); QVERIFY(doc.canRedo()); QVERIFY(doc.redo()); QCOMPARE(doc.floatingSelections().size(),2);
    QVERIFY(doc.undo()); QVERIFY(before.matches(doc));
    doc.beginEdit(); doc.setFloatingSelectionOrigin(0,{10,10}); doc.commitEdit(true); QVERIFY(!doc.canRedo());
}

void VpDocumentTest::mixedHistoryRestoresEveryState()
{
    VpDocument doc; doc.setImage(imageOf()); QVector<Snapshot> states; states.append(Snapshot(doc));
    doc.beginPaintTransaction(true); paint(doc,{250,250,20,20},Qt::red); doc.commitHistory(); states.append(Snapshot(doc));
    doc.beginEdit(); doc.appendPlane(makeTestPlane()); doc.setSelectedPlane(0); doc.commitEdit(true); states.append(Snapshot(doc));
    doc.addFloatingSelection(selectionOf()); states.append(Snapshot(doc));
    doc.beginEdit(); auto selection=doc.floatingSelection(0); selection.rotationDegrees=30; selection.scaleFactors={2,3};
    QVERIFY(doc.setFloatingSelection(0,selection)); doc.beginPaintTransaction(false); paint(doc,{255,255,10,10},Qt::transparent);
    doc.commitEdit(true); states.append(Snapshot(doc));
    doc.removePlane(0); states.append(Snapshot(doc));
    for(int i=states.size()-2;i>=0;--i) { QVERIFY(doc.undo()); QVERIFY2(states[i].matches(doc),qPrintable(QString("undo state %1").arg(i))); }
    QVERIFY(!doc.undo());
    for(int i=1;i<states.size();++i) { QVERIFY(doc.redo()); QVERIFY2(states[i].matches(doc),qPrintable(QString("redo state %1").arg(i))); }
    QVERIFY(!doc.redo());
}

void VpDocumentTest::historyRetainsFortyCompleteStates()
{
    VpDocument doc; doc.setImage(imageOf(Qt::white,{64,64})); QVector<QImage> states{doc.paintLayer()};
    for(int i=1;i<=45;++i) { doc.beginPaintTransaction(true); paint(doc,{i,2,1,1},QColor(i,30,70)); doc.commitHistory(); states.append(doc.paintLayer()); }
    int undos=0;
    for(int i=44;doc.canUndo();--i) { QVERIFY(doc.undo()); QCOMPARE(doc.paintLayer(),states[i]); ++undos; }
    QCOMPARE(undos,39); QCOMPARE(doc.paintLayer(),states[6]); QVERIFY(!doc.undo());
    for(int i=7;i<=45;++i) { QVERIFY(doc.redo()); QCOMPARE(doc.paintLayer(),states[i]); }
    QVERIFY(!doc.redo());
    QVERIFY(doc.undo()); doc.beginPaintTransaction(true); paint(doc,{50,3,1,1},Qt::blue); doc.commitHistory();
    QVERIFY(!doc.canRedo()); const QImage newBranch=doc.paintLayer(); QVERIFY(doc.undo()); QCOMPARE(doc.paintLayer(),states[44]);
    QVERIFY(doc.redo()); QCOMPARE(doc.paintLayer(),newBranch);
}

void VpDocumentTest::invalidFloatingWrites_data()
{
    QTest::addColumn<int>("invalid");
    const char *names[]={"empty-bitmap","nan-origin","infinite-origin","nan-rotation","zero-scale","negative-scale","infinite-scale",
                         "negative-opacity","high-opacity","negative-heal","high-heal","empty-faces","negative-host","high-host","invalid-face"};
    for(int i=0;i<15;++i) QTest::newRow(names[i]) << i;
}

void VpDocumentTest::invalidFloatingWrites()
{
    QFETCH(int,invalid); VpDocument doc; doc.setImage(imageOf()); doc.addFloatingSelection(selectionOf());
    doc.addFloatingSelection(selectionOf(Qt::blue)); QVERIFY(doc.undo()); const Snapshot before(doc);
    auto value=selectionOf(Qt::green); const qreal nan=std::numeric_limits<qreal>::quiet_NaN(),inf=std::numeric_limits<qreal>::infinity();
    if(invalid==0) value.bitmap={}; if(invalid==1) value.placementOrigin.setX(nan); if(invalid==2) value.placementOrigin.setY(inf);
    if(invalid==3) value.rotationDegrees=nan; if(invalid==4) value.scaleFactors.setX(0); if(invalid==5) value.scaleFactors.setY(-1);
    if(invalid==6) value.scaleFactors.setX(inf); if(invalid==7) value.opacity=-1; if(invalid==8) value.opacity=101;
    if(invalid==9) value.healMode=-1; if(invalid==10) value.healMode=3;
    if(invalid>=11) { value.surfaceAttached=true; value.surfaceQuads={makeTestPlane().quad()}; value.hostQuadIndex=0; }
    if(invalid==11) value.surfaceQuads.clear(); if(invalid==12) value.hostQuadIndex=-1; if(invalid==13) value.hostQuadIndex=1;
    if(invalid==14) value.surfaceQuads[0].setSurfaceCorners({});
    QSignalSpy structure(&doc,&VpDocument::structureChanged),selected(&doc,&VpDocument::floatingSelectionChanged);
    QVERIFY(!doc.setFloatingSelection(0,value)); QCOMPARE(doc.addFloatingSelection(value),-1);
    if(invalid<=2) QCOMPARE(doc.addFloatingSelectionOnSurface(value.bitmap,{makeTestPlane().quad()},0,value.placementOrigin),-1);
    if(invalid>=11) {
        QCOMPARE(doc.addFloatingSelectionOnSurface(value.bitmap,value.surfaceQuads,value.hostQuadIndex,value.placementOrigin),-1);
        doc.attachFloatingSelection(0,value.surfaceQuads,value.hostQuadIndex,value.placementOrigin);
    }
    QVERIFY(before.matches(doc)); QVERIFY(doc.canRedo()); QCOMPARE(structure.size(),0); QCOMPARE(selected.size(),0);
    QVERIFY(doc.redo()); QCOMPARE(doc.floatingSelections().size(),2);
}

void VpDocumentTest::invalidIndicesAndGeometryPreserveState()
{
    VpDocument doc; doc.setImage(imageOf()); doc.appendPlane(makeTestPlane()); doc.setSelectedPlane(0); doc.addFloatingSelection(selectionOf());
    const Snapshot before(doc);
    for(int index : {-1,20}) {
        QVERIFY(!doc.setPlane(index,makeTestPlane())); QVERIFY(!doc.setFloatingSelection(index,selectionOf()));
        doc.removePlane(index); doc.removeFloatingSelection(index); doc.setFloatingSelectionOrigin(index,{2,3});
        doc.attachFloatingSelection(index,{makeTestPlane().quad()},0,{2,3}); doc.detachFloatingSelection(index,{2,3}); doc.lockPlaneEdge(index,0);
    }
    auto plane=makeTestPlane(); plane.quad().setSurfaceCorners({}); QCOMPARE(doc.appendPlane(plane),-1); QVERIFY(!doc.setPlane(0,plane));
    plane=makeTestPlane(); plane.setAngleToParentDegrees(std::numeric_limits<qreal>::infinity());
    QCOMPARE(doc.appendPlane(plane),-1); QVERIFY(!doc.setPlane(0,plane));
    doc.setFloatingSelectionOrigin(0,{std::numeric_limits<qreal>::infinity(),0}); doc.attachFloatingSelection(0,{},0,{0,0});
    doc.detachFloatingSelection(0,{std::numeric_limits<qreal>::quiet_NaN(),0});
    QCOMPARE(doc.addFloatingSelectionOnSurface(imageOf(),{},0,{0,0}),-1);
    QVERIFY(before.matches(doc)); doc.setSelectedPlane(90); QCOMPARE(doc.selectedPlane(),-1);
    doc.setSelectedFloatingSelection(90); QCOMPARE(doc.selectedFloatingSelection(),-1);
}

void VpDocumentTest::attachmentAndHistoryOwnSnapshots()
{
    VpDocument doc; doc.setImage(imageOf()); auto selection=selectionOf(); auto plane=makeTestPlane();
    QVector<PerspectiveQuad> faces{plane.quad()}; doc.appendPlane(plane); doc.addFloatingSelection(selection);
    selection.bitmap.fill(Qt::blue); QCOMPARE(doc.floatingSelection(0).bitmap.pixelColor(1,1),QColor(Qt::red));
    const Snapshot detached(doc); doc.beginEdit(); doc.attachFloatingSelection(0,faces,0,{20,15}); doc.commitEdit(true);
    const Snapshot attached(doc); QVERIFY(doc.floatingSelection(0).surfaceAttached); QCOMPARE(doc.floatingSelection(0).hostQuadIndex,0);
    faces[0].setCanvasCorner(2,{140,100}); plane.quad().setCanvasCorner(2,{150,100}); QVERIFY(doc.setPlane(0,plane));
    QVERIFY(doc.floatingSelection(0).surfaceQuads[0]==makeTestPlane().quad());
    // Restore the document plane before exercising the stored attachment history.
    QVERIFY(doc.setPlane(0,makeTestPlane())); doc.beginEdit(); doc.detachFloatingSelection(0,{50,60}); doc.commitEdit(true);
    const auto image=doc.floatingSelection(0); QVERIFY(!image.surfaceAttached); QVERIFY(image.surfaceQuads.isEmpty());
    QCOMPARE(image.hostQuadIndex,-1); QCOMPARE(image.placementOrigin,QPointF(50,60));
    QVERIFY(doc.undo()); QVERIFY(attached.matches(doc)); QVERIFY(doc.undo()); QVERIFY(detached.matches(doc));
    QVERIFY(doc.redo()); QVERIFY(attached.matches(doc));
    VpDocument onSurface;
    QCOMPARE(onSurface.addFloatingSelectionOnSurface(selectionOf().bitmap,{makeTestPlane().quad()},0,{20,15}),0);
    QVERIFY(onSurface.floatingSelection(0).surfaceAttached); QCOMPARE(onSurface.floatingSelection(0).placementOrigin,QPointF(20,15));
    QVERIFY(onSurface.undo()); QVERIFY(onSurface.floatingSelections().isEmpty());
    QVERIFY(onSurface.redo()); QVERIFY(onSurface.floatingSelection(0).surfaceAttached);
}

void VpDocumentTest::floatingDeletionRemapsSelection_data()
{
    QTest::addColumn<int>("selected"); QTest::addColumn<int>("removed"); QTest::addColumn<int>("expected");
    QTest::newRow("before-selected") << 2 << 0 << 1; QTest::newRow("after-selected") << 0 << 2 << 0;
    QTest::newRow("current-middle") << 1 << 1 << 1; QTest::newRow("current-last") << 2 << 2 << 1;
    QTest::newRow("unselected") << -1 << 0 << -1;
}

void VpDocumentTest::floatingDeletionRemapsSelection()
{
    QFETCH(int,selected); QFETCH(int,removed); QFETCH(int,expected); VpDocument doc;
    for(const QColor color : {QColor(Qt::red),QColor(Qt::green),QColor(Qt::blue)}) doc.addFloatingSelection(selectionOf(color));
    doc.setSelectedFloatingSelection(selected); doc.resetHistory(); const Snapshot before(doc); const auto original=doc.floatingSelections();
    doc.removeFloatingSelection(removed); QCOMPARE(doc.selectedFloatingSelection(),expected);
    auto remaining=original; remaining.removeAt(removed); QVERIFY(doc.floatingSelections()==remaining);
    QVERIFY(doc.undo()); QVERIFY(before.matches(doc)); QVERIFY(doc.redo()); QVERIFY(doc.floatingSelections()==remaining);
    while(!doc.floatingSelections().isEmpty()) doc.removeFloatingSelection(0); QCOMPARE(doc.selectedFloatingSelection(),-1);
}

void VpDocumentTest::notificationsExposeConsistentState()
{
    VpDocument doc; bool consistent=true;
    connect(&doc,&VpDocument::canUndoChanged,&doc,[&](bool value) { consistent &= value==doc.canUndo(); });
    connect(&doc,&VpDocument::canRedoChanged,&doc,[&](bool value) { consistent &= value==doc.canRedo(); });
    connect(&doc,&VpDocument::floatingSelectionChanged,&doc,[&](bool selected) { consistent &= selected==(doc.selectedFloatingSelection()>=0); });
    connect(&doc,&VpDocument::documentAvailabilityChanged,&doc,[&](bool loaded) { consistent &= loaded==doc.hasLoadedImage(); });
    QSignalSpy selection(&doc,&VpDocument::floatingSelectionChanged),available(&doc,&VpDocument::documentAvailabilityChanged);
    QVERIFY(doc.setImage(imageOf())); QCOMPARE(available.size(),1); doc.addFloatingSelection(selectionOf()); QCOMPARE(selection.size(),1);
    doc.setSelectedFloatingSelection(0); QCOMPARE(selection.size(),1);
    doc.beginEdit(); doc.setSelectedFloatingSelection(-1); doc.cancelEdit(); QCOMPARE(selection.size(),3);
    QVERIFY(doc.undo()); QVERIFY(doc.redo()); QVERIFY(consistent);
    doc.beginEdit(); doc.setSelectedFloatingSelection(-1);
    bool endedBeforeRestore=false;
    connect(&doc,&VpDocument::floatingSelectionChanged,&doc,[&](bool selected) { if(selected) endedBeforeRestore=!doc.editActive(); });
    doc.cancelEdit(); QVERIFY(endedBeforeRestore); QVERIFY(consistent);
}

QTEST_MAIN(VpDocumentTest)
#include "tst_vpdocument.moc"
