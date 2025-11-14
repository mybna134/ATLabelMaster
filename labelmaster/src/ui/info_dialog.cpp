#include "info_dialog.h"
#include "ui_info_dialog.h"
#include <qdialog.h>
#include <qglobal.h>
#include <qguiapplication_platform.h>
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
    centerOn(parent);
}

InfoDialog::~InfoDialog() { delete this->ui; }
// 居中对齐
void InfoDialog::centerOn(QWidget* parent) {
    if (!parent) {
        parent = this->parentWidget();
    }
    if (parent) {
        auto hostRect = parent->rect();
        this->move(parent->mapToGlobal(QPoint(0, 0)) + hostRect.center() - this->rect().center());
        qDebug() << hostRect.topLeft();
    } else {
        QRect screenGeometry = QGuiApplication::screens()[0]->geometry();
        int x                = (screenGeometry.width() - this->width()) / 2;
        int y                = (screenGeometry.height() - this->height()) / 2;
        this->move(x, y);
    }
}
// 取消
void InfoDialog::reject() { this->done(1); }
// 确定
void InfoDialog::accept() {
    QString text = ui->comboBox->currentText();
    if (text == "Red") {
        text = "R";
    } else if (text == "Blue") {
        text = "B";
    } else if (text == "Purple") {
        text = "P";
    } else {
        text = "G";
    }
    emit InfoGetted(this->ui->lineEdit->text(), text, _isCurrent);
    this->done(1);
}
void InfoDialog::updateInfo(
    bool isCurrent, const QString& defaultClass, const QString& defaultColor) {
    QString Color;
    if (defaultColor == "R") {
        Color = "Red";
    } else if (defaultColor == "B") {
        Color = "Blue";
    } else if (defaultColor == "P") {
        Color = "Purple";
    } else {
        Color = "Gray";
    }
    _isCurrent = isCurrent;
    ui->comboBox->setCurrentText(Color);
    ui->lineEdit->setText(defaultClass);
}
