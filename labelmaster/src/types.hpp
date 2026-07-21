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

    // LabelMaster V5 / Bevy Simulator：左右灯条至少一侧可见。
    bool leftVisible = true;
    bool rightVisible = true;
    std::array<int, 4> keypointVisibility{2, 2, 2, 2}; // V6: TL, BL, BR, TR; editor uses 0/2

    // 新增: 归一化边界矩形 (用于15字段格式)
    double norm_x = -1;  // center_x，可位于图像外
    double norm_y = -1;  // center_y，可位于图像外
    double norm_w = -1;  // width；负数表示 bbox 尚未设置
    double norm_h = -1;  // height；负数表示 bbox 尚未设置
};

Q_DECLARE_METATYPE(Armor)
Q_DECLARE_METATYPE(QVector<Armor>)
