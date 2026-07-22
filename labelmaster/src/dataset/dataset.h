#pragma once

enum class DataSet : unsigned char {
    Auto        = 255, // Scan all non-empty labels and detect the dataset format
    LabelMaster = 0,
    LabelMaster2,      // 默认格式 (11字段: color size cls pts)
    HITSZ,             // 南工骁鹰
    //     贴纸 	ID
    // G（哨兵） 	0
    // 1（一号） 	1
    // 2（二号） 	2
    // 3（三号） 	3
    // 4（四号） 	4
    // 5（五号） 	5
    // O（前哨站） 	6
    // Bs（基地） 	7
    // Bb（基地大装甲） 	8
    // L3（三号平衡） 	9
    // L4（四号平衡） 	10
    // L5（五号平衡） 	11
    UPC,  // RPS
    NWPU, // 西北工业大学
    LabelMaster3,  // 新增: 15字段 (color size cls xywh pts)
    LabelMasterV4, // 13字段（36类组合类别 + bbox + 四角点）
    LabelMasterV5, // 旧 YOLO Pose 17字段（14类组合类别 + bbox + 四角点逐点可见性）
    LabelMasterV6, // 19字段（color size class + bbox + 四角点逐点可见性）
    UnionSecret,   // 9字段（39类组合类别 + 四角点），每种颜色占13个类别
};

enum class LabelOutputFormat : int {
    Points11      = 0,
    RectPoints15  = 1,
    LabelMasterV4 = 2,
    LabelMasterV6 = 3,
};

constexpr bool supportsVisibility(DataSet format) {
    return format == DataSet::LabelMasterV5 || format == DataSet::LabelMasterV6;
}

constexpr bool supportsVisibility(LabelOutputFormat format) {
    return format == LabelOutputFormat::LabelMasterV6;
}

constexpr bool supportsBoundingBox(LabelOutputFormat format) {
    return format == LabelOutputFormat::RectPoints15 || format == LabelOutputFormat::LabelMasterV4
        || format == LabelOutputFormat::LabelMasterV6;
}

constexpr bool isDirectOpenFormat(DataSet format) { return format == DataSet::LabelMasterV6; }

constexpr LabelOutputFormat canonicalOutputFormat(DataSet) {
    return LabelOutputFormat::LabelMasterV6;
}
