#pragma once

#include <QQuickPaintedItem>

class PerspectiveCanvas : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit PerspectiveCanvas(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;
};
