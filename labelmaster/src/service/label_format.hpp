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
};

struct FormatDetectionResult {
    DataSet format = DataSet::Auto;
    QVector<DataSet> candidates;
    PoseClassScheme poseClassScheme = PoseClassScheme::Classes36;
    bool hasAnnotations = false;
    QString error;

    bool succeeded() const { return format != DataSet::Auto; }
};

QString outputFormatName(LabelOutputFormat format);
QString dataSetName(DataSet format);
PoseClassScheme detectPoseClassScheme(const QStringList& labelPaths);
FormatDetectionResult detectDataSetFormat(
    const QVector<LabelFileSample>& samples,
    const std::function<void(int current, int total)>& progress = {});

bool readLabelFile(
    const QString& path, const QSize& imageSize, DataSet format, QVector<Armor>& armors,
    QString* error = nullptr, PoseClassScheme poseScheme = PoseClassScheme::Classes36);

bool writeLabelFile(
    const QString& path, const QSize& imageSize, LabelOutputFormat format,
    const QVector<Armor>& armors, QString* error = nullptr,
    PoseClassScheme poseScheme = PoseClassScheme::Classes36);

bool convertLabelFile(
    const QString& path, const QSize& imageSize, DataSet source, LabelOutputFormat target,
    QString* error = nullptr, PoseClassScheme poseScheme = PoseClassScheme::Classes36);

} // namespace labelmaster::service::label_format
