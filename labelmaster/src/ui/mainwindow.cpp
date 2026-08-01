#include "mainwindow.hpp"
#include "controller/settings.hpp"
#include "logger/core.hpp"
#include "service/format_help.hpp"
#include "service/label_format.hpp"
#include "ui/image_canvas.hpp"
#include "ui/settings_dialog.hpp"
#include "ui/stas_dialog.h"
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <qaction.h>
#include <qkeysequence.h>
#include <qmenu.h>
#include <qnamespace.h>
#include <qobject.h>

using ui::MainWindow;

#ifndef LABELMASTER_VERSION
#define LABELMASTER_VERSION "1.2.2"
#endif

namespace {

struct ShortcutRow {
    QString scope;
    QString action;
    QString shortcut;
};

QString displayActionText(const QAction* action) {
    if (!action)
        return {};
    QString text = action->text();
    text.remove(QLatin1Char('&'));
    return text;
}

QString displayShortcut(const QAction* action, const QString& emptyText) {
    if (!action)
        return emptyText;

    const QString text = action->shortcut().toString(QKeySequence::NativeText);
    return text.isEmpty() ? emptyText : text;
}

QString applicationVersionText() {
    const QString version = QCoreApplication::applicationVersion();
    return version.isEmpty() ? QStringLiteral(LABELMASTER_VERSION) : version;
}

QString findApplicationIconPath() {
    const QStringList candidates{
        controller::AppSettings::instance().assetsDir() + QStringLiteral("/icons/1.svg"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/icons/1.svg"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../assets/icons/1.svg"),
        QDir::currentPath() + QStringLiteral("/assets/icons/1.svg"),
        QStringLiteral("/usr/share/labelmaster/icons/1.svg"),
        QStringLiteral("/usr/local/share/labelmaster/icons/1.svg"),
        QStringLiteral("/usr/share/icons/hicolor/scalable/apps/labelmaster.svg"),
        QStringLiteral("/usr/local/share/icons/hicolor/scalable/apps/labelmaster.svg"),
    };

    for (const QString& path : candidates) {
        if (QFile::exists(path))
            return path;
    }
    return {};
}

QIcon applicationIcon(QWidget* widget) {
    QIcon icon = QIcon::fromTheme(QStringLiteral("labelmaster"));
    if (!icon.isNull())
        return icon;

    const QString path = findApplicationIconPath();
    if (!path.isEmpty()) {
        icon = QIcon(path);
        if (!icon.isNull())
            return icon;
    }

    return widget ? widget->style()->standardIcon(QStyle::SP_ComputerIcon) : QIcon{};
}

void addShortcutRow(QTableWidget* table, int row, const ShortcutRow& shortcut) {
    table->setItem(row, 0, new QTableWidgetItem(shortcut.scope));
    table->setItem(row, 1, new QTableWidgetItem(shortcut.action));
    table->setItem(row, 2, new QTableWidgetItem(shortcut.shortcut));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui_(std::make_unique<::Ui::MainWindow>()) {
    ui_->setupUi(this);
    setWindowTitle(QStringLiteral("ATLabelMaster"));
    setWindowIcon(applicationIcon(this));
    logger::Logger::instance().attachTextEdit(ui_->log_text);

    if (auto* log = ui_->log_text)
        log->setReadOnly(true);

    setupActions();
    wireButtonsToActions();

    // 文件树的"激活"事件（双击/回车等）
    // Note: activated信号已经涵盖了doubleClicked，所以不需要单独连接doubleClicked
    if (auto* tv = ui_->file_tree_view) {
        connect(tv, &QTreeView::activated, this, &MainWindow::sigFileActivated);
        tv->setSelectionBehavior(QAbstractItemView::SelectRows);
        tv->setUniformRowHeights(true);
    }

    // 标签内容编辑器：文本变化会立即解析并重绘画布。
    if (auto* edit = ui_->label_content_edit) {
        edit->setReadOnly(false);
        connect(edit, &QTextEdit::textChanged, this, [this] { applyLabelTextToCanvas(true); });
    }

    connect(
        ui_->label, &ImageCanvas::annotationsChanged, this,
        &MainWindow::updateLabelTextFromAnnotations);

    connect(this, &MainWindow::sigSaveRequested, this, [this] {
        if (!labelTextValid_) {
            setStatus(tr("标签文本仍有错误，未保存：%1").arg(labelTextError_), 5000);
            if (ui_->label_content_edit)
                ui_->label_content_edit->setFocus();
            return;
        }
        if (!updateLabelTextFromAnnotations({}))
            return;
        ui_->label->requestSave();
    });

    // 智能标注
    connect(this, &MainWindow::sigSmartAnnotateRequested, ui_->label, &ImageCanvas::requestDetect);
    connect(ui_->label, &ImageCanvas::shortcutFeedback, this, [this](const QString& message) {
        setStatus(message, 1500);
    });

    // 文件树等子控件有焦点时也让标注快捷键优先作用于画布。
    qApp->installEventFilter(this);

    statusBar()->showMessage(tr("Ready"), 3000);
}

MainWindow::~MainWindow() = default;

/* ---------------- 外部输入（更新 UI） ---------------- */
void MainWindow::showSettingDialog() {
    ui::SettingsDialog* dialog = new SettingsDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}
void MainWindow::showStasDialog() {
    ui::StasDialog* dialog = new ui::StasDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &ui::StasDialog::getStasRequested, this, &ui::MainWindow::sigGetStasRequested);
    connect(this, &ui::MainWindow::sigStasUpdateRequested, dialog, &ui::StasDialog::updateStasData);
    dialog->show();
}

void MainWindow::showHelpDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("帮助"));
    dialog.setWindowIcon(applicationIcon(this));
    dialog.resize(720, 520);

    auto* layout      = new QVBoxLayout(&dialog);
    auto* tabs        = new QTabWidget(&dialog);
    auto* shortcutTab = new QWidget(tabs);
    auto* shortcutLayout = new QVBoxLayout(shortcutTab);
    auto* title           = new QLabel(tr("当前快捷键设置"), shortcutTab);
    auto* table           = new QTableWidget(shortcutTab);

    const QString globalScope = tr("全局");
    const QString canvasScope = tr("画布");
    const QString empty       = tr("未设置");
    const QList<ShortcutRow> shortcuts{
        {globalScope, displayActionText(ui_->actionOpen), displayShortcut(ui_->actionOpen, empty)},
        {globalScope, displayActionText(ui_->actionSave), displayShortcut(ui_->actionSave, empty)},
        {globalScope, displayActionText(ui_->actionDelete), displayShortcut(ui_->actionDelete, empty)},
        {globalScope, displayActionText(ui_->actionPrev), displayShortcut(ui_->actionPrev, empty)},
        {globalScope, displayActionText(ui_->actionNext), displayShortcut(ui_->actionNext, empty)},
        {globalScope, displayActionText(ui_->actionHistEq), displayShortcut(ui_->actionHistEq, empty)},
        {globalScope, displayActionText(ui_->actionSmart), displayShortcut(ui_->actionSmart, empty)},
        {globalScope, displayActionText(ui_->actionSettings), displayShortcut(ui_->actionSettings, empty)},
        {globalScope, displayActionText(ui_->actionStas), displayShortcut(ui_->actionStas, empty)},
        {globalScope, displayActionText(ui_->actionFilter), displayShortcut(ui_->actionFilter, empty)},
        {globalScope, displayActionText(ui_->actionHelp), displayShortcut(ui_->actionHelp, empty)},
        {globalScope, displayActionText(ui_->actionAbout), displayShortcut(ui_->actionAbout, empty)},
        {canvasScope, tr("选择 Detector"), tr("W / A / S / D")},
        {canvasScope, tr("编辑选中 Detector"), tr("F2 / C / 双击")},
        {canvasScope, tr("设置颜色：Red / Gray / Blue / Purple"), tr("R / G / B / P")},
        {canvasScope, tr("设置类别：1-5 / Outpost / Base"), tr("1 / 2 / 3 / 4 / 5 / O / L")},
        {canvasScope, tr("设置大小：Big / Small"), tr("+ / -")},
        {canvasScope, tr("设置关键点可见性：左上 / 右上 / 左下 / 右下"), tr("J / K / N / M")},
        {canvasScope, tr("取消当前画布操作"), tr("Esc")},
    };

    table->setColumnCount(3);
    table->setRowCount(shortcuts.size());
    table->setHorizontalHeaderLabels({tr("范围"), tr("操作"), tr("快捷键")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    for (int row = 0; row < shortcuts.size(); ++row)
        addShortcutRow(table, row, shortcuts.at(row));

    shortcutLayout->addWidget(title);
    shortcutLayout->addWidget(table);
    tabs->addTab(shortcutTab, tr("快捷键"));

    auto* formatBrowser = new QTextBrowser(tabs);
    formatBrowser->setHtml(labelmaster::service::formatHelpHtml());
    tabs->addTab(formatBrowser, tr("标签格式"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addWidget(tabs);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::showAboutDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle(tr("关于"));
    dialog.setWindowIcon(applicationIcon(this));
    dialog.resize(420, 220);

    auto* layout = new QVBoxLayout(&dialog);
    auto* top    = new QHBoxLayout;

    auto* iconLabel = new QLabel(&dialog);
    iconLabel->setFixedSize(88, 88);
    iconLabel->setAlignment(Qt::AlignCenter);

    const QPixmap pixmap = applicationIcon(this).pixmap(72, 72);
    if (!pixmap.isNull())
        iconLabel->setPixmap(pixmap);

    auto* textLabel = new QLabel(&dialog);
    textLabel->setTextFormat(Qt::RichText);
    textLabel->setText(
        QStringLiteral("<b>ATLabelMaster</b><br>") + tr("版本：%1").arg(applicationVersionText())
        + QStringLiteral("<br>") + tr("Qt 版本：%1").arg(QString::fromLatin1(qVersion()))
        + QStringLiteral("<br>") + tr("RoboMaster 装甲板标注工具"));
    textLabel->setWordWrap(true);

    top->addWidget(iconLabel);
    top->addWidget(textLabel, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    layout->addLayout(top);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::showImage(const QImage& img) {
    ui_->label->setImage(img);
    ui_->label->setAlignment(Qt::AlignCenter);
}

void MainWindow::appendLog(const QString& line) {
    QString s = line;
    if (logTimestamp_) {
        const auto ts = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
        s             = QString("[%1] %2").arg(ts, line);
    }
    if (auto* te = ui_->log_text)
        te->append(s);
}

void MainWindow::setFileModel(QAbstractItemModel* model) {
    auto* tv = ui_->file_tree_view;
    if (!tv || !model)
        return;

    // 1) 先替换 model
    tv->setModel(model);

    // 2) 清空当前索引，防止悬空
    tv->setCurrentIndex(QModelIndex{});

    // 3) 重建并设置 SelectionModel（关键修复点）
    auto* sel = new QItemSelectionModel(model, tv);
    tv->setSelectionModel(sel);

    // 4) 重新连接“当前项变化” -> 对外发激活信号（你的 FileService 可据此刷新）
    connect(
        sel, &QItemSelectionModel::currentChanged, this,
        [this](const QModelIndex& cur, const QModelIndex&) {
            if (cur.isValid())
                emit sigFileActivated(cur);
        });

    // 5) 视图样式
    tv->header()->setStretchLastSection(false);
    tv->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < model->columnCount(); ++c)
        tv->setColumnHidden(c, true);
    tv->setTextElideMode(Qt::ElideNone);
    tv->setUniformRowHeights(true);

    // 通知 service：模型被替换了（便于它丢弃旧的持久索引）
    emit sigTreeModelReplaced(model);
}

void MainWindow::setCurrentIndex(const QModelIndex& idx) {
    if (auto* tv = ui_->file_tree_view) {
        tv->setCurrentIndex(idx);
        tv->scrollTo(idx);
    }
}

void MainWindow::setRoot(const QModelIndex& idx) {
    if (auto* tv = ui_->file_tree_view) {
        tv->setRootIndex(idx);
        tv->scrollTo(idx, QAbstractItemView::PositionAtTop);
    }
    emit sigTreeRootChanged(idx);
}

void MainWindow::setStatus(const QString& msg, int ms) { statusBar()->showMessage(msg, ms); }

void MainWindow::setBusy(bool on) {
    if (on)
        QApplication::setOverrideCursor(Qt::WaitCursor);
    else
        QApplication::restoreOverrideCursor();
}

void MainWindow::setUiEnabled(bool on) {
    if (auto* w = centralWidget())
        w->setEnabled(on);
}

void MainWindow::setLabelContent(const QString& content, DataSet format) {
    if (!ui_->label_content_edit)
        return;

    labelTextFormat_ = format == DataSet::Auto ? DataSet::LabelMasterV6 : format;
    {
        const QSignalBlocker blocker(ui_->label_content_edit);
        ui_->label_content_edit->setPlainText(content);
    }
    applyLabelTextToCanvas(false);
}

void MainWindow::applyLabelTextToCanvas(bool showStatus) {
    if (!ui_->label_content_edit || !ui_->label)
        return;

    const QString text = ui_->label_content_edit->toPlainText();
    const QSize imageSize = ui_->label->currentImage().size();
    if (!imageSize.isValid()) {
        ui_->label->loadDetections({});
        const QString error = text.trimmed().isEmpty() ? QString() : tr("请先打开图片");
        setLabelTextValidation(error.isEmpty(), error);
        if (showStatus && !error.isEmpty())
            setStatus(error, 3000);
        return;
    }

    QVector<Armor> armors;
    QStringList lineErrors;
    QString error;
    const bool readable = labelmaster::service::label_format::readLabelTextLenient(
        text, imageSize, labelTextFormat_, armors, lineErrors, &error);
    ui_->label->loadDetections(armors);

    if (!readable) {
        setLabelTextValidation(false, error);
    } else if (!lineErrors.isEmpty()) {
        setLabelTextValidation(false, lineErrors.front());
    } else {
        setLabelTextValidation(true);
    }

    if (!showStatus)
        return;
    if (labelTextValid_) {
        setStatus(tr("标签文本已应用，画布已重绘（%1 个 Detector）").arg(armors.size()), 1500);
    } else {
        setStatus(tr("标签文本未完整应用：%1").arg(labelTextError_), 4000);
    }
}

bool MainWindow::updateLabelTextFromAnnotations(const QVector<Armor>& armors) {
    if (!ui_->label_content_edit || !ui_->label)
        return false;

    const QVector<Armor>& currentArmors = armors.isEmpty() ? ui_->label->detections() : armors;
    QString text;
    QString error;
    if (!labelmaster::service::label_format::writeLabelText(
            text, ui_->label->currentImage().size(), LabelOutputFormat::LabelMasterV6,
            currentArmors, &error)) {
        setLabelTextValidation(false, error);
        setStatus(tr("无法同步标签文本：%1").arg(error), 4000);
        return false;
    }

    labelTextFormat_ = DataSet::LabelMasterV6;
    {
        const QSignalBlocker blocker(ui_->label_content_edit);
        ui_->label_content_edit->setPlainText(text);
    }
    setLabelTextValidation(true);
    return true;
}

void MainWindow::setLabelTextValidation(bool valid, const QString& error) {
    labelTextValid_ = valid;
    labelTextError_ = valid ? QString() : error;
    if (ui_->label_content_edit) {
        ui_->label_content_edit->setToolTip(
            valid ? tr("可直接编辑标签；合法内容会实时同步到画布") : labelTextError_);
    }
}

void MainWindow::setConflictMode(bool enabled, int remaining) {
    if (ui_->merge_conflict_button)
        ui_->merge_conflict_button->setVisible(enabled);
    setWindowTitle(
        enabled ? tr("ATLabelMaster - 冲突处理（剩余 %1）").arg(remaining)
                : QStringLiteral("ATLabelMaster"));
}

/* ---------------- 配置/事件 ---------------- */
void MainWindow::enableDragDrop(bool on) {
    dragDropEnabled_ = on;
    setAcceptDrops(on);
}

void MainWindow::setLogTimestampEnabled(bool on) { logTimestamp_ = on; }

bool MainWindow::textInputHasFocus() const {
    QWidget* w = QApplication::focusWidget();
    return w
        && (w->inherits("QLineEdit") || w->inherits("QTextEdit") || w->inherits("QPlainTextEdit"));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress && QApplication::activeWindow() == this
        && !textInputHasFocus()) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat() && ui_->label
            && ui_->label->handleEditorShortcut(keyEvent->key(), keyEvent->modifiers())) {
            keyEvent->accept();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    if (textInputHasFocus()) {
        QMainWindow::keyPressEvent(e);
        return;
    }
    if (e->isAutoRepeat()) {
        e->ignore();
        return;
    }

    if (ui_->label && ui_->label->handleEditorShortcut(e->key(), e->modifiers())) {
        e->accept();
        return;
    }

    switch (e->key()) {
    case Qt::Key_Q:
        emit sigPrevRequested();
        e->accept();
        return;
    case Qt::Key_E:
        emit sigNextRequested();
        e->accept();
        return;
    case Qt::Key_H:
        emit sigHistEqRequested();
        e->accept();
        return;
    case Qt::Key_Delete:
        emit sigDeleteRequested();
        e->accept();
        return;
    case Qt::Key_Space:
        emit sigSmartAnnotateRequested();
        e->accept();
        return;
    case Qt::Key_F1:
        emit sigSettingsRequested();
        e->accept();
        return;
    default: emit sigKeyCommand(QKeySequence(e->key()).toString()); break;
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (!dragDropEnabled_) {
        e->ignore();
        return;
    }
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
    else
        e->ignore();
}

void MainWindow::dropEvent(QDropEvent* e) {
    if (!dragDropEnabled_) {
        e->ignore();
        return;
    }
    QStringList paths;
    for (const QUrl& url : e->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    if (!paths.isEmpty())
        emit sigDroppedPaths(paths);
    e->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent* e) { e->accept(); }

/* ---------------- 装配 ---------------- */
static QAction* ensureAction(QAction* act, const QKeySequence& ks, const QString& tip) {
    if (!act)
        return nullptr;
    if (!ks.isEmpty())
        act->setShortcut(ks);
    if (!tip.isEmpty())
        act->setToolTip(tip);
    return act;
}

void MainWindow::setupActions() {
    ensureAction(ui_->actionOpen, QKeySequence::Open, tr("Open Folder"));
    ensureAction(ui_->actionSave, QKeySequence::Save, tr("Save Labels"));
    ensureAction(ui_->actionPrev, QKeySequence(Qt::Key_Q), tr("Previous (Q)"));
    ensureAction(ui_->actionNext, QKeySequence(Qt::Key_E), tr("Next (E)"));
    ensureAction(ui_->actionHistEq, QKeySequence(Qt::Key_H), tr("Histogram Equalize (H)"));
    ensureAction(ui_->actionDelete, QKeySequence::Delete, tr("Delete"));
    ensureAction(ui_->actionSmart, QKeySequence(Qt::Key_Space), tr("Smart Annotate (Space)"));
    ensureAction(ui_->actionSettings, {}, tr("Settings"));
    ensureAction(ui_->actionStas, QKeySequence(Qt::Key_F1), tr("Get STAS."));
    ensureAction(ui_->actionFilter, {}, tr("按 Label 类型筛选图片"));
    ensureAction(ui_->actionHelp, {}, tr("查看当前快捷键设置"));
    ensureAction(ui_->actionAbout, {}, tr("查看当前版本信息"));

    connect(ui_->actionOpen, &QAction::triggered, this, &MainWindow::sigOpenFolderRequested);
    connect(ui_->actionSave, &QAction::triggered, this, &MainWindow::sigSaveRequested);
    connect(ui_->actionPrev, &QAction::triggered, this, &MainWindow::sigPrevRequested);
    connect(ui_->actionNext, &QAction::triggered, this, &MainWindow::sigNextRequested);
    connect(ui_->actionHistEq, &QAction::triggered, this, &MainWindow::sigHistEqRequested);
    connect(ui_->actionDelete, &QAction::triggered, this, &MainWindow::sigDeleteRequested);
    connect(ui_->actionSmart, &QAction::triggered, this, &MainWindow::sigSmartAnnotateRequested);
    connect(ui_->actionStas, &QAction::triggered, this, &MainWindow::showStasDialog);
    connect(ui_->actionSettings, &QAction::triggered, this, &MainWindow::sigSettingsRequested);
    connect(ui_->actionFilter, &QAction::triggered, this, &MainWindow::sigFilterRequested);
    connect(ui_->actionHelp, &QAction::triggered, this, &MainWindow::showHelpDialog);
    connect(ui_->actionAbout, &QAction::triggered, this, &MainWindow::showAboutDialog);
}

void MainWindow::wireButtonsToActions() {
    connect(ui_->open_folder_button, &QPushButton::clicked, ui_->actionOpen, &QAction::trigger);
    connect(ui_->smart_button, &QPushButton::clicked, ui_->actionSmart, &QAction::trigger);
    connect(ui_->prev_pic, &QPushButton::clicked, ui_->actionPrev, &QAction::trigger);
    connect(ui_->next_pic, &QPushButton::clicked, ui_->actionNext, &QAction::trigger);
    connect(ui_->histogram_button, &QPushButton::clicked, ui_->actionHistEq, &QAction::trigger);
    connect(ui_->delete_button, &QPushButton::clicked, ui_->actionDelete, &QAction::trigger);
    connect(ui_->save_button, &QPushButton::clicked, ui_->actionSave, &QAction::trigger);
    connect(ui_->setttings_button, &QPushButton::clicked, ui_->actionSettings, &QAction::trigger);
    connect(
        ui_->merge_conflict_button, &QPushButton::clicked, this,
        &MainWindow::sigForceMergeConflictRequested);
}
