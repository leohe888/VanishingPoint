#include "canvas/vpcanvas.h"
#include "testhelpers.h"
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QtTest>

class CanvasProbe : public VpCanvas
{
public:
    using VpCanvas::mousePressEvent;
    using VpCanvas::mouseMoveEvent;
    using VpCanvas::mouseReleaseEvent;
};

class CanvasNavigationTest : public QObject
{
    Q_OBJECT
private slots:
    void historyRestoresPlanesAndDiscardsRedoBranch()
    {
        CanvasProbe canvas;
        auto &document = canvas.controller()->document();
        document.beginEdit();
        document.appendPlane(makeTestPlane());
        document.setSelectedPlane(0);
        document.commitEdit(true);

        canvas.undo();
        QVERIFY(document.planes().isEmpty());
        canvas.redo();
        QCOMPARE(document.planes().size(), 1);
        QCOMPARE(document.selectedPlane(), 0);

        canvas.undo();
        QImage image(8, 8, QImage::Format_ARGB32);
        image.fill(Qt::red);
        document.addFloatingImage(image);
        canvas.redo();
        QVERIFY(document.planes().isEmpty());
        QCOMPARE(document.floatingImages().size(), 1);
        QVERIFY(!document.canRedo());
    }

    void undoFinishesActiveBrushStroke()
    {
        QTemporaryDir directory;
        QImage image(200, 120, QImage::Format_RGB32);
        image.fill(Qt::white);
        const QString path = directory.filePath("background.png");
        QVERIFY(image.save(path));
        CanvasProbe canvas;
        auto &document = canvas.controller()->document();
        QVERIFY(document.loadImage(path));
        document.beginEdit();
        document.appendPlane(makeTestPlane());
        document.commitEdit(true);
        canvas.controller()->setTool(VpController::Brush);
        QVERIFY(canvas.controller()->beginBrush(QPointF(50, 40)));
        QVERIFY(document.hasPaintContent());
        canvas.undo();
        QVERIFY(!canvas.controller()->brushDrawing());
        QVERIFY(!document.hasPaintContent());
        QCOMPARE(document.planes().size(), 1);
        canvas.redo();
        QVERIFY(document.hasPaintContent());
    }

    void zoomAndScroll()
    {
        QTemporaryDir directory;
        QImage image(1000, 800, QImage::Format_RGB32);
        image.fill(Qt::white);
        const QString path = directory.filePath("background.png");
        QVERIFY(image.save(path));
        CanvasProbe canvas;
        canvas.controller()->document().loadImage(path);
        canvas.setSize(QSizeF(500, 400));
        canvas.setZoom(1);
        QCOMPARE(canvas.horizontalSize(), 0.5);
        QCOMPARE(canvas.verticalSize(), 0.5);
        canvas.scrollTo(10, -10);
        QCOMPARE(canvas.horizontalPosition(), 0.5);
        QCOMPARE(canvas.verticalPosition(), 0.0);
        canvas.setZoom(16);
        canvas.zoomStep(false);
        QCOMPARE(canvas.zoom(), 16.0);
        canvas.setZoom(0.063);
        canvas.zoomStep(true);
        QCOMPARE(canvas.zoom(), 0.063);
        canvas.fitView();
        QCOMPARE(canvas.horizontalSize(), 1.0);
        QCOMPARE(canvas.verticalSize(), 1.0);
    }

    void handMovesViewport()
    {
        QTemporaryDir directory;
        QImage image(1000, 800, QImage::Format_RGB32);
        image.fill(Qt::white);
        const QString path = directory.filePath("background.png");
        QVERIFY(image.save(path));
        CanvasProbe canvas;
        canvas.controller()->document().loadImage(path);
        canvas.setSize(QSizeF(500, 400));
        canvas.setZoom(1);
        canvas.scrollTo(0.25, 0.25);
        canvas.controller()->setTool(VpController::Hand);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 100), QPointF(100, 100),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        canvas.mousePressEvent(&press);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(150, 140), QPointF(150, 140),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        canvas.mouseReleaseEvent(&release);
        QCOMPARE(canvas.horizontalPosition(), 0.2);
        QCOMPARE(canvas.verticalPosition(), 0.2);
        canvas.controller()->setTool(VpController::Zoom);
        canvas.mousePressEvent(&press);
        QCOMPARE(canvas.zoom(), 2.0);
        QMouseEvent altPress(QEvent::MouseButtonPress, QPointF(100, 100), QPointF(100, 100),
                             Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
        canvas.mousePressEvent(&altPress);
        QCOMPARE(canvas.zoom(), 1.0);
    }
};

QTEST_MAIN(CanvasNavigationTest)
#include "tst_canvasnavigation.moc"
