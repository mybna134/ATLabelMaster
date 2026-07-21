#include "settings_dialog.hpp"
#include "controller/settings.hpp"
#include "ui_settings_dialog.h"
#include "util/id_convert.hpp"
#include "ui/pixel_widgets/theme_manager.hpp"
#include <qcombobox.h>
#include <qdir.h>
#include <qfile.h>
#include <qfiledialog.h>
#include <qfileinfo.h>
#include <QFrame>
#include <QGridLayout>
#include <QLayoutItem>
#include <QScrollArea>
#include <qglobal.h>
#include <qnamespace.h>
#include <qmessagebox.h>
#include <qobject.h>
#include <qplaintextedit.h>
#include <qvariant.h>
#include <qwidget.h>
#include <QVBoxLayout>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QStringConverter>
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

void wrapSettingsInScrollArea(QDialog* dialog, Ui::SettingsDialog* ui) {
    if (!dialog || !ui)
        return;

    auto* rootLayout = qobject_cast<QVBoxLayout*>(dialog->layout());
    if (!rootLayout)
        return;

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

    rootLayout->addWidget(scrollArea, 1);
    rootLayout->addWidget(ui->buttonBox);
    dialog->setMinimumSize(480, 520);
    dialog->resize(560, 720);
}

} // namespace

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
    , ui_(new Ui::SettingsDialog) {
    ui_->setupUi(this);
    normalizeBehaviorGroup(ui_);
    wrapSettingsInScrollArea(this, ui_);
    this->setWindowTitle("Settings");
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
    int themeIndex = ui_->theme_combo->findData(currentTheme);
    if (themeIndex < 0) themeIndex = 0;
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
    connect(this->ui_->theme_combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::setTheme);

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
        info.colorId = -1;  // -1 表示匹配所有颜色
    } else {
        info.colorId = IdConvert::colorToken2Id(colorText);
    }

    // 大小: All(-1), Small(0), Big(1)
    QString sizeText = sizeCombo->currentText();
    if (sizeText == "All") {
        info.size = -1;  // -1 表示匹配所有大小
    } else {
        info.size = (sizeText == "Small") ? 0 : 1;
    }

    // 类别: G(0), 1-5(1-5), O(6), B(7)
    QString classText = classCombo->currentText();
    info.classId = IdConvert::classToken2Id(IdConvert::normalizeClasslToken(classText));

    return info;
}

void SettingsDialog::performBatchReplace() {
    // 获取源和目标标签
    LabelInfo src = getLabelFromCombos(
        ui_->src_color_combo, ui_->src_size_combo, ui_->src_class_combo);
    LabelInfo dst = getLabelFromCombos(
        ui_->dst_color_combo, ui_->dst_size_combo, ui_->dst_class_combo);

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
        QString absPath = QDir::cleanPath(currentDir + "/" + labelDir);
        dir = QDir(absPath);

        // 如果目录仍然不存在，尝试基于上次图片目录
        if (!dir.exists()) {
            QString lastImgDir = controller::AppSettings::instance().lastImageDir();
            if (!lastImgDir.isEmpty()) {
                QFileInfo imgFi(lastImgDir);
                if (imgFi.dir().exists()) {
                    absPath = QDir::cleanPath(imgFi.absolutePath() + "/../" + labelDir);
                    dir = QDir(absPath);
                }
            }
        }
    }

    if (!dir.exists()) {
        ui_->batch_result_text->setPlainText(
            "错误: 标签目录不存在: " + labelDir + "\n"
            "解析路径为: " + dir.absolutePath() + "\n"
            "请检查设置中的保存目录是否正确。");
        return;
    }

    // 获取所有 .txt 标签文件
    QStringList filters;
    filters << "*.txt";
    dir.setNameFilters(filters);
    QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::Readable);

    int totalFiles = 0;
    int modifiedFiles = 0;
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
        bool fileModified = false;
        int labelsReplacedInFile = 0;

        while (!in.atEnd()) {
            QString line = in.readLine();
            int hashIdx = line.indexOf('#');
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

            bool ok = true;
            int colorId = parts[0].toInt(&ok);
            int size = parts[1].toInt(&ok);
            int classId = parts[2].toInt(&ok);

            if (!ok) {
                newLines.append(line);
                continue;
            }

            // 检查是否匹配源标签 (-1 表示通配符，匹配任意值)
            bool colorMatch = (src.colorId == -1 || colorId == src.colorId);
            bool sizeMatch = (src.size == -1 || size == src.size);
            bool classMatch = (classId == src.classId);

            if (colorMatch && sizeMatch && classMatch) {
                // 替换为目标标签
                int dstColorId = (dst.colorId == -1) ? colorId : dst.colorId;
                int dstSize = (dst.size == -1) ? size : dst.size;

                // 根据原始格式输出
                if (parts.size() == 11) {
                    // 11字段格式: color size class x0 y0 x1 y1 x2 y2 x3 y3
                    line = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11")
                        .arg(dstColorId)
                        .arg(dstSize)
                        .arg(dst.classId)
                        .arg(parts[3]).arg(parts[4]).arg(parts[5]).arg(parts[6])
                        .arg(parts[7]).arg(parts[8]).arg(parts[9]).arg(parts[10]);
                } else {
                    // 15字段格式: color size class x y w h x0 y0 x1 y1 x2 y2 x3 y3
                    line = QString("%1 %2 %3 %4 %5 %6 %7 %8 %9 %10 %11 %12 %13 %14 %15")
                        .arg(dstColorId)
                        .arg(dstSize)
                        .arg(dst.classId)
                        .arg(parts[3]).arg(parts[4]).arg(parts[5]).arg(parts[6])  // x y w h
                        .arg(parts[7]).arg(parts[8]).arg(parts[9]).arg(parts[10]) // x0 y0 x1 y1
                        .arg(parts[11]).arg(parts[12]).arg(parts[13]).arg(parts[14]); // x2 y2 x3 y3
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
        + QString("标签目录: %1\n\n").arg(dir.absolutePath())
        + QString("替换规则:\n")
        + QString("  源: %1\n").arg(srcDesc)
        + QString("  目标: %1\n\n").arg(dstDesc)
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
