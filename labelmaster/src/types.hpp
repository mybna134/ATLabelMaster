#pragma once
#include <QMetaType>
#include <QPointF>
#include <QString>
#include <QVector>
#include <array>

struct Armor {
    QString cls;
    QString color;
    float score = 0.f;
    // 角点顺序：从 0 开始逆时针：TL(0) → BL(1) → BR(2) → TR(3)，全为"原图坐标"
    QPointF p0, p1, p2, p3;
    QPointF norm_p0, norm_p1, norm_p2, norm_p3;
    int size = false; // small 0 big 1

    // V5/V6: TL, BL, BR, TR；0=不可见，1=不在范围内，2=可见。
    std::array<int, 4> keypointVisibility{2, 2, 2, 2};

    // 归一化边界矩形；可独立于四个关键点进行编辑。
    double norm_x = -1; // center_x，可位于图像外
    double norm_y = -1; // center_y，可位于图像外
    double norm_w = -1; // width；负数表示 bbox 尚未设置
    double norm_h = -1; // height；负数表示 bbox 尚未设置

    bool operator==(const Armor&) const = default;
};

Q_DECLARE_METATYPE(Armor)
Q_DECLARE_METATYPE(QVector<Armor>)
