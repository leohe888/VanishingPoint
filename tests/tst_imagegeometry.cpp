#include <QtTest>

#include "core/imagegeometry.h"
#include "core/perspectiveplane.h"

using namespace PerspectivePlane;

namespace {

// 左面是规整矩形，面内是仿射；右面是斜梯形，必须做真正的透视投影。
Facet leftFace()
{
    Facet face;
    face.corner[0] = QPointF(10, 10);
    face.corner[1] = QPointF(110, 10);
    face.corner[2] = QPointF(110, 80);
    face.corner[3] = QPointF(10, 80);
    face.surfaceCorner[0] = QPointF(0, 0);
    face.surfaceCorner[1] = QPointF(100, 0);
    face.surfaceCorner[2] = QPointF(100, 70);
    face.surfaceCorner[3] = QPointF(0, 70);
    return face;
}

Facet rightFace()
{
    Facet face;
    face.corner[0] = QPointF(110, 10);
    face.corner[1] = QPointF(230, 30);
    face.corner[2] = QPointF(230, 96);
    face.corner[3] = QPointF(110, 80);
    face.surfaceCorner[0] = QPointF(100, 0);
    face.surfaceCorner[1] = QPointF(200, 0);
    face.surfaceCorner[2] = QPointF(200, 70);
    face.surfaceCorner[3] = QPointF(100, 70);
    return face;
}

// 200×70 的位图铺在展开曲面上，正好横跨上面两个面片。
FloatingImage attachedAcrossSeam()
{
    FloatingImage image;
    image.image = QImage(200, 70, QImage::Format_ARGB32);
    image.image.fill(Qt::red);
    image.position = QPointF(0, 0);
    image.attached = true;
    image.hostFace = 0;
    image.faces = QVector<Facet>{leftFace(), rightFace()};
    return image;
}

QPointF cornerAverage(const Facet &face)
{
    QPointF sum;
    for (const QPointF &corner : planePolygon(face.corner))
        sum += corner / 4.0;
    return sum;
}

// 一张 100×70 的位图正好铺满该面片时，位图中心与画面四角平均值的距离。
// 仿射映射下两者必然重合，只有真正的透视项才会让它偏离——用它衡量映射里有没有透视。
qreal centerDeviation(const Facet &face)
{
    FloatingImage image;
    image.image = QImage(100, 70, QImage::Format_ARGB32);
    image.image.fill(Qt::red);
    image.position = face.surfaceCorner[0]; // 位图对齐到面片的展开区域
    image.attached = true;
    image.hostFace = 0;
    image.faces = QVector<Facet>{face};

    const auto geometry = ImageGeometry::get(image);
    if (geometry->patches().size() != 1)
        return -1;
    return QLineF(geometry->patches().first().mapping.forward().map(QPointF(50, 35)),
                  cornerAverage(face)).length();
}

} // namespace

class ImageGeometryTest : public QObject
{
    Q_OBJECT

private slots:
    void flatImageDrawsSinglePatch();
    void attachedImageSpansEveryFace();
    void hitTestCoversEveryFace();
    void perspectiveFaceProjectsProjectively();
    void affineFaceMapsLinearly();
};

// 未吸附的浮动图像按画布坐标整体平移，只有一个面片。
void ImageGeometryTest::flatImageDrawsSinglePatch()
{
    FloatingImage flat;
    flat.image = QImage(40, 30, QImage::Format_ARGB32);
    flat.image.fill(Qt::red);
    flat.position = QPointF(100, 50);

    const auto geometry = ImageGeometry::get(flat);
    QCOMPARE(geometry->patches().size(), 1);
    QCOMPARE(geometry->outline().boundingRect(), QRectF(100, 50, 40, 30));
}

// 吸附后每个面片各出一个 patch，图片才能跨缝显示。
void ImageGeometryTest::attachedImageSpansEveryFace()
{
    const auto geometry = ImageGeometry::get(attachedAcrossSeam());
    QCOMPARE(geometry->patches().size(), 2);
}

// 左右两个面片都要能被命中，画布外的点要落空。
void ImageGeometryTest::hitTestCoversEveryFace()
{
    const auto geometry = ImageGeometry::get(attachedAcrossSeam());
    QVERIFY(geometry->hitTest(QPointF(60, 45)));    // 左面内
    QVERIFY(geometry->hitTest(QPointF(170, 55)));   // 右面（透视面）内
    QVERIFY(!geometry->hitTest(QPointF(400, 400))); // 画布外
}

// 右面是梯形，所以投影是透视而不是仿射：位图中心会偏离画面四角平均值。
void ImageGeometryTest::perspectiveFaceProjectsProjectively()
{
    QVERIFY(centerDeviation(rightFace()) > 0.5);
}

// 对照：左面是矩形，中心与四角平均值重合，映射退化为仿射。
void ImageGeometryTest::affineFaceMapsLinearly()
{
    QVERIFY(centerDeviation(leftFace()) < 0.5);
}

QTEST_APPLESS_MAIN(ImageGeometryTest)
#include "tst_imagegeometry.moc"
