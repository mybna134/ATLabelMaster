
#include "ui_settings_dialog.h"
#include "util/keyboard_shortcuts.hpp"
#include <QVector>
#include <qcombobox.h>
#include <qdialog.h>
#include <qfiledialog.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qtmetamacros.h>
#include <qwidget.h>

class QLabel;
class QKeySequenceEdit;
class QTableWidget;
class QTabWidget;

namespace ui {
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() override;

private:
    Ui::SettingsDialog* ui_;
    struct ShortcutEditor {
        labelmaster::util::KeyboardAction action;
        QKeySequenceEdit* editor = nullptr;
    };
    QVector<ShortcutEditor> shortcutEditors_;
    QTableWidget* shortcutTable_   = nullptr;
    QLabel* shortcutConflictLabel_ = nullptr;

    struct LabelInfo {
        int colorId;
        int size;
        int classId;
    };
    LabelInfo getLabelFromCombos(
        QComboBox* colorCombo, QComboBox* sizeCombo, QComboBox* classCombo) const;
    void setupShortcutTab(QTabWidget* tabs);
    bool validateShortcutConflicts(QString* error = nullptr);
    void resetShortcutEditors();
private slots:
    void accept() override;
    void reject() override { QDialog::reject(); }
public slots:
    void SaveDirEditUpdate();                  // Label保存目录
    void LastImageDirEditUpdate();             // 上次图片目录
    void LastImagePathEditUpdate();
    void setLastImageDir();                    // 上次图片目录
    void setLastImagePath();                   // 上次图片路径
    void setSaveDir();                         // Label保存目录
    void setAutoSave(bool isAutoSave);         // 自动保存
    void setAutoEnhanceV(bool isAutoEnhanceV); // 自动增强
    void setVRate(int vRate);                  // 增强倍数
    void setFixedRoi(bool isFixedRoi);         // 设置固定ROI(decrepated)
    void setRoiH();                            // 设置ROI高度(decrepated)
    void setRoiW();                            // 设置ROI宽度(decrepated)
    void performBatchReplace();                // 批量替换标签
    void setTheme(int index);                  // 设置主题
    // void resotre();                            // 恢复默认值
};

} // namespace ui
