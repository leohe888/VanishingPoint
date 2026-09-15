#pragma once

#include <QQuickPaintedItem>

class VpCanvas : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit VpCanvas(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;
};
