#include "canvas/vpcanvas.h"
#include "core/scenerenderer.h"
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
        canvas.controller()->setTool(VpController::EditPlane);
    }
    static QImage rendered(const VpDocument &doc) {
        QImage image(400,400,QImage::Format_ARGB32); image.fill(Qt::transparent);
        QPainter painter(&image); SceneRenderer(doc).render(painter,1,false); return image;
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
        controller->setTool(VpController::Brush); QVERIFY(controller->beginBrush({50,50}));
        QGuiApplication::clipboard()->setImage(bitmap(Qt::red)); controller->pasteImage();
        QVERIFY(!controller->brushDrawing()); controller->moveBrush({80,50}); controller->endBrush();
        canvas.undo(); canvas.undo(); QVERIFY(!doc.hasPaintContent());
        canvas.redo(); QVERIFY(doc.hasPaintContent()); canvas.redo(); QCOMPARE(doc.floatingImages().size(),1);
    }
    void lockedCornerAndNoopPreserveHistory() {
        AuditCanvas canvas; setup(canvas); auto &doc=canvas.controller()->document(); auto p=plane();
        p.setEdgeLocked(0,true); doc.appendPlane(p); doc.resetHistory();
        canvas.pointer(QEvent::MouseButtonPress,{100,100}); canvas.pointer(QEvent::MouseButtonRelease,{110,110});
        QCOMPARE(doc.planes()[0].quad().canvasCorners()[0],QPointF(100,100)); QVERIFY(!doc.canUndo());
        doc.addFloatingImage(bitmap(Qt::red)); doc.undo(); QVERIFY(doc.canRedo());
        canvas.pointer(QEvent::MouseButtonPress,{150,150}); canvas.pointer(QEvent::MouseButtonRelease,{150,150});
        QVERIFY(doc.canRedo());
    }
    void extrapolatedMoveUsesReleaseAndCanUndo() {
        AuditCanvas canvas; setup(canvas); auto &doc=canvas.controller()->document(); auto p=plane();
        doc.appendPlane(p); doc.addFloatingImageOnSurface(bitmap(Qt::red),{p.quad()},0,{250,100}); doc.resetHistory();
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
    void releaseMovesPlaneWithoutMoveEvent() {
        AuditCanvas canvas; setup(canvas); auto &doc=canvas.controller()->document(); doc.appendPlane(plane()); doc.resetHistory();
        canvas.pointer(QEvent::MouseButtonPress,{150,150}); canvas.pointer(QEvent::MouseButtonRelease,{160,150});
        QCOMPARE(doc.planes()[0].quad().canvasCorners()[0],QPointF(110,100)); QVERIFY(doc.canUndo());
    }
};
QTEST_MAIN(AuditInteractionsTest)
#include "tst_auditinteractions.moc"
