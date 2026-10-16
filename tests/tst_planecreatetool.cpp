#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QTransform>
#include <QtTest>

#include "canvas/auxiliaryoverlayitem.h"
#include "canvas/vpcanvas.h"
#include "core/scenerenderer.h"
#include "tools/planecreatetool.h"

using CreationCorners = PerspectiveQuad::Corners;
Q_DECLARE_METATYPE(CreationCorners)

namespace {
CreationCorners rectangleCorners(QPointF offset = {})
{
    return {{QPointF(40,40)+offset, QPointF(140,40)+offset,
             QPointF(140,110)+offset, QPointF(40,110)+offset}};
}

QImage creationBackground()
{
    QImage image(640,480,QImage::Format_ARGB32);
    image.fill(Qt::white);
    return image;
}

PerspectivePlane existingPlane()
{
    const auto corners = rectangleCorners(QPointF(350,200));
    PerspectivePlane plane(corners,corners);
    plane.setSurfaceGroupId(9);
    return plane;
}

class CreationCanvas : public VpCanvas
{
public:
    using VpCanvas::keyPressEvent;
};
}

class PlaneCreateToolTest : public QObject
{
    Q_OBJECT

private slots:
    void creationPreviewsAndDiagnosticValidation();
    void creationOrderPreservesSelectionOrientation();
    void startsEmpty();
    void completesAfterFourPoints();
    void ignoresFifthPoint();
    void removesLastPoint();
    void createsExpectedPlane();
    void resetClearsState();
    void creationCommitsOneHistoryEntry();
    void failedCreationPreservesDocumentAndCanRetry_data();
    void failedCreationPreservesDocumentAndCanRetry();
    void interruptedCreationStartsFresh_data();
    void interruptedCreationStartsFresh();
    void keyboardBacktrackPreservesExistingPlane_data();
    void keyboardBacktrackPreservesExistingPlane();
    void successivePlanesHaveIndependentGroupsAndHistory();
    void creationValidityBoundaries_data();
    void creationValidityBoundaries();
    void normalizationHandlesSpecialShapes_data();
    void normalizationHandlesSpecialShapes();
    void partialCreationStates_data();
    void partialCreationStates();
    void planeSnapshotsDoNotConsumeCreationState();
};

static void addRectangle(PlaneCreateTool &tool)
{
    tool.addPoint(QPointF(10, 10));
    tool.addPoint(QPointF(110, 10));
    tool.addPoint(QPointF(110, 80));
    tool.addPoint(QPointF(10, 80));
}

void PlaneCreateToolTest::startsEmpty()
{
    PlaneCreateTool tool;
    QVERIFY(!tool.creating());
    QVERIFY(!tool.finished());
    QCOMPARE(tool.points().size(), 0);
}

void PlaneCreateToolTest::completesAfterFourPoints()
{
    PlaneCreateTool tool;
    addRectangle(tool);
    QVERIFY(tool.creating());
    QVERIFY(tool.finished());
    QCOMPARE(tool.points().size(), 4);
}

void PlaneCreateToolTest::ignoresFifthPoint()
{
    PlaneCreateTool tool;
    addRectangle(tool);
    tool.addPoint(QPointF(200, 200));
    QCOMPARE(tool.points().size(), 4);
    QCOMPARE(tool.points().last(), QPointF(10, 80));
}

void PlaneCreateToolTest::removesLastPoint()
{
    PlaneCreateTool tool;
    addRectangle(tool);

    tool.removeLastPoint();
    QCOMPARE(tool.points().size(), 3);
    QVERIFY(tool.creating());
    QVERIFY(!tool.finished());

    // 回退到空，继续回退应安全无副作用
    for (int i = 0; i < 4; ++i)
        tool.removeLastPoint();
    QCOMPARE(tool.points().size(), 0);
    QVERIFY(!tool.creating());

    // 回退后仍可继续落点
    tool.addPoint(QPointF(20, 20));
    QCOMPARE(tool.points().size(), 1);
    QCOMPARE(tool.points().first(), QPointF(20, 20));
}

void PlaneCreateToolTest::createsExpectedPlane()
{
    PlaneCreateTool tool;
    addRectangle(tool);
    const PerspectivePlane plane = tool.makePlane(3);
    QCOMPARE(plane.quad().canvasCorners()[0], QPointF(10, 10));
    QCOMPARE(plane.quad().canvasCorners()[2], QPointF(110, 80));
    QCOMPARE(plane.quad().surfaceCorners()[2], QPointF(100, 70));
    QCOMPARE(plane.surfaceGroupId(), 3);
    QVERIFY(plane.quad().isValid());
}

void PlaneCreateToolTest::resetClearsState()
{
    PlaneCreateTool tool;
    tool.addPoint(QPointF(10, 10));
    tool.reset();
    QVERIFY(!tool.creating());
    QCOMPARE(tool.points().size(), 0);
}

void PlaneCreateToolTest::creationCommitsOneHistoryEntry()
{
    VpController controller;
    auto &doc = controller.document();
    const auto background = creationBackground();
    QVERIFY(doc.setImage(background));
    const auto corners = rectangleCorners();
    for (int i = 0; i < 3; ++i) {
        controller.pointerPress(corners[i],1,Qt::NoModifier);
        QCOMPARE(controller.creationPoints().size(),i+1);
        QVERIFY(doc.planes().isEmpty());
        QVERIFY(!controller.canUndo());
        QVERIFY(!controller.canRedo());
        QVERIFY(!doc.editActive());
        QCOMPARE(controller.tool(),VpTools::CreatePlane);
    }
    controller.pointerPress(corners[3],1,Qt::NoModifier);
    QCOMPARE(doc.planes().size(),1);
    const auto created = doc.planes();
    QVERIFY(created[0].quad().isProjectable());
    QCOMPARE(doc.selectedPlane(),0);
    QVERIFY(controller.creationPoints().isEmpty());
    QCOMPARE(controller.tool(),VpTools::EditPlane);
    QVERIFY(!controller.planeAngleEditable());
    QVERIFY(!doc.editActive());
    QVERIFY(controller.canUndo());
    QVERIFY(!controller.canRedo());
    QCOMPARE(doc.background(),background);
    QVERIFY(!doc.hasPaintContent());

    controller.undo();
    QVERIFY(doc.planes().isEmpty());
    QCOMPARE(doc.selectedPlane(),-1);
    QVERIFY(!controller.canUndo());
    QVERIFY(controller.canRedo());
    controller.redo();
    QVERIFY(doc.planes() == created);
    QCOMPARE(doc.selectedPlane(),0);
    QVERIFY(controller.canUndo());
    QVERIFY(!controller.canRedo());
}

void PlaneCreateToolTest::failedCreationPreservesDocumentAndCanRetry_data()
{
    QTest::addColumn<CreationCorners>("corners");
    QTest::newRow("repeated-point") << CreationCorners{{{40,40},{140,40},{140,40},{40,110}}};
    QTest::newRow("crossed") << CreationCorners{{{40,40},{140,110},{140,40},{40,110}}};
    QTest::newRow("concave") << CreationCorners{{{40,40},{140,40},{70,60},{40,110}}};
    QTest::newRow("collinear") << CreationCorners{{{40,40},{80,40},{120,40},{160,40}}};
}

void PlaneCreateToolTest::failedCreationPreservesDocumentAndCanRetry()
{
    QFETCH(CreationCorners,corners);
    VpController controller;
    auto &doc = controller.document();
    QVERIFY(doc.setImage(creationBackground()));
    doc.beginEdit();
    QCOMPARE(doc.appendPlane(existingPlane()),0);
    doc.setSelectedPlane(0);
    doc.commitEdit(true);
    const auto before = doc.planes();
    doc.beginEdit();
    QCOMPARE(doc.appendPlane(existingPlane()),1);
    doc.setSelectedPlane(1);
    doc.commitEdit(true);
    const auto redoBranch = doc.planes();
    controller.undo();
    QVERIFY(controller.canUndo());
    QVERIFY(controller.canRedo());
    for (const auto &point : corners)
        controller.pointerPress(point,1,Qt::NoModifier);
    QVERIFY(doc.planes() == before);
    QCOMPARE(doc.selectedPlane(),0);
    QCOMPARE(controller.tool(),VpTools::CreatePlane);
    QVERIFY(controller.creationPoints().isEmpty());
    QVERIFY(!doc.editActive());
    QVERIFY(controller.canUndo());
    QVERIFY(controller.canRedo());
    controller.redo();
    QVERIFY(doc.planes() == redoBranch);
    controller.undo();

    for (const auto &point : rectangleCorners())
        controller.pointerPress(point,1,Qt::NoModifier);
    QCOMPARE(doc.planes().size(),2);
    QVERIFY(doc.planes()[0] == before[0]);
    QCOMPARE(doc.selectedPlane(),1);
    QCOMPARE(controller.tool(),VpTools::EditPlane);
    QVERIFY(controller.creationPoints().isEmpty());
    QVERIFY(!controller.canRedo());
    controller.undo();
    QVERIFY(doc.planes() == before);
    QCOMPARE(doc.selectedPlane(),0);
}

void PlaneCreateToolTest::interruptedCreationStartsFresh_data()
{
    QTest::addColumn<int>("pointCount");
    QTest::addColumn<int>("nextTool");
    for (int count = 1; count <= 3; ++count)
        for (int tool : {-1,int(VpTools::Hand),int(VpTools::Zoom),int(VpTools::EditPlane)}) {
            const auto row = QString("%1-points-tool-%2").arg(count).arg(tool).toLatin1();
            QTest::newRow(row.constData()) << count << tool;
        }
}

void PlaneCreateToolTest::interruptedCreationStartsFresh()
{
    QFETCH(int,pointCount);
    QFETCH(int,nextTool);
    VpController controller;
    auto &doc = controller.document();
    QVERIFY(doc.setImage(creationBackground()));
    QCOMPARE(doc.appendPlane(existingPlane()),0);
    doc.setSelectedPlane(0);
    doc.resetHistory();
    const auto before = doc.planes();
    const auto corners = rectangleCorners();
    for (int i = 0; i < pointCount; ++i)
        controller.pointerPress(corners[i],1,Qt::NoModifier);
    QCOMPARE(controller.creationPoints().size(),pointCount);
    if (nextTool < 0)
        controller.cancelInteraction();
    else {
        controller.setTool(static_cast<VpTools::Tool>(nextTool));
        QCOMPARE(int(controller.tool()),nextTool);
    }
    QVERIFY(controller.creationPoints().isEmpty());
    QVERIFY(doc.planes() == before);
    QCOMPARE(doc.selectedPlane(),0);
    QVERIFY(!controller.canUndo());
    QVERIFY(!controller.canRedo());
    QVERIFY(!doc.editActive());
    controller.setTool(VpTools::CreatePlane);
    controller.pointerPress(QPointF(20,20),1,Qt::NoModifier);
    QCOMPARE(controller.creationPoints(),QVector<QPointF>{QPointF(20,20)});
    QVERIFY(doc.planes() == before);
    QVERIFY(!controller.canUndo());
}

void PlaneCreateToolTest::keyboardBacktrackPreservesExistingPlane_data()
{
    QTest::addColumn<int>("key");
    QTest::addColumn<int>("pointCount");
    for (int key : {int(Qt::Key_Backspace),int(Qt::Key_Delete)})
        for (int count = 1; count <= 3; ++count) {
            const auto row = QString("%1-%2-points").arg(key == Qt::Key_Delete ? "delete" : "backspace").arg(count).toLatin1();
            QTest::newRow(row.constData()) << key << count;
        }
}

void PlaneCreateToolTest::keyboardBacktrackPreservesExistingPlane()
{
    QFETCH(int,key);
    QFETCH(int,pointCount);
    VpController controller;
    auto &doc = controller.document();
    QVERIFY(doc.setImage(creationBackground()));
    QCOMPARE(doc.appendPlane(existingPlane()),0);
    doc.setSelectedPlane(0);
    doc.resetHistory();
    const auto before = doc.planes();
    CreationCanvas canvas;
    canvas.setController(&controller);
    const auto corners = rectangleCorners();
    for (int i = 0; i < pointCount; ++i)
        controller.pointerPress(corners[i],1,Qt::NoModifier);
    QKeyEvent event(QEvent::KeyPress,key,Qt::NoModifier);
    event.setAccepted(false);
    canvas.keyPressEvent(&event);
    QVERIFY(event.isAccepted());
    QCOMPARE(controller.creationPoints().size(),pointCount-1);
    for (int i = 0; i < pointCount-1; ++i)
        QCOMPARE(controller.creationPoints()[i],corners[i]);
    QVERIFY(doc.planes() == before);
    QCOMPARE(doc.selectedPlane(),0);
    QVERIFY(!doc.hasPaintContent());
    QVERIFY(!controller.canUndo());
    QVERIFY(!controller.canRedo());
    const QPointF replacements[] = {{30,30},{160,35},{160,120}};
    const auto replacement = replacements[pointCount-1];
    controller.pointerPress(replacement,1,Qt::NoModifier);
    for (int i = pointCount; i < 4; ++i)
        controller.pointerPress(corners[i],1,Qt::NoModifier);
    QCOMPARE(doc.planes().size(),2);
    QVERIFY(doc.planes()[0] == before[0]);
    QVERIFY(doc.planes()[1].quad().canvasPolygon().contains(replacement));
    QVERIFY(!doc.planes()[1].quad().canvasPolygon().contains(corners[pointCount-1]));
    QVERIFY(controller.creationPoints().isEmpty());
    controller.undo();
    QVERIFY(doc.planes() == before);
    QVERIFY(!controller.canUndo());
}

void PlaneCreateToolTest::successivePlanesHaveIndependentGroupsAndHistory()
{
    VpController controller;
    auto &doc = controller.document();
    QVERIFY(doc.setImage(creationBackground()));
    QVector<QVector<PerspectivePlane>> states{{}};
    for (int i = 0; i < 3; ++i) {
        controller.setTool(VpTools::CreatePlane);
        for (const auto &point : rectangleCorners(QPointF(i*180,0)))
            controller.pointerPress(point,1,Qt::NoModifier);
        QCOMPARE(doc.planes().size(),i+1);
        QCOMPARE(doc.selectedPlane(),i);
        QCOMPARE(controller.tool(),VpTools::EditPlane);
        QVERIFY(controller.creationPoints().isEmpty());
        QVERIFY(!doc.isPlaneLinked(i));
        QCOMPARE(doc.planes()[i].parentPlaneIndex(),-1);
        for (int j = 0; j < i; ++j) {
            QVERIFY(doc.planes()[j] == states.last()[j]);
            QVERIFY(doc.planes()[j].surfaceGroupId() != doc.planes()[i].surfaceGroupId());
        }
        states.append(doc.planes());
    }
    for (int i = 2; i >= 0; --i) {
        controller.undo();
        QVERIFY(doc.planes() == states[i]);
        QCOMPARE(doc.selectedPlane(),i-1);
    }
    QVERIFY(!controller.canUndo());
    for (int i = 1; i <= 3; ++i) {
        controller.redo();
        QVERIFY(doc.planes() == states[i]);
        QCOMPARE(doc.selectedPlane(),i-1);
    }
    QVERIFY(!controller.canRedo());
}

void PlaneCreateToolTest::creationValidityBoundaries_data()
{
    QTest::addColumn<CreationCorners>("corners");
    QTest::addColumn<bool>("accepted");
    for (qreal width : {7.99,8.0,8.01}) {
        const auto row = QString("edge-%1").arg(width).toLatin1();
        QTest::newRow(row.constData()) << CreationCorners{{{0,0},{width,0},{width,20},{0,20}}} << (width >= 8);
    }
    for (qreal height : {4.99,5.0,5.01}) {
        const auto row = QString("area-%1").arg(height*10).toLatin1();
        QTest::newRow(row.constData()) << CreationCorners{{{0,0},{10,0},{20,height},{10,height}}} << (height >= 5);
    }
}

void PlaneCreateToolTest::creationValidityBoundaries()
{
    QFETCH(CreationCorners,corners);
    QFETCH(bool,accepted);
    VpController controller;
    QVERIFY(controller.document().setImage(creationBackground()));
    for (const auto &point : corners)
        controller.pointerPress(point,1,Qt::NoModifier);
    QCOMPARE(controller.document().planes().size(),accepted ? 1 : 0);
    QCOMPARE(controller.tool(),accepted ? VpTools::EditPlane : VpTools::CreatePlane);
    QCOMPARE(controller.canUndo(),accepted);
    QVERIFY(controller.creationPoints().isEmpty());
}

void PlaneCreateToolTest::normalizationHandlesSpecialShapes_data()
{
    QTest::addColumn<CreationCorners>("corners");
    QTest::newRow("diamond-tied-directions") << CreationCorners{{{100,20},{180,100},{100,180},{20,100}}};
    auto rotated = rectangleCorners();
    const QTransform rotation = QTransform().rotate(45);
    for (auto &point : rotated) point = rotation.map(point);
    QTest::newRow("rotated-rectangle") << rotated;
    QTest::newRow("strong-perspective") << CreationCorners{{{30,20},{300,30},{170,180},{150,180}}};
    QTest::newRow("fractional-coordinates") << CreationCorners{{{40.125,40.375},{140.625,42.875},{135.25,111.5},{38.75,108.125}}};
}

void PlaneCreateToolTest::normalizationHandlesSpecialShapes()
{
    QFETCH(CreationCorners,corners);
    PlaneCreateTool referenceTool;
    for (const auto &point : corners) referenceTool.addPoint(point);
    const auto reference = referenceTool.makePlane(5);
    QVERIFY(reference.quad().isProjectable());
    for (int step : {1,-1}) for (int first = 0; first < 4; ++first) {
        PlaneCreateTool tool;
        for (int i = 0; i < 4; ++i)
            tool.addPoint(corners[(first+step*i+8)%4]);
        const auto plane = tool.makePlane(5);
        QVERIFY(plane == reference);
        const auto mapping = plane.quad().surfaceToCanvasTransform();
        for (int i = 0; i < 4; ++i) {
            QVERIFY(plane.quad().canvasPolygon().contains(corners[i]));
            QPointF projected;
            QVERIFY(mapping.mapForward(plane.quad().surfaceCorners()[i],&projected));
            QVERIFY(QLineF(projected,plane.quad().canvasCorners()[i]).length() < 1e-7);
        }
    }
}

void PlaneCreateToolTest::partialCreationStates_data()
{
    QTest::addColumn<int>("pointCount");
    for (int count = 0; count <= 4; ++count)
        QTest::newRow(qPrintable(QString::number(count))) << count;
}

void PlaneCreateToolTest::partialCreationStates()
{
    QFETCH(int,pointCount);
    PlaneCreateTool tool;
    QVector<QPointF> expected;
    const auto corners = rectangleCorners();
    for (int i = 0; i < pointCount; ++i) {
        tool.addPoint(corners[i]);
        expected.append(corners[i]);
    }
    QCOMPARE(tool.points(),expected);
    QCOMPARE(tool.creating(),pointCount > 0);
    QCOMPARE(tool.finished(),pointCount == 4);
}

void PlaneCreateToolTest::planeSnapshotsDoNotConsumeCreationState()
{
    PlaneCreateTool tool;
    for (const auto &point : rectangleCorners()) tool.addPoint(point);
    const auto points = tool.points();
    const auto first = tool.makePlane(7);
    QVERIFY(tool.makePlane(7) == first);
    const auto second = tool.makePlane(11);
    QVERIFY(second.quad() == first.quad());
    QCOMPARE(first.surfaceGroupId(),7);
    QCOMPARE(second.surfaceGroupId(),11);
    QCOMPARE(tool.points(),points);
    QVERIFY(tool.finished());
    tool.reset();
    QVERIFY(!tool.creating());
    QVERIFY(!tool.finished());
    for (const auto &point : rectangleCorners(QPointF(200,100))) tool.addPoint(point);
    const auto next = tool.makePlane(12);
    QVERIFY(!(next.quad() == first.quad()));
    for (int i = 0; i < 4; ++i)
        QCOMPARE(next.quad().canvasCorners()[i],first.quad().canvasCorners()[i]+QPointF(200,100));
    QCOMPARE(first.quad().canvasCorners(),rectangleCorners());
    QCOMPARE(second.quad().canvasCorners(),rectangleCorners());
}

void PlaneCreateToolTest::creationOrderPreservesSelectionOrientation()
{
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    const QSize canvasSize(800, 600);
    const PerspectiveQuad::Corners corners{{{220, 160}, {520, 180}, {550, 440}, {190, 420}}};
    const auto create = [&](int first, int step) {
        PlaneCreateTool tool;
        for (int i = 0; i < 4; ++i) tool.addPoint(corners[(first + step * i + 8) % 4]);
        return tool.makePlane(0);
    };
    const auto reference = create(0, 1);
    QImage background(canvasSize, QImage::Format_ARGB32_Premultiplied);background.fill(Qt::white);
    QImage pixels(40, 24, QImage::Format_ARGB32_Premultiplied);pixels.fill(Qt::red);
    pixels.setPixelColor(0, 0, Qt::blue);
    for (int step : {1, -1}) for (int first = 0; first < 4; ++first) {
        const auto plane = create(first, step);
        check(plane.quad() == reference.quad(), "creation start corner and winding do not rotate or mirror plane axes");
        VpController controller;controller.document().setImage(background);
        controller.document().appendPlane(plane);
        controller.setCopiedCanvasSelection(pixels);controller.pasteSelection();
        controller.pointerPress(QPointF(20,12), 1, Qt::NoModifier);
        controller.pointerMove(QPointF(370,300), Qt::NoModifier);
        controller.pointerRelease(QPointF(370,300), Qt::NoModifier);
        const auto &image = controller.document().floatingSelection(0);
        check(image.surfaceAttached && image.bitmap == pixels, "drag attaches without modifying bitmap");
        const QTransform toSurface = image.bitmapToPlacementTransform();
        const QPointF origin = image.mapPlacementToCanvas(toSurface.map(QPointF(20,12)));
        const QPointF right = image.mapPlacementToCanvas(toSurface.map(QPointF(30,12))) - origin;
        const QPointF down = image.mapPlacementToCanvas(toSurface.map(QPointF(20,20))) - origin;
        check(right.x() > 0 && down.y() > 0 && right.x()*down.y()-right.y()*down.x() > 0,
              "pasted image keeps rightward and downward orientation when entering plane");
        check(QLineF(origin,QPointF(370,300)).length() < .001, "grabbed bitmap point stays under pointer");
        for (int edge = 0; edge < 4; ++edge) {
            const QPointF press = (corners[edge]+corners[(edge+1)%4])/2;
            const QPointF outward = press-QPointF(370,300);
            const auto expected = extrudePerpendicularPlane(reference,edge,press+outward*.3,press,canvasSize);
            const auto child = extrudePerpendicularPlane(plane,edge,press+outward*.3,press,canvasSize);
            check(child.quad() == expected.quad(), "child plane inherits consistent group axes for every shared edge");
        }
    }
    PlaneCreateTool crossed;
    for (int i : {0,2,1,3}) crossed.addPoint(corners[i]);
    check(!crossed.makePlane(0).quad().isValid(), "normalization does not accept self-crossing creation");
}

void PlaneCreateToolTest::creationPreviewsAndDiagnosticValidation()
{
    QQuickWindow window;
    window.resize(300, 300);
    window.show();
    QCoreApplication::processEvents();
    const auto check = [](bool passed, const char *message) {
        QVERIFY2(passed, message);
    };
    const QSize size(300, 300);
    const PerspectiveQuad::Corners surface{{QPointF(0, 0), QPointF(200, 0), QPointF(200, 150), QPointF(0, 150)}};
    const PerspectiveQuad rectangle({QPointF(50, 50), QPointF(250, 50), QPointF(250, 250), QPointF(50, 250)}, surface);
    const PerspectiveQuad yellow({QPointF(50, 50), QPointF(200, 50), QPointF(230, 200), QPointF(80, 200)}, surface);
    const PerspectiveQuad degenerate({QPointF(50, 50), QPointF(250, 50), QPointF(100, 100), QPointF(50, 250)}, surface);
    const auto fromHomography = [&](const QTransform &h) {
        PerspectiveQuad::Corners corners;
        const PerspectiveQuad::Corners uv{{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}};
        for (int i = 0; i < 4; ++i) corners[i] = h.map(uv[i]) * 300 + QPointF(150, 150);
        return PerspectiveQuad(corners, surface);
    };
    const auto blue = fromHomography(QTransform(.4, .05, .1, -.12, .4, .2, -.3, -.3, 1));
    const auto red = fromHomography(QTransform(.4, .05, .1, -.05, .5, .2, -.3, -.3, 1));
    QImage background(size, QImage::Format_ARGB32_Premultiplied); background.fill(Qt::white);
    const auto countBlue = [&](const QImage &image) {
        int matches = 0;
        const QColor expected("#017AFF");
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor actual = image.pixelColor(x, y);
                if (actual.alpha() > 120 && qAbs(actual.red() - expected.red()) < 3
                    && qAbs(actual.green() - expected.green()) < 3 && qAbs(actual.blue() - expected.blue()) < 3) ++matches;
            }
        return matches;
    };
    // Every prospective fourth corner remains blue, even when creation will be rejected.
    for (const auto &quad : {rectangle, yellow, red, degenerate}) {
        const auto corners = quad.canvasCorners();
        const QVector<QPointF> points{corners[0], corners[1], corners[2]};
        VpDocument doc; doc.setImage(background);
        QImage preview(size, QImage::Format_ARGB32_Premultiplied); preview.fill(Qt::transparent);
        QPainter painter(&preview);
        SceneRenderer renderer(doc); SceneRenderer::Guides options;
        options.creationPoints = points; options.cursorPoint = corners[3];
        renderer.renderGuides(painter, 1, options); painter.end();
        check(countBlue(preview) > 30, "raster creation preview stays blue");
        AuxiliaryOverlayItem overlay(window.contentItem()); overlay.setSize(size);
        overlay.setCreationPreview(points, corners[3], true, 1, {}, size);
        preview.fill(Qt::transparent);
        QPainter overlayPainter(&preview);
        for (QQuickItem *child : overlay.childItems())
            if (auto *raster = dynamic_cast<AuxiliaryRasterItem *>(child)) raster->paint(&overlayPainter);
        overlayPainter.end();
        check(countBlue(preview) > 30, "cached creation preview stays blue");
    }
    const PerspectiveQuad crossed({QPointF(50, 50), QPointF(250, 250), QPointF(250, 50), QPointF(50, 250)}, surface);
    const PerspectiveQuad collinear({QPointF(50, 50), QPointF(100, 50), QPointF(150, 50), QPointF(200, 50)}, surface);
    const PerspectiveQuad shortEdge({QPointF(50, 50), QPointF(55, 50), QPointF(250, 250), QPointF(50, 250)}, surface);
    for (const auto &quad : {degenerate, crossed, collinear, shortEdge}) {
        VpController controller; controller.document().setImage(background);
        for (const QPointF &point : quad.canvasCorners()) controller.pointerPress(point, 1, Qt::NoModifier);
        check(controller.document().planes().isEmpty() && controller.tool() == VpTools::CreatePlane,
              "invalid creation retains original restrictions");
        check(controller.document().appendPlane(PerspectivePlane(quad.canvasCorners(), surface)) < 0,
              "document rejects invalid geometry");
    }
    for (const auto &quad : {rectangle, yellow, red}) {
        VpController controller; controller.document().setImage(background);
        for (const QPointF &point : quad.canvasCorners()) controller.pointerPress(point, 1, Qt::NoModifier);
        check(controller.document().planes().size() == 1 && controller.tool() == VpTools::EditPlane,
              "valid geometry creates regardless of diagnostic color");
        if (!controller.document().planes().isEmpty())
            check(controller.document().planes()[0].quad().status(size) == quad.status(size),
                  "created plane displays diagnostic status");
    }
}

QTEST_MAIN(PlaneCreateToolTest)
#include "tst_planecreatetool.moc"
