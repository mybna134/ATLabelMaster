#include "info_dialog.h"
#include "ui_info_dialog.h"
#include <algorithm>
#include <qcombobox.h>
#include <qdialog.h>
#include <qglobal.h>
#include <qguiapplication_platform.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpoint.h>
#include <qscreen.h>
#include <qtmetamacros.h>
#include <qtransform.h>
#include <qwidget.h>
using namespace ui;
InfoDialog::InfoDialog(QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::InfoDialog) {
    ui->setupUi(this);
    this->setWindowTitle("Edit Info");
};

InfoDialog::~InfoDialog() { delete this->ui; }
// 取消
void InfoDialog::reject() { this->done(1); }
// 确定
void InfoDialog::accept() {
    const auto visibilityValue = [](const QComboBox* combo) { return combo->currentIndex(); };
    emit InfoGetted(
        this->ui->classCombo->currentText(), ui->colorCombo->currentText().at(0),
        ui->sizeCombo->currentIndex(), visibilityValue(ui->vis0Combo),
        visibilityValue(ui->vis1Combo), visibilityValue(ui->vis2Combo),
        visibilityValue(ui->vis3Combo), _isCurrent);
    this->done(1);
}
void InfoDialog::updateInfo(
    bool isCurrent, const int& defaultClassId, const int& defaultColorId, const int& defaultSize,
    bool visibilitySupported, int vis0, int vis1, int vis2, int vis3) {
    _isCurrent = isCurrent;
    ui->colorCombo->setCurrentIndex(defaultColorId);
    ui->sizeCombo->setCurrentIndex(defaultSize);
    ui->classCombo->setCurrentIndex(defaultClassId);
    ui->visibilityGroup->setVisible(visibilitySupported);
    setFixedHeight(visibilitySupported ? 300 : 224);
    ui->buttonBox->move(ui->buttonBox->x(), visibilitySupported ? 250 : 170);
    ui->vis0Combo->setCurrentIndex(std::clamp(vis0, 0, 2));
    ui->vis1Combo->setCurrentIndex(std::clamp(vis1, 0, 2));
    ui->vis2Combo->setCurrentIndex(std::clamp(vis2, 0, 2));
    ui->vis3Combo->setCurrentIndex(std::clamp(vis3, 0, 2));
    connect(
        ui->classCombo, &QComboBox::currentIndexChanged, this, &InfoDialog::updateSize,
        Qt::UniqueConnection); // 接收到初始数据后再连接
}
void InfoDialog::updateSize(int index) {
    if (index != 1) {
        ui->sizeCombo->setCurrentIndex(0);
    } else {
        ui->sizeCombo->setCurrentIndex(1);
    }
}
