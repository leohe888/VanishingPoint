#include "canvas/vpdocument.h"
#include "core/scenerenderer.h"
#include "testhelpers.h"
#include <QElapsedTimer>
#include <QPainter>
#include <QtTest>

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
};
QTEST_GUILESS_MAIN(AuditBoundariesTest)
#include "tst_auditboundaries.moc"
