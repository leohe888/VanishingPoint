#include "core/vpdocument.h"
#include "core/scenerenderer.h"
#include "tools/brushtool.h"
#include "tools/marqueetool.h"
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
        QPointF output(12,34);
        QVERIFY(!PerspectiveTransform::mapPoint(transform,{0,-1},&output));
        QCOMPARE(output,QPointF(12,34));
        QVERIFY(!PerspectiveTransform::mapsDomain(transform,QRectF(-1,-2,2,2)));
        QVERIFY(!PerspectiveTransform::mapPoint(transform,{0,-2},&output));
        QVERIFY(PerspectiveTransform::mapPoint(transform,{0,2},&output));
        QCOMPARE(output,QPointF(0,2.0/3));
    }
    void modelRejectsInvalidWrites() {
        VpDocument doc; QVERIFY(doc.addFloatingImage(QImage())<0);
        QImage image(10,10,QImage::Format_ARGB32); image.fill(Qt::red); doc.addFloatingImage(image);
        doc.setFloatingImageOrigin(0,{std::numeric_limits<qreal>::infinity(),0});
        QCOMPARE(doc.floatingImage(0).placementOrigin,QPointF());
        doc.attachFloatingImage(0,{},0,{0,0}); QVERIFY(!doc.floatingImage(0).surfaceAttached);
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
        SceneRenderer(doc).render(painter,1,true,{},nullptr,true,-1,0,true,1);
        QVERIFY(timer.elapsed()<2000);
    }
    void oversizedSelectionIsRejected() {
        PerspectivePlane p({QPointF(0,0),QPointF(100,0),QPointF(100,100),QPointF(0,100)},
                           {QPointF(0,0),QPointF(10000,0),QPointF(10000,10000),QPointF(0,10000)});
        MarqueeTool tool; QVERIFY(tool.beginCreate({p},{1,1})); tool.update({99,99},Qt::NoModifier,1); tool.end();
        QImage source(100,100,QImage::Format_ARGB32); source.fill(Qt::red);
        QVERIFY(tool.copy(source,{50,50}).bitmap.isNull());
        QVERIFY(tool.beginFill({50,50},source,source));
        QVERIFY(tool.clone().bitmap.isNull());
    }
    void extremeBrushSegmentDoesNotLoopOrAlterPixels() {
        QImage layer(100,100,QImage::Format_ARGB32); layer.fill(Qt::transparent); BrushTool tool;
        tool.begin(layer,{},layer.size(),{20,20}); const auto before=layer.copy();
        QElapsedTimer timer; timer.start(); QVERIFY(tool.move(layer,{1e8,1e8}).isEmpty());
        QCOMPARE(layer,before); QVERIFY(timer.elapsed()<2000);
        QVERIFY(tool.move(layer,{20,20}).isEmpty()); QCOMPARE(layer,before);
    }
};
QTEST_GUILESS_MAIN(AuditBoundariesTest)
#include "tst_auditboundaries.moc"
