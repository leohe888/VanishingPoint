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
    void setup(AuditCanvas &canvas, VpController &controller) {
        canvas.setController(&controller);
        QVERIFY(canvas.controller()->document().setImage(QImage(path)));
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
        QFETCH(int,count); VpController testController; AuditCanvas canvas; setup(canvas, testController); auto &doc=canvas.controller()->document();
        for(int i=0;i<count;++i) doc.addFloatingSelection(makeTestSelection(bitmap(i?Qt::blue:Qt::red)));
        canvas.pointer(QEvent::MouseButtonPress,{10,10}); canvas.deleteSelection();
        canvas.pointer(QEvent::MouseMove,{30,30}); canvas.pointer(QEvent::MouseButtonRelease,{40,40});
        QCOMPARE(doc.floatingSelections().size(),count-1);
        if(count>1) QCOMPARE(doc.floatingSelection(0).placementOrigin,QPointF());
        canvas.undo(); QCOMPARE(doc.floatingSelections().size(),count);
    }
    void extrapolatedMoveUsesReleaseAndCanUndo() {
        VpController testController; AuditCanvas canvas; setup(canvas, testController); auto &doc=canvas.controller()->document(); auto p=plane();
        doc.appendPlane(p); doc.addFloatingSelectionOnSurface(bitmap(Qt::red),{p.quad()},0,{250,100}); doc.resetHistory(); canvas.controller()->setTool(VpTools::EditPlane);
        canvas.pointer(QEvent::MouseButtonPress,{360,210}); canvas.pointer(QEvent::MouseButtonRelease,{370,210});
        QCOMPARE(doc.floatingSelection(0).placementOrigin,QPointF(260,100)); QVERIFY(doc.canUndo());
        canvas.undo(); QCOMPARE(doc.floatingSelection(0).placementOrigin,QPointF(250,100));
    }
    void bakePreservesLayerOrderAndUndo() {
        VpController testController; AuditCanvas canvas; setup(canvas, testController); auto &doc=canvas.controller()->document();
        doc.addFloatingSelection(makeTestSelection(bitmap(Qt::red))); doc.addFloatingSelection(makeTestSelection(bitmap(Qt::blue)));
        auto upper=bitmap(Qt::green); doc.addFloatingSelection(makeTestSelection(upper)); doc.setSelectedFloatingSelection(1);
        const auto before=rendered(doc); canvas.pointer(QEvent::MouseButtonPress,{300,300});
        QCOMPARE(rendered(doc),before); QCOMPARE(doc.floatingSelections().size(),1);
        canvas.undo(); QCOMPARE(doc.floatingSelections().size(),3); QCOMPARE(rendered(doc),before);
        canvas.redo(); QCOMPARE(rendered(doc),before);
    }
    void bakingDisablesTransform_data() {
        QTest::addColumn<bool>("hasPlane");
        QTest::newRow("no-plane") << false;
        QTest::newRow("with-plane") << true;
    }
    void bakingDisablesTransform() {
        QFETCH(bool, hasPlane);
        VpController testController; AuditCanvas canvas; setup(canvas, testController);
        auto *controller = canvas.controller(); auto &doc = controller->document();
        if (hasPlane) doc.appendPlane(plane());
        doc.resetHistory(); doc.addFloatingSelection(makeTestSelection(bitmap(Qt::red)));
        controller->setTool(VpTools::Transform);
        controller->pointerPress({300,300}, 1, Qt::NoModifier);
        QVERIFY(doc.floatingSelections().isEmpty()); QVERIFY(doc.hasPaintContent());
        QVERIFY(!controller->isToolEnabled(VpTools::Transform));
        QCOMPARE(controller->tool(), hasPlane ? VpTools::EditPlane : VpTools::CreatePlane);
        controller->undo();
        QCOMPARE(doc.floatingSelections().size(), 1); QVERIFY(!doc.hasPaintContent());
        QVERIFY(controller->isToolEnabled(VpTools::Transform));
        controller->redo();
        QVERIFY(doc.floatingSelections().isEmpty()); QVERIFY(doc.hasPaintContent());
        QVERIFY(!controller->isToolEnabled(VpTools::Transform));
    }
    void pasteCancelsPreview_data() {
        QTest::addColumn<QString>("kind");
        for (const QString kind : {"plane", "image"}) QTest::newRow(qPrintable(kind))<<kind;
    }
    void pasteCancelsPreview() {
        QFETCH(QString,kind); VpController testController; AuditCanvas canvas; setup(canvas, testController); auto *controller=canvas.controller(); auto &doc=controller->document();
        doc.appendPlane(plane());
        if(kind=="image") doc.addFloatingSelection(makeTestSelection(bitmap(Qt::red)));
        doc.resetHistory(); canvas.controller()->setTool(VpTools::EditPlane);
        if(kind=="plane") {
            controller->pointerPress({150,150},1,Qt::NoModifier); controller->pointerMove({160,150},Qt::NoModifier);
        } else {
            controller->pointerPress({10,10},1,Qt::NoModifier); controller->pointerMove({20,20},Qt::NoModifier);
        }
        QGuiApplication::clipboard()->setImage(bitmap(Qt::blue)); controller->pasteSelection();
        QVERIFY(controller->selectionPreview().bitmap.isNull());
        QVERIFY(!doc.editActive()); QVERIFY(!doc.hasPaintContent());
        QCOMPARE(doc.planes()[0].quad().canvasCorners()[0],QPointF(100,100));
        canvas.undo(); QCOMPARE(doc.floatingSelections().size(),kind=="image"?1:0);
        if(kind=="image") QCOMPARE(doc.floatingSelection(0).placementOrigin,QPointF());
        canvas.redo(); QCOMPARE(doc.floatingSelections().size(),kind=="image"?2:1);
    }
    void initializationResetsDocumentAndPreservesFailure() {
        VpController testController; AuditCanvas canvas; setup(canvas, testController); auto *controller=canvas.controller(); auto &doc=controller->document();
        doc.appendPlane(makeTestPlane()); doc.resetHistory();
        // The host initializes an image before interaction; reopening creates a new controller.
        QVERIFY(controller->pickCloneSource({20,20}));
        QVERIFY(controller->document().setImage(QImage(path)));
        QVERIFY(!controller->hasCloneSource()); QVERIFY(!controller->brushDrawing()); QVERIFY(!doc.hasPaintContent()); QVERIFY(!doc.canUndo());
        doc.addFloatingSelection(makeTestSelection(bitmap(Qt::red))); const auto before=rendered(doc);
        QVERIFY(!controller->document().setImage(QImage(directory.filePath("missing.png"))));
        QCOMPARE(rendered(doc),before); QVERIFY(doc.canUndo());
    }
};
QTEST_MAIN(AuditInteractionsTest)
#include "tst_auditinteractions.moc"
