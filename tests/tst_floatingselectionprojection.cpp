#include <QCoreApplication>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QPainter>
#include <QPainterPath>
#include <QQuickRenderControl>
#include <QQuickGraphicsDevice>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QtTest>

#include "canvas/auxiliaryoverlayitem.h"
#include "canvas/vpcanvas.h"
#include "canvas/vpcontroller.h"
#include "core/floatingselectionprojection.h"
#include "core/scenerenderer.h"

namespace {

PerspectiveQuad leftQuad()
{
    return {
        {QPointF(10, 10), QPointF(110, 10), QPointF(110, 80), QPointF(10, 80)},
        {QPointF(0, 0), QPointF(100, 0), QPointF(100, 70), QPointF(0, 70)}
    };
}

PerspectiveQuad rightQuad()
{
    return {
        {QPointF(110, 10), QPointF(230, 30), QPointF(230, 96), QPointF(110, 80)},
        {QPointF(100, 0), QPointF(200, 0), QPointF(200, 70), QPointF(100, 70)}
    };
}

FloatingSelection imageAcrossQuadSeam()
{
    FloatingSelection image;
    image.bitmap = QImage(200, 70, QImage::Format_ARGB32);
    image.bitmap.fill(Qt::red);
    image.placementOrigin = QPointF(0, 0);
    image.surfaceAttached = true;
    image.hostQuadIndex = 0;
    image.surfaceQuads = QVector<PerspectiveQuad>{leftQuad(), rightQuad()};
    return image;
}

QPointF cornerAverage(const PerspectiveQuad &quad)
{
    QPointF sum;
    for (const QPointF &corner : quad.canvasPolygon())
        sum += corner / 4.0;
    return sum;
}

qreal centerDeviation(const PerspectiveQuad &quad)
{
    FloatingSelection image;
    image.bitmap = QImage(100, 70, QImage::Format_ARGB32);
    image.bitmap.fill(Qt::red);
    image.placementOrigin = quad.surfaceCorners()[0];
    image.surfaceAttached = true;
    image.hostQuadIndex = 0;
    image.surfaceQuads = QVector<PerspectiveQuad>{quad};

    const auto projection = FloatingSelectionProjection::forSelection(image);
    if (projection->patches().size() != 1)
        return -1;
    return QLineF(projection->patches().first().bitmapToCanvas.forward().map(QPointF(50, 35)),
                  cornerAverage(quad)).length();
}

} // namespace

class FloatingSelectionProjectionTest : public QObject
{
    Q_OBJECT

private slots:
    void selectionOutlineRendering();
    void clipsAcrossSurfacesWithoutLosingPixels();
    void detachedImageProducesSinglePatch();
    void attachedImageSpansEveryQuad();
    void hitTestCoversEveryQuad();
    void perspectiveQuadUsesProjectiveMapping();
    void affineQuadUsesLinearMapping();
    void attachedCoordinatesRoundTrip();
};

void FloatingSelectionProjectionTest::detachedImageProducesSinglePatch()
{
    FloatingSelection image;
    image.bitmap = QImage(40, 30, QImage::Format_ARGB32);
    image.bitmap.fill(Qt::red);
    image.placementOrigin = QPointF(100, 50);

    const auto projection = FloatingSelectionProjection::forSelection(image);
    QCOMPARE(projection->patches().size(), 1);
    QCOMPARE(projection->canvasOutline().boundingRect(), QRectF(100, 50, 40, 30));
}

void FloatingSelectionProjectionTest::attachedImageSpansEveryQuad()
{
    QCOMPARE(FloatingSelectionProjection::forSelection(imageAcrossQuadSeam())->patches().size(), 2);
}

void FloatingSelectionProjectionTest::hitTestCoversEveryQuad()
{
    const auto projection = FloatingSelectionProjection::forSelection(imageAcrossQuadSeam());
    QVERIFY(projection->hitTest(QPointF(60, 45)));
    QVERIFY(projection->hitTest(QPointF(170, 55)));
    QVERIFY(!projection->hitTest(QPointF(400, 400)));
}

void FloatingSelectionProjectionTest::perspectiveQuadUsesProjectiveMapping()
{
    QVERIFY(centerDeviation(rightQuad()) > 0.5);
}

void FloatingSelectionProjectionTest::affineQuadUsesLinearMapping()
{
    QVERIFY(centerDeviation(leftQuad()) < 0.5);
}

void FloatingSelectionProjectionTest::attachedCoordinatesRoundTrip()
{
    const FloatingSelection image = imageAcrossQuadSeam();
    const QPointF placementPoint(150, 35);
    const QPointF canvasPoint = image.mapPlacementToCanvas(placementPoint);
    QPointF mappedBack;
    QVERIFY(image.mapCanvasToPlacement(canvasPoint, &mappedBack));
    QVERIFY(QLineF(placementPoint, mappedBack).length() < 1e-6);
}

void FloatingSelectionProjectionTest::clipsAcrossSurfacesWithoutLosingPixels()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    const PerspectiveQuad first(
        {QPointF(20,20), QPointF(120,20), QPointF(120,120), QPointF(20,120)},
        {QPointF(0,0), QPointF(100,0), QPointF(100,100), QPointF(0,100)});
    const PerspectiveQuad second(
        {QPointF(120,20), QPointF(220,40), QPointF(220,100), QPointF(120,120)},
        {QPointF(100,0), QPointF(200,0), QPointF(200,100), QPointF(100,100)});
    FloatingSelection image;
    image.bitmap = QImage(80,40,QImage::Format_ARGB32_Premultiplied); image.bitmap.fill(Qt::red);
    image.surfaceAttached = true; image.surfaceQuads = {first, second}; image.hostQuadIndex = 0;
    image.opacity = 50;
    const QImage original = image.bitmap;
    const auto render = [](const FloatingSelection &selection) {
        QImage output(400,220,QImage::Format_ARGB32_Premultiplied); output.fill(Qt::transparent);
        QPainter painter(&output); SceneRenderer::renderFloatingSelection(painter, selection);
        return output;
    };
    const auto halfAlpha = [](QRgb pixel) { return qAlpha(pixel) >= 126 && qAlpha(pixel) <= 128; };
    image.placementOrigin = QPointF(-20,30);
    auto projection = FloatingSelectionProjection::forSelection(image);
    auto output = render(image);
    check(qAlpha(output.pixel(10,70)) == 0 && halfAlpha(output.pixel(30,70)),
          "partial exit clips outside host without changing opacity");
    check(!projection->hitTest(QPointF(10,70)) && projection->hitTest(QPointF(30,70)),
          "clipped pixels cannot be grabbed");

    image.placementOrigin = QPointF(90,30);
    projection = FloatingSelectionProjection::forSelection(image); output = render(image);
    check(projection->patches().size() == 2, "selection projects across both planes");
    check(halfAlpha(output.pixel(115,70)) && halfAlpha(output.pixel(150,70)),
          "both planes retain selection opacity");
    check(halfAlpha(output.pixel(119,70)) && halfAlpha(output.pixel(120,70)),
          "shared edge has no gap or doubled alpha");
    check(projection->hitTest(image.mapPlacementToCanvas(QPointF(130,50))),
          "neighbouring plane supports hit testing");

    image.placementOrigin = QPointF(160,30);
    projection = FloatingSelectionProjection::forSelection(image); output = render(image);
    check(projection->patches().size() == 1 && qAlpha(output.pixel(250,70)) == 0,
          "neighbour intersection keeps clipping after host is fully left");
    check(projection->hitTest(image.mapPlacementToCanvas(QPointF(180,50))),
          "remaining neighbour fragment stays interactive");

    image.placementOrigin = QPointF(220,30);
    projection = FloatingSelectionProjection::forSelection(image); output = render(image);
    check(halfAlpha(output.pixel(250,70)) && halfAlpha(output.pixel(310,70)),
          "leaving entire surface group restores full image");
    check(projection->hitTest(QPointF(310,70)), "restored outside pixels can be grabbed");
    image.placementOrigin = QPointF(160,30);
    check(qAlpha(render(image).pixel(250,70)) == 0 && image.bitmap == original,
          "reentry restores clipping without losing pixels");

    // Transforming the selection uses the same bitmap-space intersections.
    image.placementOrigin = QPointF(130,25); image.scaleFactors = QPointF(1.4,.8); image.rotationDegrees = 25;
    projection = FloatingSelectionProjection::forSelection(image);
    for (const auto &patch : projection->patches()) {
        QPainterPath surfaces;
        surfaces.addPolygon(first.canvasPolygon()); surfaces.closeSubpath();
        QPainterPath neighbour; neighbour.addPolygon(second.canvasPolygon()); neighbour.closeSubpath();
        surfaces = surfaces.united(neighbour);
        check(patch.canvasClip.subtracted(surfaces).boundingRect().width() < .001,
              "scaled rotated fragments remain within surface union");
    }

    // Overlapping neighbour domains must not paint the same pixels twice.
    image.scaleFactors = QPointF(1,1); image.rotationDegrees = 0;
    image.placementOrigin = QPointF(90,30); image.surfaceQuads = {first, second, second};
    check(halfAlpha(render(image).pixel(150,70)), "overlapping planes do not double opacity");

    // A gap between planes is clipped while either plane is still intersected.
    const PerspectiveQuad separated(
        {QPointF(160,20), QPointF(260,20), QPointF(260,120), QPointF(160,120)},
        {QPointF(140,0), QPointF(240,0), QPointF(240,100), QPointF(140,100)});
    image.surfaceQuads = {first, separated};
    image.bitmap = QImage(100,40,QImage::Format_ARGB32_Premultiplied); image.bitmap.fill(Qt::red);
    output = render(image);
    check(halfAlpha(output.pixel(115,70)) && qAlpha(output.pixel(140,70)) == 0
          && halfAlpha(output.pixel(175,70)), "multi-plane gap clips without hiding either fragment");

    // Controller drag and history preserve the original bitmap across transitions.
    VpController controller;
    QImage background(400,220,QImage::Format_ARGB32_Premultiplied); background.fill(Qt::white);
    controller.document().setImage(background);
    controller.document().appendPlane(PerspectivePlane(first.canvasCorners(), first.surfaceCorners()));
    image.surfaceQuads = {first}; image.bitmap = original; image.placementOrigin = QPointF(30,30);
    const int index = controller.document().addFloatingSelection(image);
    controller.document().commitHistory(); controller.setTool(VpTools::Transform);
    controller.pointerPress(QPointF(60,60),1,Qt::NoModifier);
    controller.pointerMove(QPointF(5,60),Qt::NoModifier);
    check(qAlpha(render(controller.document().floatingSelection(index)).pixel(10,70)) == 0,
          "live drag clips partial exit");
    controller.pointerRelease(QPointF(270,60),Qt::NoModifier);
    check(halfAlpha(render(controller.document().floatingSelection(index)).pixel(280,70)),
          "live drag restores full display on complete exit");
    controller.undo(); check(controller.document().floatingSelection(index) == image, "undo restores selection before drag");
    controller.redo(); check(controller.document().floatingSelection(index).bitmap == original, "redo preserves unclipped bitmap");
}

void FloatingSelectionProjectionTest::selectionOutlineRendering()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    const QSize viewport(2048,1099), imageSize(3120,1756);
    VpController controller;
    QImage background(imageSize,QImage::Format_ARGB32_Premultiplied);background.fill(QColor(180,180,180));
    controller.document().setImage(background);
    // Reproduce the reported steep right edge with feather 0 and a 50% view.
    for(const auto &point : {QPointF(1178,796),QPointF(1852,764),QPointF(2302,1244),QPointF(882,1444)})
        controller.pointerPress(point,.5,Qt::NoModifier);
    controller.setTool(VpTools::Marquee);controller.setMarqueeFeather(0);
    controller.pointerPress(QPointF(1320,914),.5,Qt::NoModifier);
    controller.pointerRelease(QPointF(1660,1240),Qt::NoModifier);
    controller.pointerPress(QPointF(1450,1000),.5,Qt::AltModifier);
    controller.pointerRelease(QPointF(1450,1000),Qt::AltModifier);
    check(controller.document().floatingSelections().size()==1,"Alt creates floating selection");
    const auto original=controller.document().floatingSelection(0);
    // Geometry read from the failing running scene. Qt returns an open four-element
    // intersection here, omitting the edge between the last vertex and the first.
    FloatingSelection reported;
    reported.bitmap=QImage(160,255,QImage::Format_ARGB32_Premultiplied);reported.bitmap.fill(Qt::gray);
    reported.placementOrigin=QPointF(-22.617216315739906207,363.43903309495681242);
    reported.selectionRectInBitmap=QRectF(0,0,159.33163290666306011,254.28485080549495478);
    reported.rotationDegrees=159.99927217587739392;reported.surfaceAttached=true;reported.hostQuadIndex=0;
    reported.surfaceQuads={PerspectiveQuad(
        {{QPointF(-52.217338666125755,2487.21431200334),QPointF(1609.3169107817876,713.0419892219224),
          QPointF(2093.7766346663066,661.7826897239437),QPointF(4080.582076519843,2047.8407311866351)}},
        {{QPointF(-191.9745420475988,-61.879790565381654),QPointF(935.7872910903203,-61.87979056538086),
          QPointF(935.7872910903203,1169.5824011384889),QPointF(-191.9745420475574,1169.5824011385012)}})};
    const auto reportedProjection=FloatingSelectionProjection::forSelection(reported);
    for(const auto &outline : {reportedProjection->canvasOutline(),reportedProjection->canvasPaintOutline()}) {
        const auto contours=outline.toSubpathPolygons();
        check(!contours.isEmpty(),"reported contour is retained");
        for(const auto &contour:contours)
            check(contour.size()>=4 && contour.first()==contour.last(),"reported contour explicitly closes its missing edge");
    }

    const auto verify=[&](const QImage &shot,const QPainterPath &outline,qreal zoom) {
        check(!shot.isNull(),"selection guide capture succeeds");
        if(shot.isNull()) return;
        const QPointF offset((viewport.width()-imageSize.width()*zoom)/2,
                             (viewport.height()-imageSize.height()*zoom)/2);
        const QPainterPath screen=QTransform(zoom,0,0,zoom,offset.x(),offset.y()).map(outline);
        for(const auto &polygon:screen.toSubpathPolygons()) for(int edge=1;edge<polygon.size();++edge) {
            if(QLineF(polygon[edge-1],polygon[edge]).length()<16) continue;
            int dark=0,light=0;
            for(int i=5;i<95;++i) {
                const QPointF point=polygon[edge-1]*(1-i/100.)+polygon[edge]*(i/100.);
                bool black=false,white=false;
                for(int y=qRound(point.y())-1;y<=qRound(point.y())+1;++y)
                    for(int x=qRound(point.x())-1;x<=qRound(point.x())+1;++x) {
                        if(!shot.rect().contains(x,y)) continue;
                        const QColor color=shot.pixelColor(x,y);
                        black=black || (color.red()<150 && color.green()<150 && color.blue()<150);
                        white=white || (color.red()>200 && color.green()>200 && color.blue()>200);
                    }
                dark+=black;light+=white;
            }
            check(dark>=20 && light>=10,"every edge retains visible black and white ants at all orientations");
        }
    };
    const auto scenarios=[&](const auto &capture) {
        for(int angle : {0,90,180,270}) for(qreal zoom : {.25,.5,1.0}) {
            auto selection=original;selection.rotationDegrees=angle;
            controller.document().setFloatingSelection(0,selection);
            const auto projection=FloatingSelectionProjection::forSelection(selection);
            check(!projection->canvasOutline().isEmpty(),"rotating retains selection outline");
            verify(capture(projection->canvasOutline(),zoom),projection->canvasOutline(),zoom);
        }
        controller.document().setFloatingSelection(0,reported);
        verify(capture(reportedProjection->canvasOutline(),.5),reportedProjection->canvasOutline(),.5);
    };
    if(qEnvironmentVariableIsEmpty("VP_TEST_OPENGL")) {
        QQuickWindow window;window.resize(viewport);window.show();QCoreApplication::processEvents();
        AuxiliaryOverlayItem overlay(window.contentItem());overlay.setSize(viewport);
        scenarios([&](const QPainterPath &outline,qreal zoom) {
            const QPointF offset((viewport.width()-imageSize.width()*zoom)/2,
                                 (viewport.height()-imageSize.height()*zoom)/2);
            overlay.setSelectionGuides({},outline,{},zoom,offset);
            QImage shot(viewport,QImage::Format_ARGB32_Premultiplied);shot.fill(QColor(180,180,180));
            QPainter painter(&shot);
            bool found=false;
            for(auto *child:overlay.childItems()) if(auto *raster=dynamic_cast<AuxiliaryRasterItem *>(child)) {
                found=!raster->pictures.isEmpty();raster->paint(&painter);
            }
            check(found,"software selection guide commands exist");
            painter.end();return shot;
        });
    } else {
        QOpenGLContext context;check(context.create(),"OpenGL context creates");
        QOffscreenSurface surface;surface.setFormat(context.format());surface.create();
        QVERIFY2(context.makeCurrent(&surface), "OpenGL surface becomes current");
        QQuickRenderControl control;QQuickWindow window(&control);window.setGeometry(QRect(QPoint(),viewport));
        auto *canvas=new VpCanvas(window.contentItem());canvas->setSize(viewport);canvas->setController(&controller);
        window.setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context));
        check(control.initialize(), "Qt Quick render control initializes");
        QOpenGLFramebufferObject fbo(viewport,QOpenGLFramebufferObject::CombinedDepthStencil);
        window.setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(fbo.texture(), viewport));
        scenarios([&](const QPainterPath &,qreal zoom) {
            canvas->setZoom(zoom); control.polishItems(); control.beginFrame();
            control.sync(); control.render(); control.endFrame(); return fbo.toImage();
        });
        control.invalidate();
    }
}

QTEST_MAIN(FloatingSelectionProjectionTest)
#include "tst_floatingselectionprojection.moc"
