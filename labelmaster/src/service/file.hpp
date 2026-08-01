// ===============================
// File: service/file.hpp
// ===============================
#pragma once
#include "../dataset/dataset.h"
#include "types.hpp"    // Armor 定义
#include <QModelIndex>
#include <QObject>
#include <QPersistentModelIndex>
#include <QPointer>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QVector>
#include <qobject.h>

class QAbstractItemModel;
class QFileSystemModel;
class QProgressDialog;
class QSortFilterProxyModel;
class QImage;

namespace labelmaster::service::label_format {
struct LabelFileSample;
}

class FileService : public QObject {
    Q_OBJECT
public:
    explicit FileService(QObject* parent = nullptr);
    ~FileService() override;

    void exposeModel(); // 把 proxy 模型抛给 UI

public slots:
    // === 打开 ===
    void openFolderDialog(const DataSet& type = DataSet::Auto); // 弹框选目录
    void openPaths(const QStringList&);                         // 拖拽/命令行路径
    void openIndex(const QModelIndex&);                         // 由文件树激活
    void startFiltering();                                      // 按标签组合筛选并复核

    // === 浏览 ===
    void next(bool allowAutoSave = true);
    void prev();

    // === 修改 ===
    void deleteCurrent(); // 直接删除当前文件（简单实现）

    // === 保存标注 ===
    void saveData(const QVector<Armor>& armors, const QImage& image, bool needSaveImg);
    void forceMergeCurrentConflict();

    // === 获取统计信息 ==
    void getStas(int colorId, int classId, int sizeId);

signals:
    // === 给 UI 的输出 ===
    void modelReady(QAbstractItemModel* proxyModel);
    void rootChanged(const QModelIndex& proxyRoot);
    void currentIndexChanged(const QModelIndex& proxyIndex);
    void imageReady(const QImage& img);
    void status(const QString& msg, int ms = 3000);
    void busy(bool on);
    void conflictModeChanged(bool enabled, int remaining);

    // === 打开图片时加载到的标注 ===
    void labelsLoaded(const QVector<Armor>& armors);
    void labelTextChanged(const QString& labelText, DataSet format); // 标签文件内容及其当前格式
    // ===统计信息获取==
    void StasGetted(const int& targetCount, const int& fileCount);
    // ===自动保存===
    void saveRequested();

private:
    // 目录加载完成后再尝试选第一张
    void selectFirst(const QString& path);
    bool openDir(const QString& dir);
    void showDirectoryLoadProgress();
    void closeDirectoryLoadProgress();
    bool openFileAt(const QModelIndex& proxyIndex);
    void startPendingImport();
    bool tryImportPendingDataSet(const QStringList& imagePaths);
    void offerToRemoveUnusedLabels(const QStringList& imagePaths);
    bool collectLoadedImagePaths(const QString& dir, QStringList& imagePaths);
    void tryOpenFirstAfterLoaded(const QString& dir);
    QModelIndex findFirstImageUnder(const QModelIndex& proxyRoot) const;
    QModelIndex mapFromProxyToSource(const QModelIndex&) const;
    QModelIndex mapFromSourceToProxy(const QModelIndex&) const;
    bool isImageFile(const QString& path) const;
    bool maybeEnterExistingStage(const QString& imageDir);
    void finishConflictModeIfEmpty();
    QString dataSetRootForImageDir(const QString& imageDir) const;
    QString stageLabelForImage(const QString& stageImagePath) const;
    bool tryAutoResolveConflict(
        const QString& stageImagePath, bool& resolved, bool& converted, QString& error);
    bool restoreConflict(const QString& stageImagePath, bool force, QString& error);
    bool restoreCurrentConflict(bool force, QString& error);
    bool loadStageManifest();
    bool saveStageManifest(QString* error = nullptr) const;
    void refreshConflictDirectory();

    // 记忆 & 恢复
    void saveLastVisited(const QString& imagePath);
    void tryRestoreLastVisited(); // 异步调用
    bool setProxyRoot(const QString& dir);

    // 标注 I/O（归一化支持）
    static QString labelFileForImage(const QString& imagePath);
    static bool writeLabelFile(
        const QString& labelPath, const QVector<Armor>& armors,
        const QSize& imgSize);               // 保存为归一化
    static QVector<Armor> readLabelFile(
        const QString& labelPath, const QSize& imgSize,
        DataSet format);                     // 按当前数据集格式读取并反归一化

private:
    struct StageEntry {
        QString stageImagePath;
        QString stageLabelPath;
        QString originalImagePath;
        QString originalLabelPath;
        QString error;
        DataSet sourceFormat = DataSet::Auto;
    };

    QString pendingDir_;
    QString pendingTargetPath_;
    QFileSystemModel* fsModel_    = nullptr; // 源模型
    QSortFilterProxyModel* proxy_ = nullptr; // 只显示图片与目录
    QPointer<QProgressDialog> directoryLoadProgress_;
    QPersistentModelIndex proxyRoot_;
    QPersistentModelIndex proxyCurrent_;
    QString currentImagePath_;               // 当前图片绝对路径
    QSize currentImageSize_;                 // 当前图片尺寸（归一化需要）
    DataSet currentDataSet         = DataSet::Auto;
    bool formatDetectionAttempted_ = false;
    bool formatDetectionFinished_  = false;
    bool pendingImportScheduled_    = false;
    int pendingImageCount_         = -1;
    QSet<QString> pendingDirectoryLoads_;
    bool conflictMode_             = false;
    QString originalImageDir_;
    QString stageRoot_;
    QString stageImagesDir_;
    QString stageLabelsDir_;
    QVector<StageEntry> stageEntries_;
};
