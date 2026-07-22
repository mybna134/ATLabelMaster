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
    QSize imageSize;
    QString imagePath;
};

struct LabelFileIssue {
    LabelFileSample sample;
    QString error;
};

struct FormatDetectionResult {
    DataSet format = DataSet::Auto;
    QVector<DataSet> candidates;
    bool hasAnnotations          = false;
    bool v1UpcChoiceRequired     = false;
    bool nineFieldChoiceRequired = false;
    QVector<LabelFileIssue> invalidSamples;
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

// 冲突处理模式使用：保留能够解析的行，并报告无法解析的行。
bool readLabelFileLenient(
    const QString& path, const QSize& imageSize, DataSet format, QVector<Armor>& armors,
    QStringList& lineErrors, QString* error = nullptr);

bool writeLabelFile(
    const QString& path, const QSize& imageSize, LabelOutputFormat format,
    const QVector<Armor>& armors, QString* error = nullptr);

bool convertLabelFile(
    const QString& path, const QSize& imageSize, DataSet source, LabelOutputFormat target,
    QString* error = nullptr);

} // namespace labelmaster::service::label_format
