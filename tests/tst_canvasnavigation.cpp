#include <limits>
#include <memory>
#include <QCursor>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTemporaryDir>
#include <QtTest>

#include "canvas/auxiliaryoverlayitem.h"
#include "canvas/floatingcontentitem.h"
#include "canvas/vpcanvas.h"
#include "core/scenerenderer.h"
#include "testhelpers.h"

class CanvasProbe : public VpCanvas
{
public:
    using VpCanvas::mousePressEvent;
    using VpCanvas::mouseMoveEvent;
    using VpCanvas::mouseReleaseEvent;
};

class HighlightCanvas final : public VpCanvas {
public:
    using VpCanvas::hoverMoveEvent;
    using VpCanvas::hoverLeaveEvent;
    using VpCanvas::keyPressEvent;
    using VpCanvas::keyReleaseEvent;
};

class CanvasNavigationTest : public QObject
{
    Q_OBJECT
private slots:
    void marqueePointerAccuracy_data();
    void marqueePointerAccuracy();
    void paintAndMarqueeHighlightRendering();
    void clipsContentAtCanvasBoundaryAndPreservesGuides();
    void externalControllerSurvivesCanvas()
    {
        VpController controller;
        {
            CanvasProbe canvas; canvas.setController(&controller);
            controller.document().beginEdit();
            controller.document().appendPlane(makeTestPlane());
            controller.document().commitEdit(true);
        }
        QCOMPARE(controller.document().planes().size(), 1);
        QVERIFY(controller.canUndo());
        CanvasProbe recreated; recreated.setController(&controller);
        QCOMPARE(recreated.controller(), &controller);
        recreated.undo(); QVERIFY(controller.document().planes().isEmpty());
    }

    void controllerReplacementDisconnectsOldSignals()
    {
        VpController first, second;
        CanvasProbe canvas; canvas.setController(&first);
        first.setTool(VpTools::Hand);
        QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
        canvas.setController(&second);
        QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
        first.setTool(VpTools::Zoom);
        QCOMPARE(canvas.cursor().shape(), Qt::ArrowCursor);
        second.setTool(VpTools::Hand);
        QCOMPARE(canvas.cursor().shape(), Qt::OpenHandCursor);
    }

    void destroyedControllerLeavesCanvasSafe()
    {
        CanvasProbe canvas;
        auto controller = std::make_unique<VpController>();
        canvas.setController(controller.get());
        QSignalSpy changed(&canvas, &VpCanvas::controllerChanged);
        controller.reset();
        QCOMPARE(canvas.controller(), nullptr); QCOMPARE(changed.size(), 1);
        canvas.setSize({100, 100}); canvas.setZoom(2); canvas.scrollTo(0.5, 0.5);
        canvas.undo(); canvas.redo();
        QCOMPARE(canvas.horizontalSize(), 1.0); QCOMPARE(canvas.verticalSize(), 1.0);
        QCOMPARE(canvas.horizontalPosition(), 0.0); QCOMPARE(canvas.verticalPosition(), 0.0);
        QImage image(100,100,QImage::Format_ARGB32); QPainter painter(&image);
        canvas.paint(&painter);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(50,50), QPointF(50,50),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        canvas.mousePressEvent(&press); QVERIFY(!press.isAccepted());
    }

    void historyRestoresPlanesAndDiscardsRedoBranch()
    {
        VpController controller;
        CanvasProbe canvas; canvas.setController(&controller);
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
        document.addFloatingSelection(makeTestSelection(image));
        canvas.redo();
        QVERIFY(document.planes().isEmpty());
        QCOMPARE(document.floatingSelections().size(), 1);
        QVERIFY(!document.canRedo());
    }

    void undoFinishesActiveBrushStroke()
    {
        QTemporaryDir directory;
        QImage image(200, 120, QImage::Format_RGB32);
        image.fill(Qt::white);
        const QString path = directory.filePath("background.png");
        QVERIFY(image.save(path));
        VpController controller;
        CanvasProbe canvas; canvas.setController(&controller);
        auto &document = canvas.controller()->document();
        QVERIFY(document.setImage(QImage(path)));
        document.beginEdit();
        document.appendPlane(makeTestPlane());
        document.commitEdit(true);
        canvas.controller()->setTool(VpTools::Brush);
        QVERIFY(canvas.controller()->beginBrush(QPointF(50, 40)));
        QVERIFY(document.hasPaintContent());
        canvas.undo();
        QVERIFY(!canvas.controller()->brushDrawing());
        QVERIFY(!document.hasPaintContent());
        QCOMPARE(document.planes().size(), 1);
        canvas.redo();
        QVERIFY(document.hasPaintContent());
    }

    void scrollBarRanges()
    {
        QTemporaryDir directory;
        QImage image(1000, 800, QImage::Format_RGB32);
        image.fill(Qt::white);
        const QString path = directory.filePath("background.png");
        QVERIFY(image.save(path));
        VpController controller;
        CanvasProbe canvas; canvas.setController(&controller);
        canvas.controller()->document().setImage(QImage(path));
        canvas.setSize(QSizeF(500, 400));
        canvas.setZoom(1);
        QCOMPARE(canvas.horizontalSize(), 0.5);
        QCOMPARE(canvas.verticalSize(), 0.5);
        canvas.scrollTo(10, -10);
        QCOMPARE(canvas.horizontalPosition(), 0.5);
        QCOMPARE(canvas.verticalPosition(), 0.0);
    }

};


// AC12: exercise the production mouse -> image -> surface -> outline path.
// Distances are in Qt logical display pixels, independent of image zoom.
void CanvasNavigationTest::marqueePointerAccuracy_data()
{
    QTest::addColumn<qreal>("scale");
    QTest::addColumn<bool>("perspective");
    QTest::addColumn<bool>("scrolled");
    QTest::addColumn<QRectF>("region");
    QTest::addColumn<bool>("reverse");
    const QRectF regions[] = {{80,80,40,40}, {2,80,20,40}, {178,80,20,40},
                              {80,2,40,20}, {80,178,40,20}};
    const char *names[] = {"center", "left", "right", "top", "bottom"};
    for (qreal scale : {0.5, 1.0, 2.0})
        for (bool perspective : {false, true})
            for (bool scrolled : {false, true})
                for (int i = 0; i < 5; ++i)
                    for (bool reverse : {false, true}) {
                        const QByteArray name = QString("%1pct-%2-%3-%4-%5")
                            .arg(scale * 100).arg(perspective ? "perspective" : "rectangle")
                            .arg(scrolled ? "scrolled" : "centered").arg(names[i])
                            .arg(reverse ? "reverse" : "forward").toLatin1();
                        QTest::newRow(name.constData()) << scale << perspective << scrolled << regions[i] << reverse;
                    }
}

void CanvasNavigationTest::marqueePointerAccuracy()
{
    QFETCH(qreal, scale);
    QFETCH(bool, perspective);
    QFETCH(bool, scrolled);
    QFETCH(QRectF, region);
    QFETCH(bool, reverse);
    VpController controller;
    CanvasProbe canvas;
    canvas.setController(&controller);
    canvas.setSize({640,480});
    QImage image(1600,1200,QImage::Format_RGB32);
    image.fill(Qt::white);
    QVERIFY(controller.document().setImage(image));
    const PerspectiveQuad::Corners corners = perspective
        ? PerspectiveQuad::Corners{{{720,510},{900,500},{880,690},{700,700}}}
        : PerspectiveQuad::Corners{{{700,500},{900,500},{900,700},{700,700}}};
    const PerspectivePlane plane(corners, {{{0,0},{200,0},{200,200},{0,200}}});
    QVERIFY(plane.quad().isProjectable());
    controller.document().appendPlane(plane);
    controller.setTool(VpTools::Marquee);
    canvas.setZoom(scale);
    // Specify the view independently of the canvas's private coordinate helpers.
    const QPointF offset((640 - 1600 * scale) / 2 + (scrolled ? 30 : 0),
                         (480 - 1200 * scale) / 2 + (scrolled ? -20 : 0));
    canvas.scrollTo(-offset.x() / (1600 * scale), -offset.y() / (1200 * scale));
    const auto mapping = plane.quad().surfaceToCanvasTransform();
    const auto displayPoint = [&](QPointF surface) {
        return mapping.forward().map(surface) * scale + offset;
    };
    const QPointF start = reverse ? region.bottomRight() : region.topLeft();
    const QPointF finish = reverse ? region.topLeft() : region.bottomRight();
    const auto pointer = [&](QEvent::Type type, QPointF surface) {
        const QPointF point = displayPoint(surface);
        QMouseEvent event(type, point, point,
                         type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                         type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton,
                         Qt::NoModifier);
        if (type == QEvent::MouseButtonPress) canvas.mousePressEvent(&event);
        else if (type == QEvent::MouseMove) canvas.mouseMoveEvent(&event);
        else canvas.mouseReleaseEvent(&event);
    };
    const auto verifyCorners = [&](QRectF expected) {
        const QPainterPath outline = controller.selectionOutline();
        QVERIFY2(!outline.isEmpty(), "Drag must create a nonempty selection");
        qreal maximum = 0;
        for (QPointF surface : {expected.topLeft(), expected.topRight(), expected.bottomRight(), expected.bottomLeft()}) {
            const QPointF target = displayPoint(surface);
            QVERIFY2(QRectF(0,0,640,480).contains(target), "Test corner must be visible in viewport");
            qreal error = std::numeric_limits<qreal>::max();
            for (int i = 0; i < outline.elementCount(); ++i) {
                const auto vertex = outline.elementAt(i);
                error = qMin(error, QLineF(QPointF(vertex.x,vertex.y) * scale + offset, target).length());
            }
            maximum = qMax(maximum, error);
            QVERIFY2(error <= 2.0, qPrintable(QString("AC12: corner (%1,%2), error=%3 logical px (limit 2)")
                .arg(surface.x()).arg(surface.y()).arg(error,0,'g',12)));
        }
        qInfo("Maximum corner error: %.9g logical px", double(maximum));
    };
    pointer(QEvent::MouseButtonPress, start);
    pointer(QEvent::MouseMove, region.center());
    verifyCorners(QRectF(start, region.center()).normalized());
    // Release at a different position to also catch a stale last-move endpoint.
    pointer(QEvent::MouseButtonRelease, finish);
    verifyCorners(region);
}

void CanvasNavigationTest::clipsContentAtCanvasBoundaryAndPreservesGuides()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    const QSize canvasSize(100,80), viewport(300,240);
    const QPointF offset(70,60);
    FloatingSelection image;
    image.bitmap=QImage(40,30,QImage::Format_ARGB32_Premultiplied);image.bitmap.fill(Qt::red);
    const auto bitmap=image.bitmap;
    QQuickWindow window;
    FloatingContentItem item(window.contentItem());
    for (qreal scale : {.5,1.0,1.5}) for (QPointF origin : {QPointF(-20,20),QPointF(80,20),
            QPointF(20,-15),QPointF(20,65),QPointF(-70,20)}) for (bool attached : {false,true}) {
        image.placementOrigin=origin;image.rotationDegrees=15;image.surfaceAttached=attached;
        image.hostQuadIndex=attached?0:-1;
        image.surfaceQuads.clear();
        if (attached) image.surfaceQuads.append(PerspectiveQuad(
            {{{-100,-100},{200,-100},{200,180},{-100,180}}},
            {{{-100,-100},{200,-100},{200,180},{-100,180}}}));
        item.setImages({image},scale,offset,viewport,canvasSize);
        QImage actual(viewport,QImage::Format_ARGB32_Premultiplied);actual.fill(Qt::transparent);
        if(item.isVisible()) {
            QPainter painter(&actual);painter.translate(item.position());item.paint(&painter);
        }
        QImage expected(viewport,QImage::Format_ARGB32_Premultiplied);expected.fill(Qt::transparent);
        {
            QPainter painter(&expected);painter.setRenderHints(QPainter::Antialiasing|QPainter::SmoothPixmapTransform);
            painter.translate(offset);painter.scale(scale,scale);
            painter.setClipRect(QRectF(QPointF(),canvasSize));
            SceneRenderer::renderFloatingSelection(painter,image,scale);
        }
        check(actual==expected,"floating display clips only pixels at image boundary for all edges and zooms");
        check(image.bitmap==bitmap,"display clipping preserves original bitmap");
        check(!SceneRenderer::floatingSelectionOutline(image).isEmpty(),"outline survives pixel clipping");
    }
    VpController controller;
    QImage background(canvasSize,QImage::Format_ARGB32_Premultiplied);background.fill(Qt::white);
    controller.document().setImage(background);
    image.surfaceAttached=false;image.surfaceQuads.clear();image.hostQuadIndex=-1;
    image.rotationDegrees=0;image.placementOrigin=QPointF(-20,20);
    controller.document().addFloatingSelection(image);
    QImage rendered(viewport,QImage::Format_ARGB32_Premultiplied);rendered.fill(Qt::transparent);
    {
        QPainter painter(&rendered);painter.translate(offset);
        SceneRenderer(controller.document()).renderContent(painter,1);
        check(!painter.hasClipping(),"content clipping is restored before rendering guides");
        SceneRenderer(controller.document()).renderSelectionGuides(painter);
    }
    check(qAlpha(rendered.pixel(55,90))==0,"scene renderer hides out-of-image floating pixels");
    check(qAlpha(rendered.pixel(50,90))>0,"scene renderer keeps ants outside image boundary");
    VpCanvas canvas;canvas.setSize(viewport);canvas.setController(&controller);canvas.setZoom(1);
    bool found=false;
    for (QQuickItem *child : canvas.childItems()) if(auto *content=dynamic_cast<FloatingContentItem *>(child)) {
        if(!content->isVisible()) continue;
        found=true;QImage layer(viewport,QImage::Format_ARGB32_Premultiplied);layer.fill(Qt::transparent);
        QPainter painter(&layer);painter.translate(content->position());content->paint(&painter);painter.end();
        // Centered 100x80 image in a 300x240 viewport starts at (100,80).
        check(qAlpha(layer.pixel(90,110))==0 && qRed(layer.pixel(110,110))>0,
              "live canvas passes image boundary to floating content items");
    }
    check(found,"live canvas has floating image layer");
}

void CanvasNavigationTest::paintAndMarqueeHighlightRendering()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    VpController controller;
    QImage background(280,220,QImage::Format_ARGB32_Premultiplied);background.fill(Qt::white);
    controller.document().setImage(background);
    for(int x : {20,100}) {
        const PerspectiveQuad::Corners corners{{QPointF(x,20),QPointF(x+140,20),
                                                QPointF(x+140,180),QPointF(x,180)}};
        controller.document().appendPlane(PerspectivePlane(corners,corners));
    }
    QQuickWindow window;window.resize(280,220);window.show();QCoreApplication::processEvents();
    HighlightCanvas canvas;canvas.setParentItem(window.contentItem());canvas.setSize(QSizeF(280,220));
    controller.setTool(VpTools::Brush);canvas.setController(&controller);canvas.setZoom(1);
    const auto expected=[&](int highlighted) {
        QImage image(280,220,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        QPainter painter(&image);painter.setRenderHint(QPainter::Antialiasing);
        SceneRenderer::Guides guides;guides.highlightedPlane=highlighted;
        SceneRenderer(controller.document()).renderGuides(painter,1,guides);
        painter.end();return image;
    };
    const auto actual=[&]() {
        QImage image(280,220,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
        QPainter painter(&image);bool found=false;
        for(auto *child:canvas.childItems()) if(auto *overlay=dynamic_cast<AuxiliaryOverlayItem *>(child))
            for(auto *item:overlay->childItems()) if(auto *raster=dynamic_cast<AuxiliaryRasterItem *>(item)) {
                found=!raster->pictures.isEmpty();raster->paint(&painter);
            }
        check(found,"canvas has cached software guide commands");painter.end();return image;
    };
    const auto hover=[&](const QPointF &point) {
        QHoverEvent event(QEvent::HoverMove,point,QPointF());canvas.hoverMoveEvent(&event);
    };
    const QImage inactive=expected(-1), first=expected(0), second=expected(1);
    check(first!=inactive && second!=inactive,"highlight increases target border emphasis");
    check(qAlpha(first.pixel(50,100))==0,"hover highlight adds no grid or control points");
    hover(QPointF(50,100));check(actual()==first,"canvas highlights first hovered plane without selecting it");
    for(auto *child:canvas.childItems()) if(auto *overlay=dynamic_cast<AuxiliaryOverlayItem *>(child)) {
        const auto builds=overlay->planeGeometryBuildCount();
        hover(QPointF(60,100));
        check(overlay->planeGeometryBuildCount()==builds,"hovering within same plane reuses guide geometry");
    }
    hover(QPointF(220,100));check(actual()==second,"canvas hover switches target plane border");
    QHoverEvent leave(QEvent::HoverLeave,QPointF(),QPointF(220,100));canvas.hoverLeaveEvent(&leave);
    check(actual()==inactive,"leaving canvas clears hover border");
    hover(QPointF(50,100));
    QKeyEvent down(QEvent::KeyPress,Qt::Key_Space,Qt::NoModifier),up(QEvent::KeyRelease,Qt::Key_Space,Qt::NoModifier);
    canvas.keyPressEvent(&down);check(actual()==inactive,"temporary hand hides painting border");
    canvas.keyReleaseEvent(&up);check(actual()==first,"returning from hand restores painting border");
    controller.pointerPress(QPointF(50,100),1,Qt::NoModifier);
    hover(QPointF(220,100));controller.pointerMove(QPointF(220,100),Qt::NoModifier);
    check(actual()==first,"canvas keeps locked border during cross-plane stroke");
    controller.pointerRelease(QPointF(220,100),Qt::NoModifier);
    check(actual()==second && controller.document().selectedPlane()==-1,"release refreshes hover without selecting plane");
    controller.setTool(VpTools::Marquee);check(actual()==second,"marquee shares hovered plane highlight");
    hover(QPointF(50,100));check(actual()==first,"marquee highlights first plane without selecting it");
    hover(QPointF(120,100));check(actual()==second,"marquee highlights topmost overlapping plane");
    check(controller.document().selectedPlane()==-1,"marquee highlight does not select a plane");
    canvas.hoverLeaveEvent(&leave);check(actual()==inactive,"marquee leave clears border");
    hover(QPointF(50,100));
    canvas.keyPressEvent(&down);check(actual()==inactive,"marquee temporary hand hides border");
    canvas.keyReleaseEvent(&up);check(actual()==first,"marquee return from hand restores border");
    controller.setTool(VpTools::EditPlane);check(actual()==inactive,"other tools clear target highlight");
}

QTEST_MAIN(CanvasNavigationTest)
#include "tst_canvasnavigation.moc"
