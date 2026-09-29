#include "canvas/vpcanvas.h"
#include "core/scenerenderer.h"
#include "testhelpers.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

class AuditCanvas : public VpCanvas {
public:
    void pointer(QEvent::Type type, QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent event(type, point, point,
                         type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                         type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
        if (type == QEvent::MouseButtonPress) mousePressEvent(&event);
        else if (type == QEvent::MouseMove) mouseMoveEvent(&event);
        else mouseReleaseEvent(&event);
    }
    void deleteSelection() {
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        keyPressEvent(&event);
    }
};

class AuditInteractionsTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory;
    QString path;
    static PerspectivePlane plane() {
        PerspectivePlane p({QPointF(100,100), QPointF(200,100), QPointF(200,200), QPointF(100,200)},
                           {QPointF(0,0), QPointF(100,0), QPointF(100,100), QPointF(0,100)});
        p.setSurfaceGroupId(0);
        return p;
    }
    static QImage bitmap(QColor color) {
        QImage image(20,20,QImage::Format_ARGB32); image.fill(color); return image;
    }
    void setup(AuditCanvas &canvas) {
        QVERIFY(canvas.controller()->document().loadImage(path));
        canvas.setSize(QSizeF(400,400)); canvas.setZoom(1);
    }
    static QImage rendered(const VpDocument &doc) {
        QImage image(400,400,QImage::Format_ARGB32); image.fill(Qt::transparent);
        QPainter painter(&image); SceneRenderer(doc).renderContent(painter); return image;
    }
private slots:
    void initTestCase() {
        path=directory.filePath("background.png");
        QImage image(400,400,QImage::Format_ARGB32); image.fill(Qt::white);
        QVERIFY(image.save(path));
    }
    void deleteDuringDrag_data() {
        QTest::addColumn<int>("count"); QTest::newRow("single")<<1; QTest::newRow("multiple")<<2;
    }
    void deleteDuringDrag() {
        QFETCH(int,count); AuditCanvas canvas; setup(canvas); auto &doc=canvas.controller()->document();
        for(int i=0;i<count;++i) doc.addFloatingImage(bitmap(i?Qt::blue:Qt::red));
        canvas.pointer(QEvent::MouseButtonPress,{10,10}); canvas.deleteSelection();
        canvas.pointer(QEvent::MouseMove,{30,30}); canvas.pointer(QEvent::MouseButtonRelease,{40,40});
        QCOMPARE(doc.floatingImages().size(),count-1);
        if(count>1) QCOMPARE(doc.floatingImage(0).placementOrigin,QPointF());
        canvas.undo(); QCOMPARE(doc.floatingImages().size(),count);
    }
    void pasteFinishesStroke() {
        AuditCanvas canvas; setup(canvas); auto *controller=canvas.controller(); auto &doc=controller->document();
        doc.appendPlane(makeTestPlane()); doc.resetHistory();
        controller->setTool(VpController::Brush); QVERIFY(controller->beginBrush({50,50}));
        QGuiApplication::clipboard()->setImage(bitmap(Qt::red)); controller->pasteImage();
        QVERIFY(!controller->brushDrawing()); controller->moveBrush({80,50}); controller->endBrush();
        canvas.undo(); canvas.undo(); QVERIFY(!doc.hasPaintContent());
        canvas.redo(); QVERIFY(doc.hasPaintContent()); canvas.redo(); QCOMPARE(doc.floatingImages().size(),1);
    }
    void lockedCornerAndNoopPreserveHistory() {
        AuditCanvas canvas; setup(canvas); auto &doc=canvas.controller()->document(); auto p=plane();
        p.setEdgeLocked(0,true); doc.appendPlane(p); doc.resetHistory(); canvas.controller()->setTool(VpController::EditPlane);
        canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseButtonRelease,{110,110});
        QCOMPARE(doc.planes()[0].quad().canvasCorners()[0],QPointF(100,100)); QVERIFY(!doc.canUndo());
        doc.addFloatingImage(bitmap(Qt::red)); doc.undo(); QVERIFY(doc.canRedo());
        canvas.pointer(QEvent::MouseButtonPress,{150,150}); canvas.pointer(QEvent::MouseButtonRelease,{150,150});
        QVERIFY(doc.canRedo());
    }
    void extrapolatedMoveUsesReleaseAndCanUndo() {
        AuditCanvas canvas; setup(canvas); auto &doc=canvas.controller()->document(); auto p=plane();
        doc.appendPlane(p); doc.addFloatingImageOnSurface(bitmap(Qt::red),{p.quad()},0,{250,100}); doc.resetHistory(); canvas.controller()->setTool(VpController::EditPlane);
        canvas.pointer(QEvent::MouseButtonPress,{360,210}); canvas.pointer(QEvent::MouseButtonRelease,{370,210});
        QCOMPARE(doc.floatingImage(0).placementOrigin,QPointF(260,100)); QVERIFY(doc.canUndo());
        canvas.undo(); QCOMPARE(doc.floatingImage(0).placementOrigin,QPointF(250,100));
    }
    void bakePreservesLayerOrderAndUndo() {
        AuditCanvas canvas; setup(canvas); auto &doc=canvas.controller()->document();
        doc.addFloatingImage(bitmap(Qt::red)); doc.addFloatingImage(bitmap(Qt::blue));
        auto upper=bitmap(Qt::green); doc.addFloatingImage(upper); doc.setSelectedFloatingImage(1);
        const auto before=rendered(doc); canvas.pointer(QEvent::MouseButtonPress,{300,300});
        QCOMPARE(rendered(doc),before); QCOMPARE(doc.floatingImages().size(),1);
        canvas.undo(); QCOMPARE(doc.floatingImages().size(),3); QCOMPARE(rendered(doc),before);
        canvas.redo(); QCOMPARE(rendered(doc),before);
    }
    void bakingDisablesTransform_data() {
        QTest::addColumn<bool>("hasPlane");
        QTest::newRow("no-plane") << false;
        QTest::newRow("with-plane") << true;
    }
    void bakingDisablesTransform() {
        QFETCH(bool, hasPlane);
        AuditCanvas canvas; setup(canvas);
        auto *controller = canvas.controller(); auto &doc = controller->document();
        if (hasPlane) doc.appendPlane(plane());
        doc.resetHistory(); doc.addFloatingImage(bitmap(Qt::red));
        controller->setTool(VpController::Transform);
        controller->pointerPress({300,300}, 1, Qt::NoModifier);
        QVERIFY(doc.floatingImages().isEmpty()); QVERIFY(doc.hasPaintContent());
        QVERIFY(!controller->isToolEnabled(VpController::Transform));
        QCOMPARE(controller->tool(), hasPlane ? VpController::EditPlane : VpController::CreatePlane);
        controller->undo();
        QCOMPARE(doc.floatingImages().size(), 1); QVERIFY(!doc.hasPaintContent());
        QVERIFY(controller->isToolEnabled(VpController::Transform));
        controller->redo();
        QVERIFY(doc.floatingImages().isEmpty()); QVERIFY(doc.hasPaintContent());
        QVERIFY(!controller->isToolEnabled(VpController::Transform));
    }
    void releaseMovesPlaneWithoutMoveEvent() {
        AuditCanvas canvas; setup(canvas); auto &doc=canvas.controller()->document(); doc.appendPlane(plane()); doc.resetHistory(); canvas.controller()->setTool(VpController::EditPlane);
        canvas.pointer(QEvent::MouseButtonPress,{150,150}); canvas.pointer(QEvent::MouseButtonRelease,{160,150});
        QCOMPARE(doc.planes()[0].quad().canvasCorners()[0],QPointF(110,100)); QVERIFY(doc.canUndo());
    }
    void pasteCancelsPreview_data() {
        QTest::addColumn<QString>("kind");
        for (const QString kind : {"plane", "image", "fill"}) QTest::newRow(qPrintable(kind))<<kind;
    }
    void pasteCancelsPreview() {
        QFETCH(QString,kind); AuditCanvas canvas; setup(canvas); auto *controller=canvas.controller(); auto &doc=controller->document();
        doc.appendPlane(plane());
        if(kind=="image") doc.addFloatingImage(bitmap(Qt::red));
        doc.resetHistory(); canvas.controller()->setTool(VpController::EditPlane);
        if(kind=="plane") {
            controller->pointerPress({150,150},1,Qt::NoModifier); controller->pointerMove({160,150},Qt::NoModifier);
        } else if(kind=="image") {
            controller->pointerPress({10,10},1,Qt::NoModifier); controller->pointerMove({20,20},Qt::NoModifier);
        } else {
            controller->setTool(VpController::Marquee);
            controller->pointerPress({120,120},1,Qt::NoModifier); controller->pointerRelease({160,160},Qt::NoModifier);
            controller->pointerPress({140,140},1,Qt::ControlModifier); controller->pointerMove({145,140},Qt::ControlModifier);
            QVERIFY(doc.hasPaintContent());
        }
        QGuiApplication::clipboard()->setImage(bitmap(Qt::blue)); controller->pasteImage();
        QVERIFY(!doc.editActive()); QVERIFY(!doc.hasPaintContent());
        QCOMPARE(doc.planes()[0].quad().canvasCorners()[0],QPointF(100,100));
        canvas.undo(); QCOMPARE(doc.floatingImages().size(),kind=="image"?1:0);
        if(kind=="image") QCOMPARE(doc.floatingImage(0).placementOrigin,QPointF());
        canvas.redo(); QCOMPARE(doc.floatingImages().size(),kind=="image"?2:1);
    }
    void pasteFinishesCloneStroke() {
        AuditCanvas canvas; setup(canvas); auto *controller=canvas.controller(); auto &doc=controller->document();
        doc.appendPlane(makeTestPlane()); doc.resetHistory();
        controller->setTool(VpController::CloneStamp); QVERIFY(controller->pickCloneSource({20,20}));
        QVERIFY(controller->beginClone({50,50})); QGuiApplication::clipboard()->setImage(bitmap(Qt::red));
        controller->pasteImage(); QVERIFY(!controller->cloneDrawing()); controller->moveClone({80,50});
        canvas.undo(); canvas.undo(); QVERIFY(!doc.hasPaintContent());
        canvas.redo(); QVERIFY(doc.hasPaintContent());
    }
    void sharedEdgeSurvivesSiblingRemovalAndRejectsMutation() {
        AuditCanvas canvas; setup(canvas); auto &doc=canvas.controller()->document(); auto parent=plane();
        parent.setEdgeLocked(0,true); doc.appendPlane(parent);
        auto child=plane(); child.setParent(0,0); child.setEdgeLocked(0,true);
        doc.appendPlane(child); doc.appendPlane(child); doc.resetHistory(); canvas.controller()->setTool(VpController::EditPlane); doc.removePlane(1);
        QVERIFY(doc.planes()[0].isEdgeLocked(0));
        auto candidate=doc.planes()[0]; candidate.quad().setCanvasCorner(0,{110,110});
        QVERIFY(!doc.setPlane(0,candidate));
        doc.removePlane(1); QVERIFY(!doc.planes()[0].isEdgeLocked(0));
        canvas.undo(); QVERIFY(doc.planes()[0].isEdgeLocked(0));
    }
    void angleLockRejectsBothInputAndAltDrag() {
        AuditCanvas canvas; setup(canvas); auto *controller=canvas.controller(); auto &doc=controller->document();
        auto parent=plane(); doc.appendPlane(parent);
        auto child=plane(); child.setParent(0,0); child.setEdgeLocked(0,true); doc.appendPlane(child);
        auto grandchild=plane(); grandchild.quad().setCanvasCorners({QPointF(250,250),QPointF(350,250),QPointF(350,350),QPointF(250,350)});
        grandchild.setParent(1,2); grandchild.setHasCustomAngle(true); doc.appendPlane(grandchild);
        doc.setSelectedPlane(1); doc.resetHistory(); canvas.controller()->setTool(VpController::EditPlane); QVERIFY(!controller->planeAngleEditable());
        controller->setPlaneAngle(120); QCOMPARE(doc.planes()[1].angleToParentDegrees(),qreal(90));
        controller->pointerPress({150,200},1,Qt::AltModifier); controller->pointerRelease({140,210},Qt::AltModifier);
        QVERIFY(!doc.canUndo()); QCOMPARE(doc.planes()[1].quad().canvasCorners(),child.quad().canvasCorners());
    }
    void openingDocumentResetsToolsAndPreservesFailure() {
        AuditCanvas canvas; setup(canvas); auto *controller=canvas.controller(); auto &doc=controller->document();
        doc.appendPlane(makeTestPlane()); doc.resetHistory();
        QVERIFY(controller->pickCloneSource({20,20})); QVERIFY(controller->beginBrush({30,30}));
        QVERIFY(controller->openImage(QUrl::fromLocalFile(path)));
        QVERIFY(!controller->hasCloneSource()); QVERIFY(!controller->brushDrawing()); QVERIFY(!doc.hasPaintContent()); QVERIFY(!doc.canUndo());
        doc.addFloatingImage(bitmap(Qt::red)); const auto before=rendered(doc);
        QVERIFY(!controller->openImage(QUrl::fromLocalFile(directory.filePath("missing.png"))));
        QCOMPARE(rendered(doc),before); QVERIFY(doc.canUndo());
    }
    void perspectiveClickAndUnchangedAngleDoNotCreateHistory() {
        AuditCanvas canvas; setup(canvas); auto *controller=canvas.controller(); auto &doc=controller->document();
        auto p=plane(); p.quad().setCanvasCorners({QPointF(100,100),QPointF(240,100),QPointF(280,240),QPointF(80,240)});
        doc.appendPlane(p); auto child=p; child.setParent(0,0); doc.appendPlane(child); doc.setSelectedPlane(1); doc.resetHistory(); canvas.controller()->setTool(VpController::EditPlane);
        controller->setPlaneAngle(90); QVERIFY(!doc.canUndo()); QVERIFY(!doc.planes()[1].hasCustomAngle());
        controller->pointerPress({180,240},1,Qt::AltModifier); controller->pointerRelease({180,240},Qt::AltModifier);
        QVERIFY(!doc.canUndo()); QVERIFY(doc.planes()[1]==child);
        doc.removePlane(1); doc.resetHistory(); canvas.controller()->setTool(VpController::EditPlane);
        controller->pointerPress({170,170},1,Qt::NoModifier); controller->pointerRelease({170,170},Qt::NoModifier);
        QVERIFY(!doc.canUndo()); QVERIFY(doc.planes()[0]==p);
    }
    void extrusionCanUseAdjacentUnlockedEdgeAndCannotRepeatSharedEdge() {
        AuditCanvas canvas; setup(canvas); auto *controller=canvas.controller(); auto &doc=controller->document(); auto p=plane();
        p.quad().setCanvasCorners({QPointF(100,100),QPointF(220,100),QPointF(220,200),QPointF(100,200)});
        p.setEdgeLocked(0,true); doc.appendPlane(p); doc.resetHistory(); canvas.controller()->setTool(VpController::EditPlane);
        controller->pointerPress({220,150},1,Qt::ControlModifier); controller->pointerRelease({240,100},Qt::ControlModifier);
        QCOMPARE(doc.planes().size(),2); QVERIFY(doc.planes()[0].isEdgeLocked(1));
        controller->pointerPress({220,150},1,Qt::ControlModifier); controller->pointerRelease({260,100},Qt::ControlModifier);
        QCOMPARE(doc.planes().size(),2); QCOMPARE(doc.planes()[0].quad().canvasCorners(),p.quad().canvasCorners());
    }
    void adjacentMidpointsExtendAfterExtrusion_data() {
        QTest::addColumn<int>("planeIndex"); QTest::addColumn<int>("side");
        QTest::newRow("parent-next") << 0 << 1;
        QTest::newRow("parent-previous") << 0 << 3;
        QTest::newRow("child-next") << 1 << 1;
        QTest::newRow("child-previous") << 1 << 3;
    }
    void adjacentMidpointsExtendAfterExtrusion() {
        QFETCH(int, planeIndex); QFETCH(int, side);
        AuditCanvas canvas; setup(canvas); auto *controller = canvas.controller();
        auto &doc = controller->document(); auto parent = plane();
        parent.quad().setCanvasCorners({QPointF(100,100), QPointF(220,100), QPointF(220,200), QPointF(100,200)});
        doc.appendPlane(parent); controller->setTool(VpController::EditPlane);
        controller->pointerPress({220,150}, 1, Qt::ControlModifier);
        controller->pointerRelease({240,100}, Qt::ControlModifier);
        QCOMPARE(doc.planes().size(), 2);
        doc.resetHistory(); doc.setSelectedPlane(planeIndex);
        const auto original = doc.planes()[planeIndex];
        const auto other = doc.planes()[1-planeIndex];
        const int seam = planeIndex == 0 ? 1 : 0;
        const int edge = (seam + side) % 4;
        const auto corners = original.quad().canvasCorners();
        const QPointF midpoint = (corners[edge] + corners[(edge+1)%4]) / 2;
        QVERIFY(original.controlPointEditable(4 + edge));
        QVERIFY(!original.controlPointEditable(4 + seam));
        QVERIFY(!original.controlPointEditable(seam));
        QVERIFY(!original.controlPointEditable((seam+1)%4));

        QImage guides(400,400,QImage::Format_ARGB32); guides.fill(Qt::transparent);
        QPainter painter(&guides); SceneRenderer::Guides options; options.editHandlesVisible = true;
        SceneRenderer(doc).renderGuides(painter, 1, options); painter.end();
        QCOMPARE(guides.pixelColor(midpoint.toPoint()), QColor("#f3f8fa"));

        QPointF axis = midpoint - (corners[(edge+2)%4] + corners[(edge+3)%4]) / 2;
        axis /= QLineF(QPointF(), axis).length();
        controller->pointerPress(midpoint, 1, Qt::NoModifier);
        controller->pointerMove(midpoint + axis * 10, Qt::NoModifier);
        controller->pointerRelease(midpoint + axis * 20, Qt::NoModifier);
        const auto extended = doc.planes()[planeIndex];
        QVERIFY(extended.quad().canvasCorners() != corners);
        QVERIFY(extended.quad().isProjectable());
        QVERIFY(doc.planes()[1-planeIndex] == other);
        QCOMPARE(extended.lockedEdgeMask(), original.lockedEdgeMask());
        QCOMPARE(extended.parentPlaneIndex(), original.parentPlaneIndex());
        QCOMPARE(extended.parentEdgeIndex(), original.parentEdgeIndex());
        const QPointF seamVector = corners[(seam+1)%4] - corners[seam];
        for (int corner : {seam, (seam+1)%4}) {
            const QPointF offset = extended.quad().canvasCorners()[corner] - corners[seam];
            QVERIFY(qAbs(seamVector.x()*offset.y() - seamVector.y()*offset.x()) < 1e-6);
        }
        QVERIFY(doc.canUndo()); controller->undo();
        QVERIFY(doc.planes()[planeIndex] == original); QVERIFY(!doc.canUndo());
        controller->redo(); QVERIFY(doc.planes()[planeIndex] == extended);
    }
};
QTEST_MAIN(AuditInteractionsTest)
#include "tst_auditinteractions.moc"
