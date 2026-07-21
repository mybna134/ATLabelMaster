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
    // color * 16 + size * 8 + class
    LabelMaster3,  // 新增: 15字段 (color size cls xywh pts)
    LabelMasterV4, // 新增: 13字段 (cls xywh pts) cls=color*size*num
    LabelMasterV5, // Bevy Simulator 15字段（含左右灯条可见性）
    LabelMasterV6, // YOLO Pose 17字段（bbox + 四角点逐点可见性）
};

enum class LabelOutputFormat : int {
    Points11      = 0,
    RectPoints15  = 1,
    LabelMasterV4 = 2,
    LabelMasterV6 = 3,
};

enum class PoseClassScheme : int {
    Classes14 = 14,
    Classes36 = 36,
};

constexpr bool supportsVisibility(DataSet format) {
    return format == DataSet::LabelMasterV5 || format == DataSet::LabelMasterV6;
}

constexpr bool supportsVisibility(LabelOutputFormat format) {
    return format == LabelOutputFormat::LabelMasterV6;
}

constexpr bool isDirectOpenFormat(DataSet format) {
    return format == DataSet::LabelMasterV4 || format == DataSet::LabelMasterV6;
}

constexpr LabelOutputFormat canonicalOutputFormat(DataSet input) {
    return supportsVisibility(input) ? LabelOutputFormat::LabelMasterV6
                                     : LabelOutputFormat::LabelMasterV4;
}
