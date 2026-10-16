#pragma once
#include "canvas/vpcanvas.h"
#include <QtTest>
#include <QCursor>
#include <QGuiApplication>
#include <QScreen>
#include <cmath>

class CursorCanvas : public VpCanvas {
public:
    using VpCanvas::keyPressEvent;
    using VpCanvas::keyReleaseEvent;
    using VpCanvas::hoverEnterEvent;
    using VpCanvas::hoverMoveEvent;
    using VpCanvas::mousePressEvent;
    using VpCanvas::mouseMoveEvent;
    using VpCanvas::mouseReleaseEvent;
};

inline void checkCursorSize(const QCursor &cursor, bool diagonal = false)
{
    const QImage image = cursor.pixmap().toImage();
    int left=image.width(), right=-1, top=image.height(), bottom=-1;
    int minSum=image.width()+image.height(), maxSum=-minSum;
    int minDifference=minSum, maxDifference=-minSum;
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x)
        if(image.pixelColor(x,y).alpha()) {
            left=qMin(left,x); right=qMax(right,x); top=qMin(top,y); bottom=qMax(bottom,y);
            minSum=qMin(minSum,x+y); maxSum=qMax(maxSum,x+y);
            minDifference=qMin(minDifference,x-y); maxDifference=qMax(maxDifference,x-y);
        }
    const int target=qRound(24 * QGuiApplication::primaryScreen()->logicalDotsPerInchX()/96.0);
    const qreal extent=diagonal
        ? qMax(maxSum-minSum,maxDifference-minDifference)/std::sqrt(2.0)+1
        : qMax(right-left+1,bottom-top+1);
    QVERIFY2(!image.isNull() && qAbs(extent-target)<=(diagonal ? 2 : 1),
             "custom cursors have consistent visible length along their axis");
    QVERIFY2(image.rect().contains(cursor.hotSpot()),"normalized cursor hotspot remains inside image");
}
