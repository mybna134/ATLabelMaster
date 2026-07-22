#include "filter_dialog.hpp"

#include "image_canvas.hpp"
#include "service/label_format.hpp"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <array>

namespace ui {
namespace {

constexpr int kColorRole = Qt::UserRole;
constexpr int kSizeRole  = Qt::UserRole + 1;
constexpr int kClassRole = Qt::UserRole + 2;

bool moveFileWithoutOverwrite(const QString& source, const QString& destination, QString& error) {
    if (!QFile::exists(source)) {
        error = QObject::tr("源文件不存在：%1").arg(source);
        return false;
    }
    if (QFile::exists(destination)) {
        error = QObject::tr("目标文件已存在：%1").arg(destination);
        return false;
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        error = QObject::tr("无法创建目录：%1").arg(QFileInfo(destination).absolutePath());
        return false;
    }
    if (QFile::rename(source, destination))
        return true;
    if (!QFile::copy(source, destination)) {
        error = QObject::tr("移动失败：%1 → %2").arg(source, destination);
        return false;
    }
    if (!QFile::remove(source)) {
        QFile::remove(destination);
        error = QObject::tr("无法删除移动后的源文件：%1").arg(source);
        return false;
    }
    return true;
}

void removeEmptyDirectoryTree(const QString& path) {
    QDir dir(path);
    if (!dir.exists())
        return;
    const QStringList children =
        dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString& child : children)
        removeEmptyDirectoryTree(dir.filePath(child));
    QDir().rmdir(path); // 只删除空目录，意外文件会被保留。
}

QString colorName(int id) {
    switch (id) {
    case 0: return QObject::tr("蓝色 B");
    case 1: return QObject::tr("红色 R");
    case 2: return QObject::tr("灰色 G");
    case 3: return QObject::tr("紫色 P");
    default: return QString::number(id);
    }
}

} // namespace

QString filterCombinationKey(int colorId, int sizeId, int classId) {
    return QStringLiteral("%1:%2:%3").arg(colorId).arg(sizeId).arg(classId);
}

bool saveFilterManifest(
    const QString& filteringRoot, const QVector<FilterReviewEntry>& entries, QString* error) {
    if (!QDir().mkpath(filteringRoot)) {
        if (error)
            *error = QObject::tr("无法创建 filtering 目录：%1").arg(filteringRoot);
        return false;
    }

    QJsonArray serializedEntries;
    for (const FilterReviewEntry& entry : entries) {
        QJsonObject object;
        object.insert(QStringLiteral("filteredImagePath"), entry.filteredImagePath);
        object.insert(QStringLiteral("filteredLabelPath"), entry.filteredLabelPath);
        object.insert(QStringLiteral("originalImagePath"), entry.originalImagePath);
        object.insert(QStringLiteral("originalLabelPath"), entry.originalLabelPath);
        serializedEntries.push_back(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), serializedEntries);

    QSaveFile file(QDir(filteringRoot).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QObject::tr("无法写入 filtering 清单：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = QObject::tr("无法提交 filtering 清单：%1").arg(file.errorString());
        return false;
    }
    return true;
}

bool loadFilterManifest(
    const QString& filteringRoot, QVector<FilterReviewEntry>& entries, QString* error) {
    entries.clear();
    QFile file(QDir(filteringRoot).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QObject::tr("无法读取 filtering 清单：%1").arg(file.errorString());
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error)
            *error = QObject::tr("filtering 清单损坏：%1").arg(parseError.errorString());
        return false;
    }
    for (const QJsonValue& value : document.object().value(QStringLiteral("entries")).toArray()) {
        const QJsonObject object = value.toObject();
        FilterReviewEntry entry;
        entry.filteredImagePath = object.value(QStringLiteral("filteredImagePath")).toString();
        entry.filteredLabelPath = object.value(QStringLiteral("filteredLabelPath")).toString();
        entry.originalImagePath = object.value(QStringLiteral("originalImagePath")).toString();
        entry.originalLabelPath = object.value(QStringLiteral("originalLabelPath")).toString();
        if (!entry.filteredImagePath.isEmpty() && QFile::exists(entry.filteredImagePath))
            entries.push_back(entry);
    }
    return true;
}

FilterSelectionDialog::FilterSelectionDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("选择不需要的 Label 类型"));
    resize(520, 680);

    auto* layout = new QVBoxLayout(this);
    auto* tip    = new QLabel(
        tr("勾选需要筛出的精确组合。父级复选框可批量选择某个颜色或尺寸下的全部类别。"), this);
    tip->setWordWrap(true);
    layout->addWidget(tip);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderLabel(tr("不需要的 color / size / class 组合"));
    const std::array<QString, 8> classes{
        QStringLiteral("G"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"),
        QStringLiteral("4"), QStringLiteral("5"), QStringLiteral("O"), QStringLiteral("B")};
    for (int colorId = 0; colorId < 4; ++colorId) {
        auto* colorItem = new QTreeWidgetItem(tree_, {colorName(colorId)});
        colorItem->setFlags(colorItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
        colorItem->setCheckState(0, Qt::Unchecked);
        for (int sizeId = 0; sizeId < 2; ++sizeId) {
            auto* sizeItem =
                new QTreeWidgetItem(colorItem, {sizeId == 0 ? tr("小装甲") : tr("大装甲")});
            sizeItem->setFlags(
                sizeItem->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
            sizeItem->setCheckState(0, Qt::Unchecked);
            for (int classId = 0; classId < int(classes.size()); ++classId) {
                auto* classItem = new QTreeWidgetItem(sizeItem, {classes[classId]});
                classItem->setFlags(classItem->flags() | Qt::ItemIsUserCheckable);
                classItem->setCheckState(0, Qt::Unchecked);
                classItem->setData(0, kColorRole, colorId);
                classItem->setData(0, kSizeRole, sizeId);
                classItem->setData(0, kClassRole, classId);
            }
        }
    }
    tree_->expandToDepth(1);
    layout->addWidget(tree_, 1);

    auto* selectionButtons = new QHBoxLayout;
    auto* selectAllButton  = new QPushButton(tr("全选"), this);
    auto* clearButton      = new QPushButton(tr("清空"), this);
    selectionButtons->addWidget(selectAllButton);
    selectionButtons->addWidget(clearButton);
    selectionButtons->addStretch();
    layout->addLayout(selectionButtons);
    connect(selectAllButton, &QPushButton::clicked, this, [this] { setAllChecked(true); });
    connect(clearButton, &QPushButton::clicked, this, [this] { setAllChecked(false); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("开始筛选"));
    connect(buttons, &QDialogButtonBox::accepted, this, &FilterSelectionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &FilterSelectionDialog::reject);
    layout->addWidget(buttons);
}

QStringList FilterSelectionDialog::selectedKeys() const {
    QStringList result;
    for (int colorIndex = 0; colorIndex < tree_->topLevelItemCount(); ++colorIndex) {
        const QTreeWidgetItem* colorItem = tree_->topLevelItem(colorIndex);
        for (int sizeIndex = 0; sizeIndex < colorItem->childCount(); ++sizeIndex) {
            const QTreeWidgetItem* sizeItem = colorItem->child(sizeIndex);
            for (int classIndex = 0; classIndex < sizeItem->childCount(); ++classIndex) {
                const QTreeWidgetItem* classItem = sizeItem->child(classIndex);
                if (classItem->checkState(0) != Qt::Checked)
                    continue;
                result.push_back(filterCombinationKey(
                    classItem->data(0, kColorRole).toInt(), classItem->data(0, kSizeRole).toInt(),
                    classItem->data(0, kClassRole).toInt()));
            }
        }
    }
    return result;
}

void FilterSelectionDialog::accept() {
    if (selectedKeys().isEmpty()) {
        QMessageBox::information(this, tr("未选择类型"), tr("请至少选择一个 Label 组合。"));
        return;
    }
    QDialog::accept();
}

void FilterSelectionDialog::setAllChecked(bool checked) {
    for (int index = 0; index < tree_->topLevelItemCount(); ++index)
        tree_->topLevelItem(index)->setCheckState(0, checked ? Qt::Checked : Qt::Unchecked);
}

FilterReviewDialog::FilterReviewDialog(
    QString filteringRoot, QVector<FilterReviewEntry> entries, QWidget* parent)
    : QDialog(parent)
    , filteringRoot_(std::move(filteringRoot))
    , entries_(std::move(entries)) {
    setWindowTitle(tr("筛选模式"));
    resize(1180, 820);

    auto* layout  = new QVBoxLayout(this);
    summaryLabel_ = new QLabel(this);
    layout->addWidget(summaryLabel_);
    canvas_ = new ImageCanvas(this);
    canvas_->setMinimumSize(800, 560);
    canvas_->setAlignment(Qt::AlignCenter);
    layout->addWidget(canvas_, 1);

    auto* buttons     = new QHBoxLayout;
    brightnessButton_ = new QPushButton(tr("提升亮度"), this);
    restoreButton_    = new QPushButton(tr("恢复"), this);
    deleteButton_     = new QPushButton(tr("删除"), this);
    auto* closeButton = new QPushButton(tr("暂时关闭"), this);
    brightnessButton_->setCheckable(true);
    buttons->addWidget(brightnessButton_);
    buttons->addStretch();
    buttons->addWidget(restoreButton_);
    buttons->addWidget(deleteButton_);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    connect(brightnessButton_, &QPushButton::clicked, canvas_, &ImageCanvas::histEqualize);
    connect(brightnessButton_, &QPushButton::toggled, this, [this](bool enhanced) {
        brightnessButton_->setText(enhanced ? tr("恢复亮度") : tr("提升亮度"));
    });
    connect(restoreButton_, &QPushButton::clicked, this, &FilterReviewDialog::restoreCurrent);
    connect(deleteButton_, &QPushButton::clicked, this, &FilterReviewDialog::deleteCurrent);
    connect(closeButton, &QPushButton::clicked, this, &FilterReviewDialog::reject);

    loadCurrent();
}

void FilterReviewDialog::loadCurrent() {
    if (entries_.isEmpty()) {
        summaryLabel_->setText(tr("筛选目录已清空，正在返回原模式…"));
        restoreButton_->setEnabled(false);
        deleteButton_->setEnabled(false);
        brightnessButton_->setEnabled(false);
        QFile::remove(QDir(filteringRoot_).filePath(QStringLiteral("manifest.json")));
        removeEmptyDirectoryTree(QDir(filteringRoot_).filePath(QStringLiteral("images")));
        removeEmptyDirectoryTree(QDir(filteringRoot_).filePath(QStringLiteral("labels")));
        removeEmptyDirectoryTree(filteringRoot_);
        QTimer::singleShot(0, this, &QDialog::accept);
        return;
    }

    const FilterReviewEntry& entry = entries_.front();
    QImageReader reader(entry.filteredImagePath);
    reader.setAutoTransform(true);
    const QImage image = reader.read();
    if (image.isNull()) {
        showOperationError(tr("图片读取失败"), reader.errorString());
        return;
    }

    canvas_->setImage(image);
    brightnessButton_->setChecked(canvas_->brightnessEnhanced());
    QVector<Armor> armors;
    QString error;
    if (!labelmaster::service::label_format::readLabelFile(
            entry.filteredLabelPath, image.size(), DataSet::LabelMasterV6, armors, &error)) {
        armors.clear();
        summaryLabel_->setText(tr("剩余 %1 张｜%2｜标签读取失败：%3")
                                   .arg(entries_.size())
                                   .arg(QFileInfo(entry.filteredImagePath).fileName(), error));
    } else {
        summaryLabel_->setText(tr("剩余 %1 张｜%2｜检测框 %3 个")
                                   .arg(entries_.size())
                                   .arg(QFileInfo(entry.filteredImagePath).fileName())
                                   .arg(armors.size()));
    }
    canvas_->setDetections(armors);
}

void FilterReviewDialog::restoreCurrent() {
    if (entries_.isEmpty())
        return;
    const FilterReviewEntry entry = entries_.front();
    QString error;
    if (!moveFileWithoutOverwrite(entry.filteredLabelPath, entry.originalLabelPath, error)) {
        showOperationError(tr("恢复失败"), error);
        return;
    }
    if (!moveFileWithoutOverwrite(entry.filteredImagePath, entry.originalImagePath, error)) {
        QString rollbackError;
        moveFileWithoutOverwrite(entry.originalLabelPath, entry.filteredLabelPath, rollbackError);
        showOperationError(tr("恢复失败"), error);
        return;
    }
    advanceAfterCurrentRemoved();
}

void FilterReviewDialog::deleteCurrent() {
    if (entries_.isEmpty())
        return;
    const FilterReviewEntry entry = entries_.front();
    if (QFile::exists(entry.filteredLabelPath) && !QFile::remove(entry.filteredLabelPath)) {
        showOperationError(tr("删除失败"), tr("无法删除标签：%1").arg(entry.filteredLabelPath));
        return;
    }
    if (QFile::exists(entry.filteredImagePath) && !QFile::remove(entry.filteredImagePath)) {
        showOperationError(tr("删除失败"), tr("无法删除图片：%1").arg(entry.filteredImagePath));
        return;
    }
    advanceAfterCurrentRemoved();
}

void FilterReviewDialog::advanceAfterCurrentRemoved() {
    entries_.removeFirst();
    QString error;
    if (!entries_.isEmpty() && !saveFilterManifest(filteringRoot_, entries_, &error))
        QMessageBox::warning(this, tr("清单更新失败"), error);
    loadCurrent();
}

void FilterReviewDialog::showOperationError(const QString& title, const QString& error) {
    QMessageBox::critical(this, title, error);
}

} // namespace ui
