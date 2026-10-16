#include "canvas/vpdocument.h"
#include "core/scenerenderer.h"
#include "tools/brushtool.h"
#include "testhelpers.h"
#include <QElapsedTimer>
#include <QPainter>
#include <QtTest>
#include <limits>

class AuditBoundariesTest : public QObject {
    Q_OBJECT
private slots:
    void safeMappingRejectsPoleAndPreservesOutput() {
        QTransform transform(1,0,0, 0,1,1, 0,0,1);
        const QPolygonF source(QVector<QPointF>{{0,0},{1,0},{1,1},{0,1}});
        const PerspectiveTransform mapping(source, transform.map(source));
        QVERIFY(mapping.isValid());
        QPointF output(12,34);
        QVERIFY(!mapping.mapForward({0,-1},&output));
        QCOMPARE(output,QPointF(12,34));
        QVERIFY(!mapping.canProject(QRectF(-1,-2,2,2)));
        QVERIFY(!mapping.mapForward({0,-2},&output));
        QVERIFY(mapping.mapForward({0,2},&output));
        QCOMPARE(output,QPointF(0,2.0/3));
    }
    void modelRejectsInvalidWrites() {
        VpDocument doc; QVERIFY(doc.addFloatingSelection(makeTestSelection(QImage()))<0);
        QImage image(10,10,QImage::Format_ARGB32); image.fill(Qt::red); doc.addFloatingSelection(makeTestSelection(image));
        doc.setFloatingSelectionOrigin(0,{std::numeric_limits<qreal>::infinity(),0});
        QCOMPARE(doc.floatingSelection(0).placementOrigin,QPointF());
        doc.attachFloatingSelection(0,{},0,{0,0}); QVERIFY(!doc.floatingSelection(0).surfaceAttached);
        auto p=makeTestPlane(); p.quad().setSurfaceCorners({});
        QVERIFY(p.quad().isValid()); QVERIFY(!p.quad().isProjectable()); QCOMPARE(doc.appendPlane(p),-1);
        doc.setSelectedPlane(90); QCOMPARE(doc.selectedPlane(),-1);
    }
    void hugeAnglesAndGridFinishPromptly() {
        QElapsedTimer timer; timer.start(); auto p=makeTestPlane();
        rotatePlaneAroundEdge(p,0,1e300,{1000,800});
        QVERIFY(timer.elapsed()<2000);
        p=PerspectivePlane({QPointF(0,0),QPointF(1e7,0),QPointF(1e7,1e7),QPointF(0,1e7)},
                           {QPointF(0,0),QPointF(100,0),QPointF(100,100),QPointF(0,100)});
        VpDocument doc; doc.appendPlane(p); doc.setSelectedPlane(0);
        QImage image(64,64,QImage::Format_ARGB32); image.fill(Qt::transparent); QPainter painter(&image);
        SceneRenderer::Guides guides; guides.gridSize=1; guides.editHandlesVisible=true;
        SceneRenderer(doc).renderGuides(painter,1,guides);
        QVERIFY(timer.elapsed()<2000);
    }
    void extremeBrushSegmentDoesNotLoopOrAlterPixels() {
        QImage layer(100,100,QImage::Format_ARGB32); layer.fill(Qt::transparent); BrushTool tool;
        tool.begin(layer,{},layer.size(),{20,20}); const auto before=layer.copy();
        QElapsedTimer timer; timer.start(); QVERIFY(tool.move(layer,{1e8,1e8}).isEmpty());
        QCOMPARE(layer,before); QVERIFY(timer.elapsed()<2000);
        QVERIFY(tool.move(layer,{20,20}).isEmpty()); QCOMPARE(layer,before);
    }
    void historyRetainsAtMostFortyStates() {
        VpDocument doc;
        // History retains at most 40 states, including the current state.
        for(int i=0;i<24;++i) {
            QImage image(200,100,QImage::Format_ARGB32); image.fill(QColor(i,0,0));
            doc.addFloatingSelection(makeTestSelection(image)); doc.removeFloatingSelection(0);
        }
        int count=0; while(doc.undo()) ++count;
        QCOMPARE(count,39);
    }
};
QTEST_GUILESS_MAIN(AuditBoundariesTest)
#include "tst_auditboundaries.moc"
