#include "mainwindow.hpp"
#include "logger/core.hpp"
#include "ui/image_canvas.hpp"
#include "ui/settings_dialog.hpp"
#include "ui/stas_dialog.h"
#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QHeaderView>
#include <QImage>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPixmap>
#include <QTextEdit>
#include <QTreeView>
#include <QUrl>
#include <qaction.h>
#include <qkeysequence.h>
#include <qmenu.h>
#include <qnamespace.h>
#include <qobject.h>

using ui::MainWindow;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , ui_(std::make_unique<::Ui::MainWindow>()) {
    ui_->setupUi(this);
    setWindowTitle(QStringLiteral("ATLabelMaster"));
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

    // 标签内容查看器：初始化为只读
    if (auto* edit = ui_->label_content_edit) {
        edit->setReadOnly(true);
    }

    // 连接标签内容更新信号
    connect(this, &MainWindow::sigLabelContentChanged, this, [this](const QString& content) {
        if (ui_->label_content_edit)
            ui_->label_content_edit->setPlainText(content);
    });

    connect(this, &MainWindow::sigSaveRequested, ui_->label, &ImageCanvas::requestSave);

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

void MainWindow::setLabelContent(const QString& content) {
    if (ui_->label_content_edit)
        ui_->label_content_edit->setPlainText(content);
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
