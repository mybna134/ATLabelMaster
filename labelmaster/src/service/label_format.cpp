#include "label_format.hpp"

#include "util/id_convert.hpp"
#include "util/svg_constants.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPolygonF>
#include <QSaveFile>
#include <QTextStream>
#include <QTransform>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace labelmaster::service::label_format {
namespace {

struct BoundingBox {
    double centerX = 0.0;
    double centerY = 0.0;
    double width   = 0.0;
    double height  = 0.0;
};

void setError(QString* destination, const QString& message) {
    if (destination)
        *destination = message;
}

bool requireFieldCount(const QStringList& fields, int expected, QString& error) {
    if (fields.size() == expected)
        return true;
    error = QString("期望 %1 个字段，实际 %2 个").arg(expected).arg(fields.size());
    return false;
}

bool parseInt(const QString& text, const QString& name, int& value, QString& error) {
    bool ok = false;
    value   = text.toInt(&ok);
    if (!ok)
        error = QString("%1不是整数: %2").arg(name, text);
    return ok;
}

bool parseDouble(const QString& text, const QString& name, double& value, QString& error) {
    bool ok = false;
    value   = text.toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        error = QString("%1不是有限数字: %2").arg(name, text);
        return false;
    }
    return true;
}

bool validColor(int color) { return color >= 0 && color < 4; }
bool validSize(int size) { return size == 0 || size == 1; }
bool validClass(int cls) { return cls >= 0 && cls < 8; }

bool parseNativeIdentity(
    const QStringList& fields, int& color, int& size, int& cls, QString& error) {
    if (!parseInt(fields[0], "颜色", color, error) || !parseInt(fields[1], "尺寸", size, error)
        || !parseInt(fields[2], "类别", cls, error)) {
        return false;
    }
    if (!validColor(color) || !validSize(size) || !validClass(cls)) {
        error = QString("无效的颜色/尺寸/类别 ID: %1 %2 %3").arg(color).arg(size).arg(cls);
        return false;
    }
    return true;
}

bool parsePoints(
    const QStringList& fields, int start, const QSize& imageSize, std::array<QPointF, 4>& points,
    bool& normalized, QString& error) {
    if (imageSize.width() <= 0 || imageSize.height() <= 0) {
        error = "图片尺寸无效";
        return false;
    }
    if (fields.size() < start + 8) {
        error = "角点字段不足";
        return false;
    }

    std::array<double, 8> values{};
    normalized = true;
    for (int i = 0; i < 8; ++i) {
        if (!parseDouble(fields[start + i], "角点", values[i], error))
            return false;
        normalized = normalized && std::abs(values[i]) <= 1.5;
    }

    const double width  = imageSize.width();
    const double height = imageSize.height();
    for (int i = 0; i < 4; ++i) {
        double x = values[i * 2];
        double y = values[i * 2 + 1];
        if (normalized) {
            x *= width;
            y *= height;
        }
        points[i] = QPointF(
            std::clamp(x, 0.0, std::max(0.0, width - 1.0)),
            std::clamp(y, 0.0, std::max(0.0, height - 1.0)));
    }
    return true;
}

void assignPoints(Armor& armor, const std::array<QPointF, 4>& points) {
    armor.p0 = points[0];
    armor.p1 = points[1];
    armor.p2 = points[2];
    armor.p3 = points[3];
}

int classIdFromToken(const QString& token) {
    const QString value = token.trimmed().toUpper();
    if (value == "G" || value == "0")
        return 0;
    if (value == "1" || value == "2" || value == "3" || value == "4" || value == "5")
        return value.toInt();
    if (value == "O" || value == "6")
        return 6;
    if (value == "B" || value == "BS" || value == "BB" || value == "7")
        return 7;
    return -1;
}

bool decodePoseClass(int poseClass, PoseClassScheme scheme, Armor& armor, QString& error) {
    int colorId = -1;
    int classId = -1;
    int size    = 0;
    if (scheme == PoseClassScheme::Classes36) {
        if (poseClass < 0 || poseClass >= 36) {
            error = QString("无效的 V6 36 类 class_id: %1").arg(poseClass);
            return false;
        }
        colorId       = poseClass / 9;
        const int tag = poseClass % 9;
        classId       = tag <= 6 ? tag : 7;
        size          = tag == 1 || tag == 8 ? 1 : 0;
    } else {
        static constexpr std::array<int, 7> classes{2, 3, 4, 5, 7, 0, 6};
        if (poseClass < 0 || poseClass >= 14) {
            error = QString("无效的 V6 14 类 class_id: %1").arg(poseClass);
            return false;
        }
        colorId = poseClass < 7 ? 0 : 1;
        classId = classes[poseClass % 7];
        size    = 0;
    }
    armor.color = IdConvert::colorId2Letter(colorId);
    armor.cls   = IdConvert::idCollect2Token(classId);
    armor.size  = size;
    return true;
}

bool encodePoseClass(const Armor& armor, PoseClassScheme scheme, int& poseClass, QString& error) {
    const int colorId = IdConvert::colorLetter2Id(armor.color);
    const int classId = classIdFromToken(armor.cls);
    if (!validColor(colorId) || !validClass(classId)) {
        error = "V6 标注的颜色或类别无效";
        return false;
    }
    if (scheme == PoseClassScheme::Classes36) {
        const int tag = classId == 7 ? (armor.size == 1 ? 8 : 7) : classId;
        poseClass     = colorId * 9 + tag;
        return true;
    }

    if (colorId > 1 || classId == 1) {
        error = "当前颜色/类别不存在于 V6 14 类体系";
        return false;
    }
    int suffix = -1;
    switch (classId) {
    case 2: suffix = 0; break;
    case 3: suffix = 1; break;
    case 4: suffix = 2; break;
    case 5: suffix = 3; break;
    case 7: suffix = 4; break;
    case 0: suffix = 5; break;
    case 6: suffix = 6; break;
    default: break;
    }
    if (suffix < 0) {
        error = "当前类别不存在于 V6 14 类体系";
        return false;
    }
    poseClass = colorId * 7 + suffix;
    return true;
}

bool parsePoseVisibility(const QString& text, int& value, QString& error) {
    double parsed = 0.0;
    if (!parseDouble(text, "关键点可见性", parsed, error))
        return false;
    const int rounded = int(std::round(parsed));
    if (std::abs(parsed - rounded) > 0.000001 || rounded < 0 || rounded > 2) {
        error = QString("关键点可见性只能是 0、1、2: %1").arg(text);
        return false;
    }
    value = rounded;
    return true;
}

bool decodeV4Class(int v4Class, Armor& armor, QString& error) {
    if (v4Class < 0 || v4Class >= 64) {
        error = QString("无效的 V4 class_id: %1").arg(v4Class);
        return false;
    }

    const int colorId = v4Class / 16;
    const int sizeId  = (v4Class % 16) / 8;
    const int classId = v4Class % 8;
    if (!validColor(colorId) || !validSize(sizeId) || !validClass(classId)) {
        error = QString("无效的 V4 color/size/class: %1 %2 %3")
                    .arg(colorId)
                    .arg(sizeId)
                    .arg(classId);
        return false;
    }

    armor.color = IdConvert::colorId2Letter(colorId);
    armor.size  = sizeId;
    armor.cls   = IdConvert::idCollect2Token(classId);
    return true;
}

int encodeV4Class(int colorId, int sizeId, int classId) {
    return colorId * 16 + sizeId * 8 + classId;
}

bool parseLegacyClass(
    int rawClass, int colorId, const std::array<QPointF, 4>& points, Armor& armor, QString& error) {
    if (!validColor(colorId)) {
        error = QString("无效颜色 ID: %1").arg(colorId);
        return false;
    }
    armor.size  = (rawClass == 1 || (rawClass >= 8 && rawClass <= 11)) ? 1 : 0;
    int classId = rawClass;
    if (rawClass == 8)
        classId = 7;
    else if (rawClass >= 9 && rawClass <= 11)
        classId = rawClass - 6;
    if (!validClass(classId)) {
        error = QString("无效旧格式类别 ID: %1").arg(rawClass);
        return false;
    }
    armor.color = IdConvert::colorId2Letter(colorId);
    armor.cls   = IdConvert::idCollect2Token(classId);
    assignPoints(armor, points);
    return true;
}

bool parseLine(
    const QStringList& fields, const QSize& imageSize, DataSet format, PoseClassScheme poseScheme,
    Armor& armor, QString& error) {
    armor = Armor{};
    std::array<QPointF, 4> points{};
    bool normalized = true;

    switch (format) {
    case DataSet::LabelMasterV6: {
        if (!requireFieldCount(fields, 17, error))
            return false;
        int poseClass = 0;
        if (!parseInt(fields[0], "V6 class_id", poseClass, error)
            || !decodePoseClass(poseClass, poseScheme, armor, error)) {
            return false;
        }
        std::array<double, 4> bbox{};
        for (int i = 0; i < 4; ++i) {
            if (!parseDouble(fields[1 + i], "V6 bbox", bbox[i], error) || bbox[i] < 0.0
                || bbox[i] > 1.0) {
                if (error.isEmpty())
                    error = "V6 bbox 必须在 [0, 1] 范围内";
                return false;
            }
        }
        armor.norm_x = bbox[0];
        armor.norm_y = bbox[1];
        armor.norm_w = bbox[2];
        armor.norm_h = bbox[3];
        for (int i = 0; i < 4; ++i) {
            double x        = 0.0;
            double y        = 0.0;
            const int start = 5 + i * 3;
            if (!parseDouble(fields[start], "V6 关键点 x", x, error)
                || !parseDouble(fields[start + 1], "V6 关键点 y", y, error) || x < 0.0 || x > 1.0
                || y < 0.0 || y > 1.0
                || !parsePoseVisibility(fields[start + 2], armor.keypointVisibility[i], error)) {
                if (error.isEmpty())
                    error = "V6 关键点必须在 [0, 1] 范围内";
                return false;
            }
            points[i] = QPointF(x * imageSize.width(), y * imageSize.height());
        }
        armor.leftVisible  = armor.keypointVisibility[0] > 0 || armor.keypointVisibility[1] > 0;
        armor.rightVisible = armor.keypointVisibility[2] > 0 || armor.keypointVisibility[3] > 0;
        assignPoints(armor, points);
        return true;
    }
    case DataSet::LabelMasterV5: {
        if (!requireFieldCount(fields, 15, error))
            return false;
        int color = 0, size = 0, cls = 0;
        if (!parseNativeIdentity(fields, color, size, cls, error))
            return false;
        std::array<double, 10> coordinates{};
        const std::array<int, 10> indices{3, 4, 5, 6, 7, 8, 10, 11, 12, 13};
        for (int i = 0; i < int(indices.size()); ++i) {
            if (!parseDouble(fields[indices[i]], "V5 坐标", coordinates[i], error)
                || coordinates[i] < 0.0 || coordinates[i] > 1.0) {
                if (error.isEmpty())
                    error = "V5 坐标必须在 [0, 1] 范围内";
                return false;
            }
        }
        int leftVisible = 0, rightVisible = 0;
        if (!parseInt(fields[9], "左灯条可见性", leftVisible, error)
            || !parseInt(fields[14], "右灯条可见性", rightVisible, error)
            || (leftVisible != 0 && leftVisible != 1) || (rightVisible != 0 && rightVisible != 1)) {
            if (error.isEmpty())
                error = "灯条可见性只能是 0 或 1";
            return false;
        }
        if (!leftVisible && !rightVisible) {
            error = "左右灯条不能同时不可见";
            return false;
        }
        const double centerX = coordinates[0];
        const double centerY = coordinates[1];
        points               = {
            QPointF(coordinates[2] * imageSize.width(), coordinates[3] * imageSize.height()),
            QPointF(coordinates[4] * imageSize.width(), coordinates[5] * imageSize.height()),
            QPointF(coordinates[8] * imageSize.width(), coordinates[9] * imageSize.height()),
            QPointF(coordinates[6] * imageSize.width(), coordinates[7] * imageSize.height()),
        };
        const double computedX =
            (coordinates[2] + coordinates[4] + coordinates[6] + coordinates[8]) / 4.0;
        const double computedY =
            (coordinates[3] + coordinates[5] + coordinates[7] + coordinates[9]) / 4.0;
        if (std::abs(centerX - computedX) > 0.00001 || std::abs(centerY - computedY) > 0.00001) {
            error = "V5 中心点与四个灯条端点的均值不一致";
            return false;
        }
        armor.color              = IdConvert::colorId2Letter(color);
        armor.size               = size;
        armor.cls                = IdConvert::idCollect2Token(cls);
        armor.leftVisible        = leftVisible;
        armor.rightVisible       = rightVisible;
        armor.keypointVisibility = {
            leftVisible ? 2 : 0,
            leftVisible ? 2 : 0,
            rightVisible ? 2 : 0,
            rightVisible ? 2 : 0,
        };
        assignPoints(armor, points);
        return true;
    }
    case DataSet::LabelMasterV4: {
        if (!requireFieldCount(fields, 13, error))
            return false;
        int v4Class = 0;
        if (!parseInt(fields[0], "V4 class_id", v4Class, error)
            || !decodeV4Class(v4Class, armor, error)) {
            return false;
        }
        std::array<double, 4> bbox{};
        for (int i = 0; i < 4; ++i) {
            if (!parseDouble(fields[1 + i], "V4 bbox", bbox[i], error))
                return false;
        }
        if (!parsePoints(fields, 5, imageSize, points, normalized, error))
            return false;
        assignPoints(armor, points);
        if (normalized) {
            armor.norm_x = std::clamp(bbox[0], 0.0, 1.0);
            armor.norm_y = std::clamp(bbox[1], 0.0, 1.0);
            armor.norm_w = std::clamp(bbox[2], 0.0, 1.0);
            armor.norm_h = std::clamp(bbox[3], 0.0, 1.0);
        } else {
            armor.norm_x = std::clamp(bbox[0] / imageSize.width(), 0.0, 1.0);
            armor.norm_y = std::clamp(bbox[1] / imageSize.height(), 0.0, 1.0);
            armor.norm_w = std::clamp(bbox[2] / imageSize.width(), 0.0, 1.0);
            armor.norm_h = std::clamp(bbox[3] / imageSize.height(), 0.0, 1.0);
        }
        return true;
    }
    case DataSet::LabelMaster2:
    case DataSet::LabelMaster3: {
        const bool extended = format == DataSet::LabelMaster3;
        if (!requireFieldCount(fields, extended ? 15 : 11, error))
            return false;
        int color = 0, size = 0, cls = 0;
        if (!parseNativeIdentity(fields, color, size, cls, error)
            || !parsePoints(fields, extended ? 7 : 3, imageSize, points, normalized, error)) {
            return false;
        }
        armor.color = IdConvert::colorId2Letter(color);
        armor.size  = size;
        armor.cls   = IdConvert::idCollect2Token(cls);
        assignPoints(armor, points);
        if (extended) {
            std::array<double, 4> bbox{};
            for (int i = 0; i < 4; ++i) {
                if (!parseDouble(fields[3 + i], "V3 bbox", bbox[i], error))
                    return false;
            }
            armor.norm_x = std::clamp(normalized ? bbox[0] : bbox[0] / imageSize.width(), 0.0, 1.0);
            armor.norm_y =
                std::clamp(normalized ? bbox[1] : bbox[1] / imageSize.height(), 0.0, 1.0);
            armor.norm_w = std::clamp(normalized ? bbox[2] : bbox[2] / imageSize.width(), 0.0, 1.0);
            armor.norm_h =
                std::clamp(normalized ? bbox[3] : bbox[3] / imageSize.height(), 0.0, 1.0);
        }
        return true;
    }
    case DataSet::LabelMaster: {
        if (!requireFieldCount(fields, 10, error))
            return false;
        int color = 0, rawClass = 0;
        if (!parseInt(fields[0], "颜色", color, error)
            || !parseInt(fields[1], "类别", rawClass, error)
            || !parsePoints(fields, 2, imageSize, points, normalized, error)) {
            return false;
        }
        armor.size  = rawClass == 1 ? 1 : 0;
        int classId = rawClass == 5 ? 6 : ((rawClass == 6 || rawClass == 7) ? 7 : rawClass);
        if (!validColor(color) || !validClass(classId)) {
            error = QString("无效 V1 颜色/类别 ID: %1 %2").arg(color).arg(rawClass);
            return false;
        }
        armor.color = IdConvert::colorId2Letter(color);
        armor.cls   = IdConvert::idCollect2Token(classId);
        assignPoints(armor, points);
        return true;
    }
    case DataSet::HITSZ: {
        if (!requireFieldCount(fields, 10, error)
            || !parsePoints(fields, 0, imageSize, points, normalized, error)) {
            return false;
        }
        int rawClass = 0, color = 0;
        if (!parseInt(fields[8], "类别", rawClass, error)
            || !parseInt(fields[9], "颜色", color, error)) {
            return false;
        }
        return parseLegacyClass(rawClass, color, points, armor, error);
    }
    case DataSet::UPC: {
        if (!requireFieldCount(fields, 10, error))
            return false;
        int color = 0, rawClass = 0;
        if (!parseInt(fields[0], "颜色", color, error)
            || !parseInt(fields[1], "类别", rawClass, error)
            || !parsePoints(fields, 2, imageSize, points, normalized, error)) {
            return false;
        }
        return parseLegacyClass(rawClass, color, points, armor, error);
    }
    case DataSet::NWPU: {
        if (!requireFieldCount(fields, 9, error))
            return false;
        int combined = 0;
        if (!parseInt(fields[0], "组合类别", combined, error) || combined < 0 || combined >= 64
            || !parsePoints(fields, 1, imageSize, points, normalized, error)) {
            if (error.isEmpty())
                error = QString("无效 NWPU 组合类别: %1").arg(combined);
            return false;
        }
        armor.color = IdConvert::colorId2Letter(combined / 16);
        armor.size  = (combined % 16) / 8;
        armor.cls   = IdConvert::idCollect2Token(combined % 8);
        assignPoints(armor, points);
        return true;
    }
    case DataSet::Auto: error = "读取标签时必须指定输入格式"; return false;
    }
    error = "未知输入格式";
    return false;
}

BoundingBox projectedBoundingBox(const Armor& armor, const QSize& imageSize) {
    const auto& svgTemplate = armor.size == 0 ? labelmaster::util::SvgConstants::smallArmor()
                                              : labelmaster::util::SvgConstants::bigArmor();
    QPolygonF outer;
    outer << QPointF(0.0, 0.0) << QPointF(0.0, svgTemplate.height)
          << QPointF(svgTemplate.width, svgTemplate.height) << QPointF(svgTemplate.width, 0.0);
    QPolygonF anchors;
    anchors << armor.p0 << armor.p1 << armor.p2 << armor.p3;

    QPolygonF projected = anchors;
    QTransform transform;
    if (QTransform::quadToQuad(svgTemplate.anchors, anchors, transform))
        projected = transform.map(outer);

    double minX = std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double maxY = std::numeric_limits<double>::lowest();
    for (const QPointF& point : projected) {
        minX = std::min(minX, point.x());
        minY = std::min(minY, point.y());
        maxX = std::max(maxX, point.x());
        maxY = std::max(maxY, point.y());
    }

    const double width  = std::max(1, imageSize.width());
    const double height = std::max(1, imageSize.height());
    return {
        std::clamp((minX + maxX) / (2.0 * width), 0.0, 1.0),
        std::clamp((minY + maxY) / (2.0 * height), 0.0, 1.0),
        std::clamp((maxX - minX) / width, 0.0, 1.0),
        std::clamp((maxY - minY) / height, 0.0, 1.0),
    };
}

std::array<QPointF, 4> normalizedPoints(const Armor& armor, const QSize& imageSize) {
    const double width  = std::max(1, imageSize.width());
    const double height = std::max(1, imageSize.height());
    const std::array<QPointF, 4> source{armor.p0, armor.p1, armor.p2, armor.p3};
    std::array<QPointF, 4> result{};
    for (int i = 0; i < int(source.size()); ++i) {
        result[i] = QPointF(
            std::clamp(source[i].x() / width, 0.0, 1.0),
            std::clamp(source[i].y() / height, 0.0, 1.0));
    }
    return result;
}

void writePoint(QTextStream& stream, const QPointF& point) {
    stream << point.x() << ' ' << point.y();
}

bool labelHasDataLines(const QString& path, bool& hasData, QString& error) {
    hasData = false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        error = QString("无法读取标签: %1").arg(path);
        return false;
    }

    QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        const int comment = line.indexOf('#');
        if (comment >= 0)
            line = line.left(comment);
        if (!line.trimmed().isEmpty()) {
            hasData = true;
            return true;
        }
    }
    return true;
}

} // namespace

QString outputFormatName(LabelOutputFormat format) {
    switch (format) {
    case LabelOutputFormat::Points11: return "Points Only (11 fields)";
    case LabelOutputFormat::RectPoints15: return "Rect + Points (15 fields)";
    case LabelOutputFormat::LabelMasterV4: return "LabelMaster V4 (13 fields)";
    case LabelOutputFormat::LabelMasterV6: return "LabelMaster V6 / YOLO Pose (17 fields)";
    }
    return "Unknown";
}

QString dataSetName(DataSet format) {
    switch (format) {
    case DataSet::Auto: return "Auto";
    case DataSet::LabelMaster: return "LabelMaster V1";
    case DataSet::LabelMaster2: return "LabelMaster V2";
    case DataSet::LabelMaster3: return "LabelMaster V3";
    case DataSet::LabelMasterV4: return "LabelMaster V4";
    case DataSet::LabelMasterV5: return "LabelMaster V5";
    case DataSet::LabelMasterV6: return "LabelMaster V6 / YOLO Pose";
    case DataSet::HITSZ: return "HITSZ";
    case DataSet::UPC: return "UPC";
    case DataSet::NWPU: return "NWPU";
    }
    return "Unknown";
}

PoseClassScheme detectPoseClassScheme(const QStringList& labelPaths) {
    bool foundClass  = false;
    int maximumClass = -1;
    for (const QString& path : labelPaths) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QTextStream stream(&file);
        while (!stream.atEnd()) {
            QString line      = stream.readLine();
            const int comment = line.indexOf('#');
            if (comment >= 0)
                line = line.left(comment);
            const QStringList fields = line.simplified().split(' ', Qt::SkipEmptyParts);
            if (fields.size() != 17)
                continue;
            bool ok           = false;
            const int classId = fields[0].toInt(&ok);
            if (!ok)
                continue;
            foundClass   = true;
            maximumClass = std::max(maximumClass, classId);
        }
    }
    if (!foundClass)
        return PoseClassScheme::Classes36;
    return maximumClass >= 14 ? PoseClassScheme::Classes36 : PoseClassScheme::Classes14;
}

FormatDetectionResult detectDataSetFormat(const QVector<LabelFileSample>& samples) {
    FormatDetectionResult result;
    const QVector<DataSet> allFormats{
        DataSet::LabelMasterV6,
        DataSet::LabelMasterV5,
        DataSet::LabelMasterV4,
        DataSet::LabelMaster3,
        DataSet::LabelMaster2,
        DataSet::LabelMaster,
        DataSet::HITSZ,
        DataSet::UPC,
        DataSet::NWPU,
    };
    QVector<DataSet> commonCandidates = allFormats;
    QStringList labelPaths;
    labelPaths.reserve(samples.size());
    for (const LabelFileSample& sample : samples)
        labelPaths.push_back(sample.path);
    result.poseClassScheme = detectPoseClassScheme(labelPaths);

    for (const LabelFileSample& sample : samples) {
        bool hasData = false;
        QString readError;
        if (!labelHasDataLines(sample.path, hasData, readError)) {
            result.error = readError;
            return result;
        }
        if (!hasData)
            continue;
        result.hasAnnotations = true;

        QVector<DataSet> fileCandidates;
        for (DataSet format : allFormats) {
            QVector<Armor> parsed;
            if (readLabelFile(
                    sample.path, sample.imageSize, format, parsed, nullptr,
                    result.poseClassScheme)) {
                fileCandidates.push_back(format);
            }
        }

        // V5 has two visibility fields plus an exact center-point invariant. These
        // strong signatures take priority over the deliberately permissive V3 parser.
        if (fileCandidates.contains(DataSet::LabelMasterV5))
            fileCandidates.removeOne(DataSet::LabelMaster3);

        if (fileCandidates.isEmpty()) {
            result.candidates.clear();
            result.error = QString("%1 不符合任何已支持的标签格式")
                               .arg(QFileInfo(sample.path).fileName());
            return result;
        }

        QVector<DataSet> intersection;
        for (DataSet candidate : commonCandidates) {
            if (fileCandidates.contains(candidate))
                intersection.push_back(candidate);
        }
        commonCandidates = intersection;
        if (commonCandidates.isEmpty()) {
            result.candidates.clear();
            result.error = QString("目录内标签格式不一致，或 %1 与其他标签格式冲突")
                               .arg(QFileInfo(sample.path).fileName());
            return result;
        }
    }

    if (!result.hasAnnotations) {
        result.format = DataSet::LabelMasterV6;
        result.candidates = {DataSet::LabelMasterV6};
        result.poseClassScheme = PoseClassScheme::Classes36;
        return result;
    }

    result.candidates = commonCandidates;
    if (commonCandidates.size() == 1) {
        result.format = commonCandidates.front();
        return result;
    }

    QStringList names;
    for (DataSet candidate : commonCandidates)
        names.push_back(dataSetName(candidate));
    result.error = QString("标签同时符合多种格式：%1").arg(names.join("、"));
    return result;
}

bool readLabelFile(
    const QString& path, const QSize& imageSize, DataSet format, QVector<Armor>& armors,
    QString* error, PoseClassScheme poseScheme) {
    armors.clear();
    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        setError(error, QString("无法打开标签: %1").arg(path));
        return false;
    }

    QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
    int lineNumber = 0;
    while (!stream.atEnd()) {
        ++lineNumber;
        QString line      = stream.readLine();
        const int comment = line.indexOf('#');
        if (comment >= 0)
            line = line.left(comment);
        line = line.simplified();
        if (line.isEmpty())
            continue;

        Armor armor;
        QString lineError;
        if (!parseLine(line.split(' '), imageSize, format, poseScheme, armor, lineError)) {
            setError(error, QString("第 %1 行格式错误: %2").arg(lineNumber).arg(lineError));
            armors.clear();
            return false;
        }
        armors.push_back(armor);
    }
    return true;
}

bool writeLabelFile(
    const QString& path, const QSize& imageSize, LabelOutputFormat format,
    const QVector<Armor>& armors, QString* error, PoseClassScheme poseScheme) {
    if (imageSize.width() <= 0 || imageSize.height() <= 0) {
        setError(error, "图片尺寸无效");
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        setError(error, QString("无法写入标签: %1").arg(path));
        return false;
    }

    QTextStream stream(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    stream.setRealNumberPrecision(6);

    for (int index = 0; index < armors.size(); ++index) {
        const Armor& armor = armors[index];
        const int colorId  = IdConvert::colorLetter2Id(armor.color);
        const int classId  = classIdFromToken(armor.cls);
        if (!validColor(colorId) || !validSize(armor.size) || !validClass(classId)) {
            setError(error, QString("第 %1 个标注的颜色/尺寸/类别无效").arg(index + 1));
            file.cancelWriting();
            return false;
        }
        const auto points = normalizedPoints(armor, imageSize);

        if (format == LabelOutputFormat::LabelMasterV6) {
            int poseClass = 0;
            QString poseError;
            if (!encodePoseClass(armor, poseScheme, poseClass, poseError)) {
                setError(
                    error,
                    QString("第 %1 个标注无法编码 class_id: %2").arg(index + 1).arg(poseError));
                file.cancelWriting();
                return false;
            }
            const BoundingBox bbox = armor.norm_x >= 0.0 && armor.norm_y >= 0.0
                    && armor.norm_w >= 0.0 && armor.norm_h >= 0.0
                ? BoundingBox{
                      std::clamp(armor.norm_x, 0.0, 1.0),
                      std::clamp(armor.norm_y, 0.0, 1.0),
                      std::clamp(armor.norm_w, 0.0, 1.0),
                      std::clamp(armor.norm_h, 0.0, 1.0)}
                : projectedBoundingBox(armor, imageSize);
            stream << poseClass << ' ' << bbox.centerX << ' ' << bbox.centerY << ' ' << bbox.width
                   << ' ' << bbox.height;
            for (int pointIndex = 0; pointIndex < 4; ++pointIndex) {
                const int visibility = armor.keypointVisibility[pointIndex];
                if (visibility < 0 || visibility > 2) {
                    setError(
                        error, QString("第 %1 个标注的第 %2 个关键点可见性无效")
                                   .arg(index + 1)
                                   .arg(pointIndex + 1));
                    file.cancelWriting();
                    return false;
                }
                stream << ' ';
                writePoint(stream, points[pointIndex]);
                stream << ' ' << visibility;
            }
            stream << '\n';
            continue;
        }

        const BoundingBox bbox = projectedBoundingBox(armor, imageSize);
        if (format == LabelOutputFormat::LabelMasterV4) {
            stream << encodeV4Class(colorId, armor.size, classId) << ' ' << bbox.centerX << ' '
                   << bbox.centerY << ' ' << bbox.width << ' ' << bbox.height << ' ';
        } else {
            stream << colorId << ' ' << armor.size << ' ' << classId << ' ';
            if (format == LabelOutputFormat::RectPoints15) {
                stream << bbox.centerX << ' ' << bbox.centerY << ' ' << bbox.width << ' '
                       << bbox.height << ' ';
            }
        }
        for (int pointIndex = 0; pointIndex < 4; ++pointIndex) {
            if (pointIndex > 0)
                stream << ' ';
            writePoint(stream, points[pointIndex]);
        }
        stream << '\n';
    }

    stream.flush();
    if (!file.commit()) {
        setError(error, QString("无法原子替换标签: %1").arg(path));
        return false;
    }
    return true;
}

bool convertLabelFile(
    const QString& path, const QSize& imageSize, DataSet source, LabelOutputFormat target,
    QString* error, PoseClassScheme poseScheme) {
    QVector<Armor> armors;
    if (!readLabelFile(path, imageSize, source, armors, error, poseScheme))
        return false;
    return writeLabelFile(path, imageSize, target, armors, error, poseScheme);
}

} // namespace labelmaster::service::label_format
