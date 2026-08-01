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
#include <atomic>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace labelmaster::service::label_format {
namespace {

struct BoundingBox {
    double centerX = 0.0;
    double centerY = 0.0;
    double width   = 0.0;
    double height  = 0.0;
};

struct PreparedLine {
    int lineNumber = 0;
    QStringList fields;
};

struct PreparedLabel {
    QVector<PreparedLine> lines;
    QString error;
};

constexpr QSize kNormalizedSpaceSize{1, 1};
constexpr int kValidationWorkerCount = 8;
constexpr int kValidationChunkSize   = 32;

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
    // Every supported label specification stores keypoints as normalized values.
    // Do not infer pixel coordinates from magnitude: out-of-image points may be
    // negative or greater than 1.
    normalized = true;
    for (int i = 0; i < 8; ++i) {
        if (!parseDouble(fields[start + i], "角点", values[i], error))
            return false;
    }

    const double width  = imageSize.width();
    const double height = imageSize.height();
    for (int i = 0; i < 4; ++i) {
        const double x = values[i * 2] * width;
        const double y = values[i * 2 + 1] * height;
        points[i]      = QPointF(x, y);
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

bool decodeV5Class(int poseClass, Armor& armor, QString& error) {
    static constexpr std::array<int, 7> classes{2, 3, 4, 5, 7, 0, 6};
    if (poseClass < 0 || poseClass >= 14) {
        error = QString("无效的 V5 14 类 class_id: %1").arg(poseClass);
        return false;
    }
    const int colorId = poseClass < 7 ? 0 : 1;
    const int classId = classes[poseClass % 7];
    armor.color = IdConvert::colorId2Letter(colorId);
    armor.cls   = IdConvert::idCollect2Token(classId);
    armor.size  = 0;
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
    if (v4Class < 0 || v4Class >= 36) {
        error = QString("无效的 V4 36 类 class_id: %1").arg(v4Class);
        return false;
    }

    const int colorId = v4Class / 9;
    const int tag     = v4Class % 9;
    const int sizeId  = tag == 1 || tag == 8 ? 1 : 0;
    const int classId = tag <= 6 ? tag : 7;
    if (!validColor(colorId) || !validSize(sizeId) || !validClass(classId)) {
        error =
            QString("无效的 V4 color/size/class: %1 %2 %3").arg(colorId).arg(sizeId).arg(classId);
        return false;
    }

    armor.color = IdConvert::colorId2Letter(colorId);
    armor.size  = sizeId;
    armor.cls   = IdConvert::idCollect2Token(classId);
    return true;
}

bool encodeV4Class(
    int colorId, int sizeId, int classId, int& encoded, QString& error) {
    int tag = classId;
    if (classId == 1) {
        if (sizeId != 1) {
            error = "V4 36 类体系中的 1 号必须是大装甲";
            return false;
        }
    } else if (classId == 7) {
        tag = sizeId == 1 ? 8 : 7;
    } else if (sizeId != 0) {
        error = "该类别在 V4 36 类体系中不存在大装甲编码";
        return false;
    }
    encoded = colorId * 9 + tag;
    return true;
}

bool decodeUnionSecretClass(int combined, Armor& armor, QString& error) {
    if (combined < 0 || combined >= 39) {
        error = QString("无效的 UnionSecret class_id: %1（允许范围 0～38）").arg(combined);
        return false;
    }

    const int colorId = combined / 13;
    const int tag     = combined % 13;
    int sizeId        = 0;
    int classId       = tag;
    if (tag >= 8) {
        sizeId = 1;
        if (tag == 8)
            classId = 7; // Big Base
        else if (tag == 9)
            classId = 0; // Big G
        else
            classId = tag - 7; // Big 3/4/5
    }

    if (!validColor(colorId) || !validSize(sizeId) || !validClass(classId)) {
        error = QString("无法解码 UnionSecret class_id: %1").arg(combined);
        return false;
    }
    armor.color = IdConvert::colorId2Letter(colorId);
    armor.size  = sizeId;
    armor.cls   = IdConvert::idCollect2Token(classId);
    return true;
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
    const QStringList& fields, const QSize& imageSize, DataSet format, Armor& armor,
    QString& error) {
    armor = Armor{};
    std::array<QPointF, 4> points{};
    bool normalized = true;

    switch (format) {
    case DataSet::LabelMasterV5: {
        if (!requireFieldCount(fields, 17, error))
            return false;
        int poseClass = 0;
        if (!parseInt(fields[0], "V5 class_id", poseClass, error)
            || !decodeV5Class(poseClass, armor, error)) {
            return false;
        }
        std::array<double, 4> bbox{};
        for (int i = 0; i < 4; ++i) {
            if (!parseDouble(fields[1 + i], "V5 bbox", bbox[i], error)) {
                return false;
            }
        }
        if (bbox[2] < 0.0 || bbox[3] < 0.0) {
            error = "V5 bbox 的宽高不能为负数";
            return false;
        }
        armor.norm_x = bbox[0];
        armor.norm_y = bbox[1];
        armor.norm_w = bbox[2];
        armor.norm_h = bbox[3];
        for (int i = 0; i < 4; ++i) {
            double x        = 0.0;
            double y        = 0.0;
            const int start = 5 + i * 3;
            if (!parseDouble(fields[start], "V5 关键点 x", x, error)
                || !parseDouble(fields[start + 1], "V5 关键点 y", y, error)
                || !parsePoseVisibility(fields[start + 2], armor.keypointVisibility[i], error)) {
                return false;
            }
            points[i] = QPointF(x * imageSize.width(), y * imageSize.height());
        }
        assignPoints(armor, points);
        return true;
    }
    case DataSet::LabelMasterV6: {
        if (!requireFieldCount(fields, 19, error))
            return false;
        int color = 0, size = 0, cls = 0;
        if (!parseNativeIdentity(fields, color, size, cls, error))
            return false;
        std::array<double, 4> bbox{};
        for (int i = 0; i < 4; ++i) {
            if (!parseDouble(fields[3 + i], "V6 bbox", bbox[i], error)) {
                return false;
            }
        }
        if (bbox[2] < 0.0 || bbox[3] < 0.0) {
            error = "V6 bbox 的宽高不能为负数";
            return false;
        }
        armor.norm_x = bbox[0];
        armor.norm_y = bbox[1];
        armor.norm_w = bbox[2];
        armor.norm_h = bbox[3];
        for (int i = 0; i < 4; ++i) {
            double x        = 0.0;
            double y        = 0.0;
            const int start = 7 + i * 3;
            if (!parseDouble(fields[start], "V6 关键点 x", x, error)
                || !parseDouble(fields[start + 1], "V6 关键点 y", y, error)
                || !parsePoseVisibility(fields[start + 2], armor.keypointVisibility[i], error)) {
                return false;
            }
            points[i] = QPointF(x * imageSize.width(), y * imageSize.height());
        }
        armor.color = IdConvert::colorId2Letter(color);
        armor.size  = size;
        armor.cls   = IdConvert::idCollect2Token(cls);
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
        if (bbox[2] < 0.0 || bbox[3] < 0.0) {
            error = "V4 bbox 的宽高不能为负数";
            return false;
        }
        if (!parsePoints(fields, 5, imageSize, points, normalized, error))
            return false;
        assignPoints(armor, points);
        if (normalized) {
            armor.norm_x = bbox[0];
            armor.norm_y = bbox[1];
            armor.norm_w = bbox[2];
            armor.norm_h = bbox[3];
        } else {
            armor.norm_x = bbox[0] / imageSize.width();
            armor.norm_y = bbox[1] / imageSize.height();
            armor.norm_w = bbox[2] / imageSize.width();
            armor.norm_h = bbox[3] / imageSize.height();
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
            if (bbox[2] < 0.0 || bbox[3] < 0.0) {
                error = "V3 bbox 的宽高不能为负数";
                return false;
            }
            armor.norm_x = normalized ? bbox[0] : bbox[0] / imageSize.width();
            armor.norm_y = normalized ? bbox[1] : bbox[1] / imageSize.height();
            armor.norm_w = normalized ? bbox[2] : bbox[2] / imageSize.width();
            armor.norm_h = normalized ? bbox[3] : bbox[3] / imageSize.height();
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
    case DataSet::UnionSecret: {
        if (!requireFieldCount(fields, 9, error))
            return false;
        int combined = 0;
        if (!parseInt(fields[0], "UnionSecret class_id", combined, error)
            || !decodeUnionSecretClass(combined, armor, error)
            || !parsePoints(fields, 1, imageSize, points, normalized, error)) {
            return false;
        }
        assignPoints(armor, points);
        return true;
    }
    case DataSet::Auto: error = "读取标签时必须指定输入格式"; return false;
    }
    error = "未知输入格式";
    return false;
}

PreparedLabel readPreparedLabel(const QString& path) {
    PreparedLabel prepared;
    QFile file(path);
    if (!file.exists())
        return prepared;
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        prepared.error = QString("无法打开标签: %1").arg(path);
        return prepared;
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
        if (!line.isEmpty())
            prepared.lines.push_back({lineNumber, line.split(' ')});
    }
    return prepared;
}

PreparedLabel readPreparedLabelText(const QString& text) {
    PreparedLabel prepared;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int index = 0; index < lines.size(); ++index) {
        QString line      = lines[index];
        const int comment = line.indexOf(QLatin1Char('#'));
        if (comment >= 0)
            line = line.left(comment);
        line = line.simplified();
        if (!line.isEmpty())
            prepared.lines.push_back({index + 1, line.split(QLatin1Char(' '))});
    }
    return prepared;
}

QVector<DataSet> candidateFormatsForFieldCount(int fieldCount) {
    switch (fieldCount) {
    case 19: return {DataSet::LabelMasterV6};
    case 17: return {DataSet::LabelMasterV5};
    case 15: return {DataSet::LabelMaster3};
    case 13: return {DataSet::LabelMasterV4};
    case 11: return {DataSet::LabelMaster2};
    case 10: return {DataSet::LabelMaster, DataSet::HITSZ, DataSet::UPC};
    case 9: return {DataSet::UnionSecret, DataSet::NWPU};
    default: return {};
    }
}

bool parsePreparedLabel(
    const PreparedLabel& prepared, DataSet format, const QSize& imageSize,
    QVector<Armor>& armors, QString* error) {
    armors.clear();
    if (!prepared.error.isEmpty()) {
        setError(error, prepared.error);
        return false;
    }
    for (const PreparedLine& line : prepared.lines) {
        Armor armor;
        QString lineError;
        if (!parseLine(line.fields, imageSize, format, armor, lineError)) {
            setError(
                error, QString("第 %1 行格式错误: %2").arg(line.lineNumber).arg(lineError));
            armors.clear();
            return false;
        }
        armors.push_back(armor);
    }
    return true;
}

template <typename Function>
void parallelForChunks(int count, int workerCount, int chunkSize, Function&& function) {
    if (count <= 0)
        return;
    const int threadCount = std::min(workerCount, count);
    const int effectiveChunkSize =
        std::min(chunkSize, std::max(1, (count + threadCount - 1) / threadCount));
    std::atomic<int> next{0};
    std::vector<std::thread> workers;
    workers.reserve(threadCount);
    for (int worker = 0; worker < threadCount; ++worker) {
        workers.emplace_back([&] {
            while (true) {
                const int begin = next.fetch_add(effectiveChunkSize, std::memory_order_relaxed);
                if (begin >= count)
                    return;
                const int end = std::min(begin + effectiveChunkSize, count);
                for (int index = begin; index < end; ++index)
                    function(index);
            }
        });
    }
    for (std::thread& worker : workers)
        worker.join();
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
        (minX + maxX) / (2.0 * width),
        (minY + maxY) / (2.0 * height),
        (maxX - minX) / width,
        (maxY - minY) / height,
    };
}

std::array<QPointF, 4> normalizedPoints(const Armor& armor, const QSize& imageSize) {
    const double width  = std::max(1, imageSize.width());
    const double height = std::max(1, imageSize.height());
    const std::array<QPointF, 4> source{armor.p0, armor.p1, armor.p2, armor.p3};
    std::array<QPointF, 4> result{};
    for (int i = 0; i < int(source.size()); ++i) {
        result[i] = QPointF(source[i].x() / width, source[i].y() / height);
    }
    return result;
}

void writePoint(QTextStream& stream, const QPointF& point) {
    stream << point.x() << ' ' << point.y();
}

} // namespace

const QVector<Armor>* ParsedLabelFile::armorsFor(DataSet format) const {
    for (const ParsedLabelCandidate& candidate : candidates) {
        if (candidate.format == format)
            return &candidate.armors;
    }
    return nullptr;
}

QString outputFormatName(LabelOutputFormat format) {
    switch (format) {
    case LabelOutputFormat::Points11: return "Points Only (11 fields)";
    case LabelOutputFormat::RectPoints15: return "Rect + Points (15 fields)";
    case LabelOutputFormat::LabelMasterV4: return "LabelMaster V4 (13 fields)";
    case LabelOutputFormat::LabelMasterV6: return "LabelMaster V6 (19 fields)";
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
    case DataSet::LabelMasterV5: return "LabelMaster V5 / legacy YOLO Pose";
    case DataSet::LabelMasterV6: return "LabelMaster V6";
    case DataSet::HITSZ: return "HITSZ";
    case DataSet::UPC: return "UPC";
    case DataSet::NWPU: return "NWPU";
    case DataSet::UnionSecret: return "UnionSecret 格式";
    }
    return "Unknown";
}

FormatDetectionResult detectDataSetFormat(
    const QVector<LabelFileSample>& samples,
    const std::function<void(int current, int total)>& progress) {
    FormatDetectionResult result;
    const QVector<DataSet> allFormats{
        DataSet::LabelMasterV6, DataSet::LabelMasterV5, DataSet::LabelMasterV4,
        DataSet::LabelMaster3,  DataSet::LabelMaster2,  DataSet::LabelMaster,
        DataSet::HITSZ,         DataSet::UPC,           DataSet::UnionSecret,
        DataSet::NWPU,
    };
    QVector<int> candidateSupport(allFormats.size(), 0);
    const int total          = static_cast<int>(samples.size());
    int candidateSampleCount = 0;
    std::vector<ParsedLabelFile> parsedFiles(static_cast<size_t>(total));
    std::mutex progressMutex;
    int processed = 0;

    // 每个 label 仅在这里打开一次。字段数先把候选缩到 1～3 个，再在 1x1
    // 归一化空间解析；成功结果直接成为后续转换缓存。
    parallelForChunks(total, kValidationWorkerCount, kValidationChunkSize, [&](int index) {
        ParsedLabelFile parsedFile;
        parsedFile.sample = samples[index];
        const PreparedLabel prepared = readPreparedLabel(parsedFile.sample.path);
        if (!prepared.error.isEmpty()) {
            parsedFile.error = prepared.error;
        } else if (!prepared.lines.isEmpty()) {
            parsedFile.hasAnnotations = true;
            const int fieldCount = prepared.lines.front().fields.size();
            const QVector<DataSet> possible = candidateFormatsForFieldCount(fieldCount);
            if (possible.isEmpty()) {
                parsedFile.error = QString("不支持的字段数: %1").arg(fieldCount);
            } else {
                QString firstError;
                for (DataSet format : possible) {
                    QVector<Armor> armors;
                    QString parseError;
                    if (parsePreparedLabel(
                            prepared, format, kNormalizedSpaceSize, armors, &parseError)) {
                        parsedFile.candidates.push_back({format, std::move(armors)});
                    } else if (firstError.isEmpty()) {
                        firstError = parseError;
                    }
                }
                if (parsedFile.candidates.isEmpty())
                    parsedFile.error = firstError;
            }
        }
        parsedFiles[static_cast<size_t>(index)] = std::move(parsedFile);

        if (progress) {
            std::lock_guard lock(progressMutex);
            progress(++processed, total);
        }
    });

    result.parsedFiles.reserve(total);
    for (ParsedLabelFile& parsedFile : parsedFiles) {
        result.parsedFiles.push_back(std::move(parsedFile));
        const ParsedLabelFile& cached = result.parsedFiles.back();
        if (!cached.error.isEmpty() && !cached.hasAnnotations) {
            result.error = cached.error;
            return result;
        }
        if (!cached.hasAnnotations)
            continue;
        result.hasAnnotations = true;

        if (cached.candidates.isEmpty()) {
            const QString detail = cached.error.isEmpty() ? QString() : QString(": %1").arg(cached.error);
            result.invalidSamples.push_back(
                {cached.sample,
                 QString("%1 不符合任何已支持的标签格式%2")
                     .arg(QFileInfo(cached.sample.path).fileName(), detail)});
            continue;
        }
        ++candidateSampleCount;
        for (int index = 0; index < allFormats.size(); ++index) {
            if (cached.armorsFor(allFormats[index]))
                ++candidateSupport[index];
        }
    }

    if (!result.hasAnnotations) {
        result.format        = DataSet::LabelMasterV6;
        result.candidates    = {DataSet::LabelMasterV6};
        return result;
    }

    if (candidateSampleCount == 0) {
        result.candidates.clear();
        result.error = "所有非空标签都不符合已支持格式，无法判断导入格式";
        return result;
    }

    const int maximumSupport = *std::max_element(candidateSupport.cbegin(), candidateSupport.cend());
    QVector<DataSet> commonCandidates;
    for (int index = 0; index < allFormats.size(); ++index) {
        if (candidateSupport[index] == maximumSupport)
            commonCandidates.push_back(allFormats[index]);
    }
    result.candidates = commonCandidates;
    if (commonCandidates.contains(DataSet::LabelMaster)
        && commonCandidates.contains(DataSet::UPC)) {
        result.format              = DataSet::LabelMaster;
        result.v1UpcChoiceRequired = true;
        result.error = "10 字段标签无法自动区分 LabelMaster V1 与 UPC 格式";
        return result;
    }
    if (commonCandidates.contains(DataSet::UnionSecret)
        && commonCandidates.contains(DataSet::NWPU)) {
        result.format                  = DataSet::UnionSecret;
        result.nineFieldChoiceRequired = true;
        result.error =
            "9 字段 class_id 全部位于 0～38，无法自动区分 UnionSecret 格式与 NWPU 格式";
        return result;
    }
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
    QString* error) {
    return parsePreparedLabel(readPreparedLabel(path), format, imageSize, armors, error);
}

bool readLabelText(
    const QString& text, const QSize& imageSize, DataSet format, QVector<Armor>& armors,
    QString* error) {
    return parsePreparedLabel(readPreparedLabelText(text), format, imageSize, armors, error);
}

bool readLabelFileLenient(
    const QString& path, const QSize& imageSize, DataSet format, QVector<Armor>& armors,
    QStringList& lineErrors, QString* error) {
    armors.clear();
    lineErrors.clear();
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
        if (!parseLine(line.split(' '), imageSize, format, armor, lineError)) {
            lineErrors.push_back(QString("第 %1 行格式错误: %2").arg(lineNumber).arg(lineError));
            continue;
        }
        armors.push_back(armor);
    }
    return true;
}

bool readLabelTextLenient(
    const QString& text, const QSize& imageSize, DataSet format, QVector<Armor>& armors,
    QStringList& lineErrors, QString* error) {
    armors.clear();
    lineErrors.clear();
    const PreparedLabel prepared = readPreparedLabelText(text);
    if (!prepared.error.isEmpty()) {
        setError(error, prepared.error);
        return false;
    }
    for (const PreparedLine& line : prepared.lines) {
        Armor armor;
        QString lineError;
        if (!parseLine(line.fields, imageSize, format, armor, lineError)) {
            lineErrors.push_back(
                QString("第 %1 行格式错误: %2").arg(line.lineNumber).arg(lineError));
            continue;
        }
        armors.push_back(armor);
    }
    return true;
}

bool writeLabelText(
    QString& text, const QSize& imageSize, LabelOutputFormat format,
    const QVector<Armor>& armors, QString* error) {
    text.clear();
    if (imageSize.width() <= 0 || imageSize.height() <= 0) {
        setError(error, "图片尺寸无效");
        return false;
    }
    QString output;
    QTextStream stream(&output, QIODevice::WriteOnly);
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    stream.setRealNumberPrecision(6);

    for (int index = 0; index < armors.size(); ++index) {
        const Armor& armor = armors[index];
        const int colorId  = IdConvert::colorLetter2Id(armor.color);
        const int classId  = classIdFromToken(armor.cls);
        if (!validColor(colorId) || !validSize(armor.size) || !validClass(classId)) {
            setError(error, QString("第 %1 个标注的颜色/尺寸/类别无效").arg(index + 1));
            return false;
        }
        const auto points = normalizedPoints(armor, imageSize);

        if (format == LabelOutputFormat::LabelMasterV6) {
            const BoundingBox bbox =
                std::isfinite(armor.norm_x) && std::isfinite(armor.norm_y)
                        && std::isfinite(armor.norm_w) && std::isfinite(armor.norm_h)
                        && armor.norm_w >= 0.0 && armor.norm_h >= 0.0
                    ? BoundingBox{armor.norm_x, armor.norm_y, armor.norm_w, armor.norm_h}
                    : projectedBoundingBox(armor, imageSize);
            stream << colorId << ' ' << armor.size << ' ' << classId << ' ' << bbox.centerX << ' '
                   << bbox.centerY << ' ' << bbox.width << ' ' << bbox.height;
            for (int pointIndex = 0; pointIndex < 4; ++pointIndex) {
                const int visibility = armor.keypointVisibility[pointIndex];
                if (visibility < 0 || visibility > 2) {
                    setError(
                        error, QString("第 %1 个标注的第 %2 个关键点可见性无效")
                                   .arg(index + 1)
                                   .arg(pointIndex + 1));
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
            int encoded = 0;
            QString encodeError;
            if (!encodeV4Class(colorId, armor.size, classId, encoded, encodeError)) {
                setError(error, QString("第 %1 个标注无法写入 V4：%2").arg(index + 1).arg(encodeError));
                return false;
            }
            stream << encoded << ' ' << bbox.centerX << ' '
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
    text = output;
    return true;
}

bool writeLabelFile(
    const QString& path, const QSize& imageSize, LabelOutputFormat format,
    const QVector<Armor>& armors, QString* error) {
    QString text;
    if (!writeLabelText(text, imageSize, format, armors, error))
        return false;

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
    stream << text;
    stream.flush();
    if (!file.commit()) {
        setError(error, QString("无法原子替换标签: %1").arg(path));
        return false;
    }
    return true;
}

bool convertLabelFile(
    const QString& path, const QSize& imageSize, DataSet source, LabelOutputFormat target,
    QString* error) {
    QVector<Armor> armors;
    if (!readLabelFile(path, imageSize, source, armors, error))
        return false;
    return writeLabelFile(path, imageSize, target, armors, error);
}

} // namespace labelmaster::service::label_format
