#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

class ImageCanvas;
class QLabel;
class QPushButton;
class QTreeWidget;

namespace ui {

struct FilterReviewEntry {
    QString filteredImagePath;
    QString filteredLabelPath;
    QString originalImagePath;
    QString originalLabelPath;
};

QString filterCombinationKey(int colorId, int sizeId, int classId);
bool saveFilterManifest(
    const QString& filteringRoot, const QVector<FilterReviewEntry>& entries,
    QString* error = nullptr);
bool loadFilterManifest(
    const QString& filteringRoot, QVector<FilterReviewEntry>& entries, QString* error = nullptr);

class FilterSelectionDialog final : public QDialog {
public:
    explicit FilterSelectionDialog(QWidget* parent = nullptr);

    QStringList selectedKeys() const;

protected:
    void accept() override;

private:
    void setAllChecked(bool checked);

    QTreeWidget* tree_ = nullptr;
};

class FilterReviewDialog final : public QDialog {
public:
    FilterReviewDialog(
        QString filteringRoot, QVector<FilterReviewEntry> entries, QWidget* parent = nullptr);

private:
    void loadCurrent();
    void restoreCurrent();
    void deleteCurrent();
    void advanceAfterCurrentRemoved();
    void showOperationError(const QString& title, const QString& error);

    QString filteringRoot_;
    QVector<FilterReviewEntry> entries_;
    ImageCanvas* canvas_           = nullptr;
    QLabel* summaryLabel_          = nullptr;
    QPushButton* brightnessButton_ = nullptr;
    QPushButton* restoreButton_    = nullptr;
    QPushButton* deleteButton_     = nullptr;
};

} // namespace ui
