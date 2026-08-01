#pragma once

#include "dataset/dataset.h"
#include "types.hpp"
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

namespace labelmaster::service::label_format {

struct LabelFileSample {
    QString path;
    QString imagePath;
};

struct LabelFileIssue {
    LabelFileSample sample;
    QString error;
};

struct ParsedLabelCandidate {
    DataSet format = DataSet::Auto;
    QVector<Armor> armors;
};

// 批量检测阶段的解析缓存。坐标保存在归一化空间（等价于以 1x1 图片解析），
// 后续转换可以直接写出，不需要再次打开 label 或读取图片尺寸。
struct ParsedLabelFile {
    LabelFileSample sample;
    QVector<ParsedLabelCandidate> candidates;
    bool hasAnnotations = false;
    QString error;

    const QVector<Armor>* armorsFor(DataSet format) const;
};

struct FormatDetectionResult {
    DataSet format = DataSet::Auto;
    QVector<DataSet> candidates;
    bool hasAnnotations          = false;
    bool v1UpcChoiceRequired     = false;
    bool nineFieldChoiceRequired = false;
    QVector<LabelFileIssue> invalidSamples;
    QVector<ParsedLabelFile> parsedFiles;
    QString error;

    bool succeeded() const {
        return format != DataSet::Auto && !v1UpcChoiceRequired && !nineFieldChoiceRequired;
    }
};

QString outputFormatName(LabelOutputFormat format);
QString dataSetName(DataSet format);
FormatDetectionResult detectDataSetFormat(
    const QVector<LabelFileSample>& samples,
    const std::function<void(int current, int total)>& progress = {});

bool readLabelFile(
    const QString& path, const QSize& imageSize, DataSet format, QVector<Armor>& armors,
    QString* error = nullptr);

bool readLabelText(
    const QString& text, const QSize& imageSize, DataSet format, QVector<Armor>& armors,
    QString* error = nullptr);

// 冲突处理模式使用：保留能够解析的行，并报告无法解析的行。
bool readLabelFileLenient(
    const QString& path, const QSize& imageSize, DataSet format, QVector<Armor>& armors,
    QStringList& lineErrors, QString* error = nullptr);

// 文本框实时编辑使用：合法行立即反映到画布，错误行单独报告。
bool readLabelTextLenient(
    const QString& text, const QSize& imageSize, DataSet format, QVector<Armor>& armors,
    QStringList& lineErrors, QString* error = nullptr);

bool writeLabelText(
    QString& text, const QSize& imageSize, LabelOutputFormat format,
    const QVector<Armor>& armors, QString* error = nullptr);

bool writeLabelFile(
    const QString& path, const QSize& imageSize, LabelOutputFormat format,
    const QVector<Armor>& armors, QString* error = nullptr);

bool convertLabelFile(
    const QString& path, const QSize& imageSize, DataSet source, LabelOutputFormat target,
    QString* error = nullptr);

} // namespace labelmaster::service::label_format
