#pragma once

#include "perspectivetransform.h"
#include <QColor>
#include <QImage>
#include <QtMath>
#include <array>
#include <cmath>

// Photoshop documents the mode semantics, not its proprietary healing solver.
// Fit surrounding low-frequency color/lighting in unfolded surface coordinates,
// then transfer it to the dab while retaining the sampled high-frequency detail.
// Stroke finalization uses the composite frozen before the first dab.
class HealingBlend
{
public:
    enum Mode { Off = 0, Luminance = 1, On = 2 };

    HealingBlend() = default;
    HealingBlend(int mode, const QImage &reference, const PerspectiveTransform &target,
                 const QPointF &center, qreal radius, const QColor &brushColor,
                 const PerspectiveTransform *source = nullptr, const QPointF &offset = {},
                 const QImage *sourceReference = nullptr)
        : m_mode(mode), m_center(center), m_radius(radius)
    {
        if (mode == Off || reference.isNull()) return;
        m_valid = fit(reference, target, center, radius, &m_target);
        if (source)
            m_valid = fit(sourceReference ? *sourceReference : reference, *source, center + offset, radius, &m_source) && m_valid;
        else {
            m_source[0] = {{qreal(brushColor.red()), qreal(brushColor.green()), qreal(brushColor.blue())}};
        }
    }

    QRgb blend(QRgb premultiplied, const QPointF &position) const
    {
        if (!m_valid || qAlpha(premultiplied) == 0) return premultiplied;
        const QRgb pixel = qUnpremultiply(premultiplied);
        std::array<qreal, 3> color{{qreal(qRed(pixel)), qreal(qGreen(pixel)), qreal(qBlue(pixel))}};
        const QPointF p = (position - m_center) / m_radius;
        std::array<qreal, 3> delta;
        for (int c = 0; c < 3; ++c)
            delta[c] = m_target[0][c] - m_source[0][c]
                + p.x() * (m_target[1][c] - m_source[1][c])
                + p.y() * (m_target[2][c] - m_source[2][c]);
        if (m_mode == Luminance) {
            const qreal shift = luminance(delta);
            const qreal light = qBound(0.0, luminance(color) + shift, 255.0);
            const qreal oldLight = luminance(color);
            // Scale chroma only when necessary to fit the gamut: hue is retained.
            qreal chroma = 1;
            for (int c = 0; c < 3; ++c) {
                const qreal difference = color[c] - oldLight;
                if (difference < 0) chroma = qMin(chroma, -light / difference);
                if (difference > 0) chroma = qMin(chroma, (255 - light) / difference);
            }
            for (int c = 0; c < 3; ++c) color[c] = light + chroma * (color[c] - oldLight);
        } else {
            for (int c = 0; c < 3; ++c) color[c] += delta[c];
        }
        return qPremultiply(qRgba(qBound(0, qRound(color[0]), 255),
                                 qBound(0, qRound(color[1]), 255),
                                 qBound(0, qRound(color[2]), 255), qAlpha(pixel)));
    }

private:
    using Field = std::array<std::array<qreal, 3>, 3>;
    static qreal luminance(const std::array<qreal, 3> &rgb)
    { return .3 * rgb[0] + .59 * rgb[1] + .11 * rgb[2]; }

    static bool fit(const QImage &image, const PerspectiveTransform &mapping,
                    const QPointF &center, qreal radius, Field *field)
    {
        qreal matrix[3][6]{};
        int count = 0;
        // Sample outside the core so defects under the brush do not set its tone.
        for (qreal ring : {.9, 1.3}) {
            for (int i = 0; i < 16; ++i) {
                const qreal angle = i * 2 * M_PI / 16;
                const qreal x = ring * std::cos(angle), y = ring * std::sin(angle);
                QPointF canvas;
                if (!mapping.mapForward(center + QPointF(x, y) * radius, &canvas)
                    || !std::isfinite(canvas.x()) || !std::isfinite(canvas.y())
                    || canvas.x() < 0 || canvas.y() < 0
                    || canvas.x() >= image.width() || canvas.y() >= image.height()) continue;
                const QColor pixel = image.pixelColor(int(canvas.x()), int(canvas.y()));
                if (pixel.alpha() == 0) continue;
                const qreal weight = pixel.alphaF();
                const qreal basis[3]{1, x, y};
                const qreal rgb[3]{qreal(pixel.red()), qreal(pixel.green()), qreal(pixel.blue())};
                for (int r = 0; r < 3; ++r) {
                    for (int c = 0; c < 3; ++c) {
                        matrix[r][c] += weight * basis[r] * basis[c];
                        matrix[r][c + 3] += weight * basis[r] * rgb[c];
                    }
                }
                ++count;
            }
        }
        if (count < 3) return false;
        for (int col = 0; col < 3; ++col) {
            int pivot = col;
            for (int row = col + 1; row < 3; ++row)
                if (std::abs(matrix[row][col]) > std::abs(matrix[pivot][col])) pivot = row;
            if (std::abs(matrix[pivot][col]) < 1e-6) return false;
            for (int c = 0; c < 6; ++c) std::swap(matrix[col][c], matrix[pivot][c]);
            const qreal divisor = matrix[col][col];
            for (int c = col; c < 6; ++c) matrix[col][c] /= divisor;
            for (int row = 0; row < 3; ++row) {
                if (row == col) continue;
                const qreal factor = matrix[row][col];
                for (int c = col; c < 6; ++c) matrix[row][c] -= factor * matrix[col][c];
            }
        }
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) (*field)[r][c] = matrix[r][c + 3];
        return true;
    }

    int m_mode = Off;
    bool m_valid = false;
    QPointF m_center;
    qreal m_radius = 1;
    Field m_source{}, m_target{};
};
