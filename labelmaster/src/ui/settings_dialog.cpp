#include "settings_dialog.hpp"
#include "controller/settings.hpp"
#include "ui/pixel_widgets/theme_manager.hpp"
#include "ui_settings_dialog.h"
#include "util/id_convert.hpp"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLayoutItem>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <qcombobox.h>
#include <qdir.h>
#include <qfile.h>
#include <qfiledialog.h>
#include <qfileinfo.h>
#include <qglobal.h>
#include <qmessagebox.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qplaintextedit.h>
#include <qvariant.h>
#include <qwidget.h>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
# include <QStringConverter>
#endif
#include <QTextStream>
using namespace ui;
using labelmaster::ui::ThemeManager;

namespace {
void normalizeBehaviorGroup(Ui::SettingsDialog* ui) {
    if (!ui || !ui->groupBox || ui->groupBox->layout())
        return;

    if (ui->horizontalLayout) {
        ui->horizontalLayout->removeWidget(ui->auto_save_checkbox);
        ui->horizontalLayout->removeWidget(ui->auto_enhance_checkbox);
    }
    if (ui->horizontalLayoutWidget)
        ui->horizontalLayoutWidget->hide();

    ui->auto_save_checkbox->setParent(ui->groupBox);
    ui->auto_enhance_checkbox->setParent(ui->groupBox);

    auto* behaviorLayout = new QGridLayout(ui->groupBox);
    behaviorLayout->setObjectName(QStringLiteral("gridBehavior"));
    behaviorLayout->setContentsMargins(12, 18, 12, 12);
    behaviorLayout->setHorizontalSpacing(10);
    behaviorLayout->setVerticalSpacing(10);
    behaviorLayout->setColumnStretch(1, 1);

    behaviorLayout->addWidget(ui->v_rate_tip_label, 0, 0);
    behaviorLayout->addWidget(ui->v_rate_slider, 0, 1);
    behaviorLayout->addWidget(ui->v_rate_label, 0, 2);
    behaviorLayout->addWidget(ui->auto_save_checkbox, 1, 0);
    behaviorLayout->addWidget(ui->auto_enhance_checkbox, 1, 1, 1, 2);
}

QTabWidget* setupSettingsTabs(QDialog* dialog, Ui::SettingsDialog* ui) {
    if (!dialog || !ui)
        return nullptr;

    auto* rootLayout = qobject_cast<QVBoxLayout*>(dialog->layout());
    if (!rootLayout)
        return nullptr;

    auto* scrollContent = new QWidget(dialog);
    auto* contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(rootLayout->spacing());

    QLayoutItem* item = nullptr;
    while ((item = rootLayout->takeAt(0)) != nullptr) {
        QWidget* widget = item->widget();
        if (widget == ui->buttonBox) {
            delete item;
            continue;
        }
        if (widget) {
            widget->setParent(scrollContent);
            contentLayout->addWidget(widget);
            delete item;
            continue;
        }

        contentLayout->addItem(item);
    }

    contentLayout->addStretch(1);

    auto* scrollArea = new QScrollArea(dialog);
    scrollArea->setObjectName(QStringLiteral("settings_scroll_area"));
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(scrollContent);

    auto* tabs = new QTabWidget(dialog);
    tabs->setObjectName(QStringLiteral("settings_tabs"));
    tabs->addTab(scrollArea, QObject::tr("常规"));

    rootLayout->addWidget(tabs, 1);
    rootLayout->addWidget(ui->buttonBox);
    dialog->setMinimumSize(620, 560);
    dialog->resize(760, 720);
    return tabs;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , ui_(new Ui::SettingsDialog) {
    ui_->setupUi(this);
    normalizeBehaviorGroup(ui_);
    QTabWidget* tabs = setupSettingsTabs(this, ui_);
    setupShortcutTab(tabs);
    this->setWindowTitle(tr("设置"));
    this->ui_->dataset_dir_edit->setText(controller::AppSettings::instance().saveDir());
    this->ui_->last_img_dir_edit->setText(controller::AppSettings::instance().lastImageDir());
    this->ui_->last_img_path_edit->setText(controller::AppSettings::instance().lastImagePath());
    this->ui_->auto_save_checkbox->setChecked(controller::AppSettings::instance().autoSave());
    this->ui_->auto_enhance_checkbox->setChecked(
        controller::AppSettings::instance().autoEnhanceV());
    this->ui_->v_rate_slider->setValue(controller::AppSettings::instance().vRate() * 10);
    this->ui_->v_rate_label->setText(
        QString::number(controller::AppSettings::instance().vRate(), 'f', 1));
    this->ui_->fix_roi_checkbox->setChecked(controller::AppSettings::instance().fixedRoi());
    this->ui_->roi_h_spin->setValue(controller::AppSettings::instance().roiH());
    this->ui_->roi_w_spin->setValue(controller::AppSettings::instance().roiW());

    // Initialize theme combo with available themes
    QStringList themes = ThemeManager::instance().availableThemes();
    ui_->theme_combo->clear();

    // Add themes with display names
    for (const QString& themeId : themes) {
        QString displayName = ThemeManager::instance().themeDisplayName(themeId);
        ui_->theme_combo->addItem(displayName, themeId);
    }

    // Set current theme
    QString currentTheme = controller::AppSettings::instance().theme();
    int themeIndex       = ui_->theme_combo->findData(currentTheme);
    if (themeIndex < 0)
        themeIndex = 0;
    ui_->theme_combo->setCurrentIndex(themeIndex);
    update();
    connect(
        this->ui_->dataset_dir_edit, &QLineEdit::editingFinished, this,
        &SettingsDialog::setSaveDir);
    connect(
        this->ui_->last_img_dir_edit, &QLineEdit::editingFinished, this,
        &SettingsDialog::setLastImageDir);
    connect(
        this->ui_->last_img_path_edit, &QLineEdit::editingFinished, this,
        &SettingsDialog::setLastImagePath);
    connect(this->ui_->auto_save_checkbox, &QCheckBox::toggled, this, &SettingsDialog::setAutoSave);
    connect(
        this->ui_->auto_enhance_checkbox, &QCheckBox::toggled, this,
        &SettingsDialog::setAutoEnhanceV);
    connect(this->ui_->v_rate_slider, &QSlider::valueChanged, this, &SettingsDialog::setVRate);
    connect(this->ui_->fix_roi_checkbox, &QCheckBox::toggled, this, &SettingsDialog::setFixedRoi);
    connect(this->ui_->roi_h_spin, &QSpinBox::editingFinished, this, &SettingsDialog::setRoiH);
    connect(this->ui_->roi_w_spin, &QSpinBox::editingFinished, this, &SettingsDialog::setRoiW);
    connect(
        this->ui_->theme_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
        &SettingsDialog::setTheme);
}

SettingsDialog::~SettingsDialog() { delete ui_; }

void SettingsDialog::setupShortcutTab(QTabWidget* tabs) {
    if (!tabs)
        return;

    auto* page   = new QWidget(tabs);
    auto* layout = new QVBoxLayout(page);
    auto* description = new QLabel(
        tr("单击快捷键框后按下新键位；留空可禁用该操作。所有键位必须唯一。\n"
           "四点可见性键：单按在可见/不可见间切换，连续双按设为不在范围内。"),
        page);
    description->setWordWrap(true);
    layout->addWidget(description);

    shortcutTable_       = new QTableWidget(page);
    const auto& keyboard = labelmaster::util::KeyboardManager::instance();
    const QList<labelmaster::util::KeyboardAction> actions = keyboard.allActions();
    shortcutTable_->setColumnCount(3);
    shortcutTable_->setRowCount(actions.size());
    shortcutTable_->setHorizontalHeaderLabels({tr("范围"), tr("操作"), tr("键位")});
    shortcutTable_->setAlternatingRowColors(true);
    shortcutTable_->setSelectionMode(QAbstractItemView::NoSelection);
    shortcutTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    shortcutTable_->verticalHeader()->setVisible(false);
    shortcutTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    shortcutTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    shortcutTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    shortcutEditors_.reserve(actions.size());
    for (int row = 0; row < actions.size(); ++row) {
        const auto action = actions.at(row);
        auto* scopeItem   = new QTableWidgetItem(keyboard.scopeName(action));
        auto* actionItem  = new QTableWidgetItem(keyboard.actionName(action));
        scopeItem->setFlags(scopeItem->flags() & ~Qt::ItemIsEditable);
        actionItem->setFlags(actionItem->flags() & ~Qt::ItemIsEditable);
        shortcutTable_->setItem(row, 0, scopeItem);
        shortcutTable_->setItem(row, 1, actionItem);

        auto* editorContainer = new QWidget(shortcutTable_);
        auto* editorLayout    = new QHBoxLayout(editorContainer);
        editorLayout->setContentsMargins(0, 0, 0, 0);
        editorLayout->setSpacing(4);
        auto* editor = new QKeySequenceEdit(keyboard.shortcut(action), editorContainer);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
        editor->setMaximumSequenceLength(1);
#endif
        editor->setToolTip(tr("按下新键位，或使用右侧按钮清除"));
        auto* clearButton = new QPushButton(QStringLiteral("×"), editorContainer);
        clearButton->setFixedWidth(28);
        clearButton->setToolTip(tr("清除此键位"));
        connect(clearButton, &QPushButton::clicked, editor, &QKeySequenceEdit::clear);
        editorLayout->addWidget(editor, 1);
        editorLayout->addWidget(clearButton);
        shortcutTable_->setCellWidget(row, 2, editorContainer);
        shortcutEditors_.append({action, editor});
        connect(editor, &QKeySequenceEdit::keySequenceChanged, this, [this] {
            validateShortcutConflicts();
        });
    }

    auto* footer           = new QHBoxLayout;
    shortcutConflictLabel_ = new QLabel(page);
    shortcutConflictLabel_->setWordWrap(true);
    auto* resetButton = new QPushButton(tr("恢复默认键位"), page);
    connect(resetButton, &QPushButton::clicked, this, &SettingsDialog::resetShortcutEditors);
    footer->addWidget(shortcutConflictLabel_, 1);
    footer->addWidget(resetButton);

    layout->addWidget(shortcutTable_, 1);
    layout->addLayout(footer);
    tabs->addTab(page, tr("键位设置"));
    validateShortcutConflicts();
}

bool SettingsDialog::validateShortcutConflicts(QString* error) {
    QHash<QString, QVector<int>> rowsBySequence;
    for (const ShortcutEditor& row : shortcutEditors_)
        row.editor->setStyleSheet(QString());

    for (int row = 0; row < shortcutEditors_.size(); ++row) {
        QKeySequenceEdit* editor    = shortcutEditors_[row].editor;
        const QKeySequence sequence = editor->keySequence();
        if (sequence.isEmpty())
            continue;
        if (sequence.count() != 1) {
            const QString message = tr("每个操作只能设置一个按键或组合键");
            if (error)
                *error = message;
            editor->setStyleSheet(
                QStringLiteral("QKeySequenceEdit { border: 2px solid #d9534f; }"));
            if (shortcutConflictLabel_) {
                shortcutConflictLabel_->setText(message);
                shortcutConflictLabel_->setStyleSheet(QStringLiteral("color: #d9534f;"));
            }
            if (auto* ok = ui_->buttonBox->button(QDialogButtonBox::Ok))
                ok->setEnabled(false);
            return false;
        }
        rowsBySequence[labelmaster::util::KeyboardManager::shortcutIdentity(sequence)].append(row);
    }

    QStringList conflicts;
    for (auto it = rowsBySequence.cbegin(); it != rowsBySequence.cend(); ++it) {
        if (it.value().size() < 2)
            continue;
        QStringList actionNames;
        for (int row : it.value()) {
            shortcutEditors_[row].editor->setStyleSheet(
                QStringLiteral("QKeySequenceEdit { border: 2px solid #d9534f; }"));
            actionNames.append(
                labelmaster::util::KeyboardManager::instance().actionName(
                    shortcutEditors_[row].action));
        }
        const QKeySequence sequence =
            QKeySequence::fromString(it.key(), QKeySequence::PortableText);
        conflicts.append(tr("%1：%2").arg(
            sequence.toString(QKeySequence::NativeText), actionNames.join(tr("、"))));
    }

    const bool valid = conflicts.isEmpty();
    if (shortcutConflictLabel_) {
        shortcutConflictLabel_->setText(
            valid ? tr("未检测到键位冲突") : tr("键位冲突：%1").arg(conflicts.join(tr("；"))));
        shortcutConflictLabel_->setStyleSheet(
            valid ? QStringLiteral("color: #4c9a2a;") : QStringLiteral("color: #d9534f;"));
    }
    if (auto* ok = ui_->buttonBox->button(QDialogButtonBox::Ok))
        ok->setEnabled(valid);
    if (!valid && error)
        *error = tr("存在重复键位：%1").arg(conflicts.join(tr("；")));
    return valid;
}

void SettingsDialog::resetShortcutEditors() {
    const auto& keyboard = labelmaster::util::KeyboardManager::instance();
    for (const ShortcutEditor& row : shortcutEditors_)
        row.editor->setKeySequence(keyboard.defaultShortcut(row.action));
    validateShortcutConflicts();
}

void SettingsDialog::accept() {
    QString error;
    if (!validateShortcutConflicts(&error)) {
        QMessageBox::warning(this, tr("键位冲突"), error);
        return;
    }

    QHash<labelmaster::util::KeyboardAction, QKeySequence> shortcuts;
    for (const ShortcutEditor& row : shortcutEditors_)
        shortcuts.insert(row.action, row.editor->keySequence());

    auto& keyboard = labelmaster::util::KeyboardManager::instance();
    if (!keyboard.setShortcuts(shortcuts, &error)) {
        QMessageBox::warning(this, tr("无法保存键位"), error);
        return;
    }
    keyboard.save();
    QDialog::accept();
}
void SettingsDialog::SaveDirEditUpdate() {
    const QString dir = QFileDialog::getExistingDirectory(
        nullptr, tr("选择图片文件夹"), QString(),
        QFileDialog::Options(QFileDialog::DontUseNativeDialog | QFileDialog::ShowDirsOnly));
    if (!dir.isEmpty()) {
        this->ui_->dataset_dir_edit->setText(dir);
        setSaveDir();
    }
}
void SettingsDialog::LastImageDirEditUpdate() {
    const QString dir = QFileDialog::getExistingDirectory(
        nullptr, tr("选择图片文件夹"), QString(),
        QFileDialog::Options(QFileDialog::DontUseNativeDialog | QFileDialog::ShowDirsOnly));
    if (!dir.isEmpty()) {
        this->ui_->last_img_dir_edit->setText(dir);
        setLastImageDir();
    }
}
void SettingsDialog::LastImagePathEditUpdate() {
    const QString str = QFileDialog::getOpenFileName();
    if (!str.isEmpty()) {
        this->ui_->last_img_path_edit->setText(str);
        setLastImagePath();
    }
}
void SettingsDialog::setLastImageDir() {
    controller::AppSettings::instance().setlastImageDir(this->ui_->last_img_dir_edit->text());
    update();
}
void SettingsDialog::setLastImagePath() {
    controller::AppSettings::instance().setlastImagePath(this->ui_->last_img_path_edit->text());
    update();
}
void SettingsDialog::setSaveDir() {
    controller::AppSettings::instance().setsaveDir(this->ui_->dataset_dir_edit->text());
    update();
}
void SettingsDialog::setAutoSave(bool isAutoSave) {
    controller::AppSettings::instance().setautoSave(isAutoSave);
    update();
}
void SettingsDialog::setAutoEnhanceV(bool isAutoEnhanceV) {
    controller::AppSettings::instance().setautoEnhanceV(isAutoEnhanceV);
    update();
}
void SettingsDialog::setVRate(int vRate) {
    controller::AppSettings::instance().setvRate(static_cast<float>(vRate) / 10);
    this->ui_->v_rate_label->setText(
        QString::number(controller::AppSettings::instance().vRate(), 'f', 1));
    update();
}
void SettingsDialog::setFixedRoi(bool isFixedRoi) {
    controller::AppSettings::instance().setfixedRoi(isFixedRoi);
    update();
}
void SettingsDialog::setRoiH() {
    controller::AppSettings::instance().setroiH(this->ui_->roi_h_spin->value());
    update();
}
void SettingsDialog::setRoiW() {
    controller::AppSettings::instance().setroiW(this->ui_->roi_w_spin->value());
    update();
}

// ---------- 批量替换功能 ----------
SettingsDialog::LabelInfo SettingsDialog::getLabelFromCombos(
    QComboBox* colorCombo, QComboBox* sizeCombo, QComboBox* classCombo) const {
    LabelInfo info;

    // 颜色: All(-1), Blue(0), Red(1), Gray(2), Purple(3)
    QString colorText = colorCombo->currentText();
    if (colorText == "All") {
        info.colorId = -1; // -1 表示匹配所有颜色
    } else {
        info.colorId = IdConvert::colorToken2Id(colorText);
    }

    // 大小: All(-1), Small(0), Big(1)
    QString sizeText = sizeCombo->currentText();
    if (sizeText == "All") {
        info.size = -1; // -1 表示匹配所有大小
    } else {
        info.size = (sizeText == "Small") ? 0 : 1;
    }

    // 类别: G(0), 1-5(1-5), O(6), B(7)
    QString classText = classCombo->currentText();
    info.classId      = IdConvert::classToken2Id(IdConvert::normalizeClasslToken(classText));

    return info;
}

void SettingsDialog::performBatchReplace() {
    // 获取源和目标标签
    LabelInfo src =
        getLabelFromCombos(ui_->src_color_combo, ui_->src_size_combo, ui_->src_class_combo);
    LabelInfo dst =
        getLabelFromCombos(ui_->dst_color_combo, ui_->dst_size_combo, ui_->dst_class_combo);

    // 获取标签保存目录
    QString labelDir = controller::AppSettings::instance().saveDir();
    if (labelDir.isEmpty()) {
        ui_->batch_result_text->setPlainText("错误: 请先设置标签保存目录");
        return;
    }

    // 处理相对路径 - 将相对路径转换为绝对路径
    QDir dir(labelDir);
    if (!dir.isAbsolute()) {
        // 如果是相对路径，尝试基于当前目录转换
        QString currentDir = QDir::currentPath();
        QString absPath    = QDir::cleanPath(currentDir + "/" + labelDir);
        dir                = QDir(absPath);

        // 如果目录仍然不存在，尝试基于上次图片目录
        if (!dir.exists()) {
            QString lastImgDir = controller::AppSettings::instance().lastImageDir();
            if (!lastImgDir.isEmpty()) {
                QFileInfo imgFi(lastImgDir);
                if (imgFi.dir().exists()) {
                    absPath = QDir::cleanPath(imgFi.absolutePath() + "/../" + labelDir);
                    dir     = QDir(absPath);
                }
            }
        }
    }

    if (!dir.exists()) {
        ui_->batch_result_text->setPlainText(
            "错误: 标签目录不存在: " + labelDir
            + "\n"
              "解析路径为: "
            + dir.absolutePath()
            + "\n"
              "请检查设置中的保存目录是否正确。");
        return;
    }

    // 获取所有 .txt 标签文件
    QStringList filters;
    filters << "*.txt";
    dir.setNameFilters(filters);
    QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::Readable);

    int totalFiles     = 0;
    int modifiedFiles  = 0;
    int replacedLabels = 0;
    QStringList modifiedFileNames;

    for (const QFileInfo& fileInfo : fileList) {
        QString filePath = fileInfo.absoluteFilePath();
        QFile file(filePath);

        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }

        totalFiles++;
        QTextStream in(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        in.setEncoding(QStringConverter::Utf8);
#else
        in.setCodec("UTF-8");
#endif

        QStringList newLines;
        bool fileModified        = false;
        int labelsReplacedInFile = 0;

        while (!in.atEnd()) {
            QString line = in.readLine();
            int hashIdx  = line.indexOf('#');
            if (hashIdx >= 0) {
                // Keep comments as-is
                if (hashIdx == 0) {
                    newLines.append(line);
                    continue;
                }
                line = line.left(hashIdx);
            }

            QString trimmed = line.trimmed();
            if (trimmed.isEmpty()) {
                newLines.append(line);
                continue;
            }

            // Parse: 支持11字段和15字段格式
            // 11字段: color size class x0 y0 x1 y1 x2 y2 x3 y3
            // 15字段: color size class x y w h x0 y0 x1 y1 x2 y2 x3 y3
            QStringList parts = trimmed.simplified().split(' ');
            if (parts.size() != 11 && parts.size() != 15) {
                newLines.append(line);
                continue;
            }

            bool ok     = true;
            int colorId = parts[0].toInt(&ok);
            int size    = parts[1].toInt(&ok);
            int classId = parts[2].toInt(&ok);

            if (!ok) {
                newLines.append(line);
                continue;
            }

            // 检查是否匹配源标签 (-1 表示通配符，匹配任意值)
            bool colorMatch = (src.colorId == -1 || colorId == src.colorId);
            bool sizeMatch  = (src.size == -1 || size == src.size);
            bool classMatch = (classId == src.classId);

            if (colorMatch && sizeMatch && classMatch) {
                // 替换为目标标签
                int dstColorId = (dst.colorId == -1) ? colorId : dst.colorId;
                int dstSize    = (dst.size == -1) ? size : dst.size;

                // 根据原始格式输出
                if (parts.size() == 11) {
                    // 11字段格式: color size class x0 y0 x1 y1 x2 y2 x3 y3
                    line = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11")
                               .arg(dstColorId)
                               .arg(dstSize)
                               .arg(dst.classId)
                               .arg(parts[3])
                               .arg(parts[4])
                               .arg(parts[5])
                               .arg(parts[6])
                               .arg(parts[7])
                               .arg(parts[8])
                               .arg(parts[9])
                               .arg(parts[10]);
                } else {
                    // 15字段格式: color size class x y w h x0 y0 x1 y1 x2 y2 x3 y3
                    line = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15")
                               .arg(dstColorId)
                               .arg(dstSize)
                               .arg(dst.classId)
                               .arg(parts[3])
                               .arg(parts[4])
                               .arg(parts[5])
                               .arg(parts[6])   // x y w h
                               .arg(parts[7])
                               .arg(parts[8])
                               .arg(parts[9])
                               .arg(parts[10])  // x0 y0 x1 y1
                               .arg(parts[11])
                               .arg(parts[12])
                               .arg(parts[13])
                               .arg(parts[14]); // x2 y2 x3 y3
                }
                fileModified = true;
                labelsReplacedInFile++;
                replacedLabels++;
            }

            newLines.append(line);
        }

        // file destructor will close automatically, but explicit close is safe

        // 如果文件被修改，写回
        if (fileModified) {
            QFile writeFile(filePath);
            if (writeFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream out(&writeFile);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                out.setEncoding(QStringConverter::Utf8);
#else
                out.setCodec("UTF-8");
#endif
                for (const QString& newLine : newLines) {
                    out << newLine << '\n';
                }
                // writeFile destructor closes automatically
                modifiedFiles++;
                modifiedFileNames.append(fileInfo.fileName());
            } else {
                qWarning() << "Failed to open file for writing:" << filePath;
            }
        }
    }

    // 生成统计报告
    QString srcDesc = QString("颜色=%1, 大小=%2, 类别=%3")
                          .arg(ui_->src_color_combo->currentText())
                          .arg(ui_->src_size_combo->currentText())
                          .arg(ui_->src_class_combo->currentText());
    QString dstDesc = QString("颜色=%1, 大小=%2, 类别=%3")
                          .arg(ui_->dst_color_combo->currentText())
                          .arg(ui_->dst_size_combo->currentText())
                          .arg(ui_->dst_class_combo->currentText());

    QString report = QString("=== 批量替换统计 ===\n\n")
                   + QString("标签目录: %1\n\n").arg(dir.absolutePath()) + QString("替换规则:\n")
                   + QString("  源: %1\n").arg(srcDesc) + QString("  目标: %1\n\n").arg(dstDesc)
                   + QString("扫描文件数: %1\n").arg(totalFiles)
                   + QString("修改文件数: %1\n").arg(modifiedFiles)
                   + QString("替换标签数: %1\n\n").arg(replacedLabels);

    if (!modifiedFileNames.isEmpty() && modifiedFileNames.size() <= 50) {
        report += "已修改的文件:\n";
        for (const QString& name : modifiedFileNames) {
            report += "  - " + name + "\n";
        }
    } else if (modifiedFileNames.size() > 50) {
        report += QString("已修改的文件: %1 个 (列表已省略)\n").arg(modifiedFileNames.size());
    }

    ui_->batch_result_text->setPlainText(report);
}

void SettingsDialog::setTheme(int index) {
    QVariant themeData = ui_->theme_combo->itemData(index);
    if (!themeData.isValid()) {
        return; // Invalid selection
    }

    QString themeId = themeData.toString();
    controller::AppSettings::instance().settheme(themeId);

    // Apply theme immediately (hot reload)
    ThemeManager::instance().loadTheme(themeId);

    update();
}
