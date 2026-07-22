// ===============================
// File: service/file.cpp
// ===============================
#include "service/file.hpp"
#include "../util/id_convert.hpp"
#include "../util/svg_constants.hpp"
#include "service/label_format.hpp"
#include "types.hpp"
#include <QApplication>
#include <QBuffer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QQueue>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <cstdio>
#include <qabstractitemmodel.h>
#include <qbuffer.h>
#include <qcontiguouscache.h>
#include <qdebug.h>
#include <qdir.h>
#include <qfiledialog.h>
#include <qfileinfo.h>
#include <qglobal.h>
#include <qhashfunctions.h>
#include <qimage.h>
#include <qiodevicebase.h>
#include <qlist.h>
#include <qmath.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpointer.h>
#include <qsettings.h>
#include <qsharedpointer.h>
#include <qsortfilterproxymodel.h>
#include <qstringalgorithms.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <strings.h>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
# include <QStringConverter> // Qt6: QTextStream::setEncoding
#endif
#include <QEventLoop>
#include <QTextBrowser>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWhatsThis>

#include <algorithm>
#include <cmath>
#include <limits>

#include "../ui/filter_dialog.hpp"
#include "../ui/stas_dialog.h"
#include "../util/string.hpp"
#include "controller/settings.hpp"
#include "logger/core.hpp"

namespace {
static const QStringList kImgExt = {"*.png", "*.jpg", "*.jpeg", "*.bmp",
                                    "*.gif", "*.tif", "*.tiff", "*.webp"};

class ImageFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

protected:
    bool filterAcceptsRow(int srcRow, const QModelIndex& srcParent) const override {
        const auto idx = sourceModel()->index(srcRow, 0, srcParent);
        if (!idx.isValid())
            return false;

        const auto* fsm = qobject_cast<const QFileSystemModel*>(sourceModel());
        if (!fsm)
            return true;

        if (fsm->isDir(idx))
            return true;     // 保留目录
        const QString name = fsm->fileName(idx).toLower();
        for (const auto& pat : kImgExt) {
            if (name.endsWith(pat.mid(1)))
                return true; // endsWith(".png")
        }
        return false;
    }
};

bool directoryContainsImage(const QString& dirPath) {
    QDirIterator it(dirPath, kImgExt, QDir::Files, QDirIterator::Subdirectories);
    return it.hasNext();
}

bool chooseV1OrUpcFormat(QWidget* parent, DataSet& format) {
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QObject::tr("选择 10 字段标签格式"));
    box.setText(QObject::tr("这些标签同时符合 LabelMaster V1 和 UPC 格式。"));
    box.setInformativeText(QObject::tr("请选择数据集实际使用的格式；取消后不会转换任何标签。"));
    auto* v1Button  = box.addButton(QObject::tr("LabelMaster V1"), QMessageBox::AcceptRole);
    auto* upcButton = box.addButton(QObject::tr("UPC"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == v1Button) {
        format = DataSet::LabelMaster;
        return true;
    }
    if (box.clickedButton() == upcButton) {
        format = DataSet::UPC;
        return true;
    }
    return false;
}

bool chooseNineFieldFormat(QWidget* parent, DataSet& format) {
    QMessageBox box(parent);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QObject::tr("选择 9 字段类别编码"));
    box.setText(QObject::tr("9 字段标签的 class_id 全部位于 0～38，无法自动区分格式。"));
    box.setInformativeText(
        QObject::tr("请选择该数据集使用 UnionSecret 格式还是 NWPU 格式。"));
    auto* unionSecretButton =
        box.addButton(QObject::tr("UnionSecret 格式"), QMessageBox::AcceptRole);
    auto* nwpuButton = box.addButton(QObject::tr("NWPU 格式"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == unionSecretButton) {
        format = DataSet::UnionSecret;
        return true;
    }
    if (box.clickedButton() == nwpuButton) {
        format = DataSet::NWPU;
        return true;
    }
    return false;
}

bool copyFileReplacing(
    const QString& source, const QString& destination, bool replace, QString& error) {
    if (!QFile::exists(source)) {
        error = QObject::tr("源文件不存在：%1").arg(source);
        return false;
    }
    if (!QDir().mkpath(QFileInfo(destination).absolutePath())) {
        error = QObject::tr("无法创建目录：%1").arg(QFileInfo(destination).absolutePath());
        return false;
    }
    if (QFile::exists(destination)) {
        if (!replace) {
            error = QObject::tr("目标文件已存在：%1").arg(destination);
            return false;
        }
        if (!QFile::remove(destination)) {
            error = QObject::tr("无法替换目标文件：%1").arg(destination);
            return false;
        }
    }
    if (!QFile::copy(source, destination)) {
        error = QObject::tr("复制失败：%1 → %2").arg(source, destination);
        return false;
    }
    return true;
}

bool moveFileWithoutOverwrite(const QString& source, const QString& destination, QString& error) {
    if (!QFile::exists(source))
        return true;
    if (QFile::exists(destination)) {
        error = QObject::tr("目标目录中已存在同名文件：%1").arg(destination);
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

QString imageDirectoryFromParent(const QString& parentPath) {
    const QDir parent(parentPath);
    const QStringList imageDirNames = {
        QStringLiteral("images"),
        QStringLiteral("image"),
        QStringLiteral("imgs"),
        QStringLiteral("img"),
    };

    QString firstExisting;
    for (const QString& name : imageDirNames) {
        const QString candidate = QDir::cleanPath(parent.filePath(name));
        if (!QFileInfo(candidate).isDir())
            continue;
        if (firstExisting.isEmpty())
            firstExisting = candidate;
        if (directoryContainsImage(candidate))
            return candidate;
    }

    return firstExisting.isEmpty() ? QDir::cleanPath(parentPath) : firstExisting;
}

QString formatHelpHtml() {
    return QStringLiteral(
        "<h2>支持的标签格式</h2>"
        "<p>坐标均为归一化值；bbox 和关键点均允许越过图像边界，<code>pts</code> 的点序为 "
        "TL、BL、BR、TR。</p>"
        "<table cellspacing='8'>"
        "<tr><th align='left'>格式</th><th align='left'>字段布局</th><th "
        "align='left'>自动处理</th></tr>"
        "<tr><td>V1（10）</td><td><code>color class pts[4]</code></td><td>转 V6；与 UPC "
        "并列时手动选择</td></tr>"
        "<tr><td>V2（11）</td><td><code>color size class pts[4]</code></td><td>转 V6</td></tr>"
        "<tr><td>V3（15）</td><td><code>color size class bbox pts[4]</code></td><td>转 V6</td></tr>"
        "<tr><td>V4（13，36 类）</td><td><code>combined-class bbox pts[4]</code></td><td>转 "
        "V6</td></tr>"
        "<tr><td>V5（17，14 类）</td><td><code>class_id bbox (x y visibility)[4]</code></td><td>转 "
        "V6</td></tr>"
        "<tr><td>V6（19）</td><td><code>color size class bbox (x y "
        "visibility)[4]</code></td><td>直接打开</td></tr>"
        "<tr><td>HITSZ（10）</td><td><code>pts[4] class color</code></td><td>转 V6</td></tr>"
        "<tr><td>UPC（10）</td><td><code>color class pts[4]</code></td><td>转 V6；类别 0～7 "
        "时可能与 V1 冲突</td></tr>"
        "<tr><td>UnionSecret（9）</td><td><code>combined-class pts[4]</code></td><td>转 "
        "V6；与低编号 NWPU 数据需手动选择</td></tr>"
        "<tr><td>NWPU（9）</td><td><code>combined-class pts[4]</code></td><td>转 V6</td></tr>"
        "</table>"
        "<h3>字段说明</h3>"
        "<ul>"
        "<li><code>bbox</code> 依次为中心点 x/y、宽、高；<code>pts[4]</code> 为四组 x/y。</li>"
        "<li>V1、V2、V3、UPC 的 <code>color</code> 位于行首；HITSZ 将 <code>class color</code> "
        "放在行尾。</li>"
        "<li>V4 固定使用 36 类编码：每种颜色占 9 个编号，组内依次为 "
        "G/Big 1/2/3/4/5/O/Small Base/Big Base。</li>"
        "<li>V5 是原 17 字段 V6，只使用 14 类编码；class_id 允许范围为 0～13。</li>"
        "<li>V6 的 <code>color/size/class</code> 定义与 V2/V3 "
        "相同；四个关键点各自携带可见性：0=不可见、1=不在范围内、2=可见。</li>"
        "<li>不含可见性字段的输入转换为 V6 时，四点可见性全部设为 2。</li>"
        "<li>NWPU 的组合类别按 <code>color*16 + size*8 + class</code> 解码。</li>"
        "<li>UnionSecret 格式中 B/R/G 每种颜色占 13 个编号；0～7 是 Small "
        "G/1/2/3/4/5/O/Base，8～12 是 Big Base/G/3/4/5。class_id 全部小于 39 时需手动选择 "
        "UnionSecret 格式或 NWPU 格式；"
        "出现 39～63 时自动判为 NWPU。</li>"
        "</ul>"
        "<h3>识别规则</h3>"
        "<p>"
        "程序扫描目录内全部非空标签，按获得最多文件支持的格式导入；候选仍并列时要求手动选择或停止。"
        "除 V6 外的受支持格式统一转换为 V6；空数据集默认使用 V6。"
        "选定格式后，class 范围、字段数或其他内容错误的整个图片/标签会移入数据集根目录的 "
        "<code>stage/images</code> 与 <code>stage/labels</code>，合法样本继续完成导入。</p>");
}

void showFormatHelp(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("标签格式详解"));
    dialog.resize(760, 560);
    auto* layout  = new QVBoxLayout(&dialog);
    auto* browser = new QTextBrowser(&dialog);
    browser->setHtml(formatHelpHtml());
    layout->addWidget(browser);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

class FormatConflictMessageBox final : public QMessageBox {
public:
    explicit FormatConflictMessageBox(
        const labelmaster::service::label_format::FormatDetectionResult& detection,
        QWidget* parent = nullptr)
        : QMessageBox(parent) {
        setIcon(QMessageBox::Warning);
        setWindowTitle(tr("标签格式冲突"));
        setText(tr("无法安全地自动识别标签格式，未修改任何标签。"));
        setInformativeText(detection.error + tr("\n\n点击标题栏右上角的 ? 查看所有格式详解。"));
        setStandardButtons(QMessageBox::Ok);
        setWindowFlag(Qt::WindowContextHelpButtonHint, true);
        qApp->installEventFilter(this);
    }

    ~FormatConflictMessageBox() override { qApp->removeEventFilter(this); }

protected:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::EnterWhatsThisMode || event->type() == QEvent::WhatsThis
            || event->type() == QEvent::WhatsThisClicked) {
            queueFormatHelp();
            return true;
        }
        return QMessageBox::event(event);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::EnterWhatsThisMode && isVisible()) {
            queueFormatHelp();
            return true;
        }
        return QMessageBox::eventFilter(watched, event);
    }

private:
    void queueFormatHelp() {
        if (helpPending_)
            return;
        helpPending_ = true;
        QWhatsThis::leaveWhatsThisMode();
        QTimer::singleShot(0, this, [this] {
            showFormatHelp(this);
            helpPending_ = false;
        });
    }

    bool helpPending_ = false;
};
} // namespace

// ---------- 构造 / 析构 ----------
FileService::FileService(QObject* parent)
    : QObject(parent)
    , fsModel_(new QFileSystemModel(this))
    , proxy_(new ImageFilterProxy(this)) {

    fsModel_->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot | QDir::Files);
    fsModel_->setNameFilterDisables(false);
    fsModel_->setNameFilters(kImgExt);

    proxy_->setSourceModel(fsModel_);
    proxy_->setRecursiveFilteringEnabled(true);
    proxy_->setDynamicSortFilter(true);
    currentDataSet = DataSet::Auto;
    // Don't use DirectoryLoaded , need sort
    connect(
        fsModel_, &QFileSystemModel::directoryLoaded, this, &FileService::selectFirst,
        Qt::UniqueConnection);

    // proxy 重置时清空索引与当前路径，避免悬空
    connect(proxy_, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        proxyCurrent_ = QPersistentModelIndex();
        proxyRoot_    = QPersistentModelIndex();
        currentImagePath_.clear();
        currentImageSize_ = {};
    });

    // 异步尝试恢复上次图片（避免构造期阻塞）
    QTimer::singleShot(0, this, &FileService::tryRestoreLastVisited);
}
FileService::~FileService() = default;

// ---------- 模型暴露 ----------
void FileService::exposeModel() { emit modelReady(proxy_); }

// ---------- 打开入口 ----------

void FileService::openFolderDialog(const DataSet& type) {
    if (conflictMode_) {
        QMessageBox::information(
            QApplication::activeWindow(), tr("请先处理冲突"),
            tr("当前数据集仍有 stage 样本。请先逐个保存修复结果，或使用右下角的强制合并。"));
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        nullptr, tr("选择图片文件夹"), QString(),
        QFileDialog::Options(QFileDialog::DontUseNativeDialog | QFileDialog::ShowDirsOnly));
    if (dir.isEmpty())
        return;

    QMessageBox box;
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("选择目录类型"));
    box.setText(tr("你选择的是图片所在目录，还是图片所在目录的父目录？"));
    box.setInformativeText(tr("选择父目录时，会优先打开其中的 images/image/imgs/img 子目录。"));
    auto* imageDirButton  = box.addButton(tr("图片所在目录"), QMessageBox::AcceptRole);
    auto* parentDirButton = box.addButton(tr("图片目录父目录"), QMessageBox::ActionRole);
    auto* cancelButton    = box.addButton(QMessageBox::Cancel);
    box.exec();
    auto* clickedButton = box.clickedButton();
    if (clickedButton == cancelButton
        || (clickedButton != imageDirButton && clickedButton != parentDirButton))
        return;

    const QString openPath =
        clickedButton == parentDirButton ? imageDirectoryFromParent(dir) : QDir::cleanPath(dir);

    Q_UNUSED(type);
    currentDataSet = DataSet::Auto;
    openDir(openPath);
}

void FileService::startFiltering() {
    if (conflictMode_) {
        QMessageBox::information(
            QApplication::activeWindow(), tr("无法开始筛选"), tr("请先处理完 stage 冲突样本。"));
        return;
    }
    if (!pendingDir_.isEmpty() || currentDataSet == DataSet::Auto) {
        QMessageBox::information(
            QApplication::activeWindow(), tr("无法开始筛选"), tr("请等待当前图片目录加载完成。"));
        return;
    }

    const QString imageDir = QDir::cleanPath(fsModel_->rootPath());
    if (imageDir.isEmpty() || !QFileInfo(imageDir).isDir()) {
        QMessageBox::information(
            QApplication::activeWindow(), tr("无法开始筛选"), tr("请先打开一个图片目录。"));
        return;
    }

    QDir imageParent(imageDir);
    imageParent.cdUp();
    const QString filteringRoot      = imageParent.filePath(QStringLiteral("filtering"));
    const QString filteringImagesDir = QDir(filteringRoot).filePath(QStringLiteral("images"));
    const QString filteringLabelsDir = QDir(filteringRoot).filePath(QStringLiteral("labels"));
    const QString manifestPath = QDir(filteringRoot).filePath(QStringLiteral("manifest.json"));

    QVector<ui::FilterReviewEntry> entries;
    if (QFile::exists(manifestPath)) {
        QString error;
        if (!ui::loadFilterManifest(filteringRoot, entries, &error)) {
            QMessageBox::critical(QApplication::activeWindow(), tr("无法恢复筛选模式"), error);
            return;
        }
        if (!entries.isEmpty()) {
            ui::FilterReviewDialog review(filteringRoot, entries, QApplication::activeWindow());
            review.exec();
            currentDataSet = DataSet::Auto;
            openDir(imageDir);
            return;
        }
        QFile::remove(manifestPath);
    } else if (directoryContainsImage(filteringImagesDir)) {
        QMessageBox::critical(
            QApplication::activeWindow(), tr("无法恢复筛选模式"),
            tr("filtering 中存在图片但缺少 manifest.json，无法确定原始路径。"));
        return;
    }

    ui::FilterSelectionDialog selection(QApplication::activeWindow());
    if (selection.exec() != QDialog::Accepted)
        return;
    QSet<QString> selectedKeys;
    for (const QString& key : selection.selectedKeys())
        selectedKeys.insert(key);

    struct FilterCandidate {
        QString imagePath;
        QString labelPath;
    };
    QVector<FilterCandidate> candidates;
    QSet<QString> visitedLabels;
    QProgressDialog progress(
        tr("正在扫描标签组合…"), tr("取消"), 0, 0, QApplication::activeWindow());
    progress.setWindowTitle(tr("筛选 Label"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QDirIterator iterator(imageDir, kImgExt, QDir::Files, QDirIterator::Subdirectories);
    int scanned = 0;
    while (iterator.hasNext()) {
        const QString imagePath = iterator.next();
        const QString labelPath = labelFileForImage(imagePath);
        ++scanned;
        if (scanned == 1 || scanned % 20 == 0) {
            progress.setLabelText(tr("正在扫描第 %1 张图片…").arg(scanned));
            QApplication::processEvents();
            if (progress.wasCanceled())
                return;
        }
        if (!QFile::exists(labelPath) || visitedLabels.contains(labelPath))
            continue;
        visitedLabels.insert(labelPath);

        QImageReader reader(imagePath);
        const QSize imageSize = reader.size();
        if (imageSize.isEmpty())
            continue;
        QVector<Armor> armors;
        QString error;
        if (!labelmaster::service::label_format::readLabelFile(
                labelPath, imageSize, DataSet::LabelMasterV6, armors, &error)) {
            progress.close();
            QMessageBox::critical(
                QApplication::activeWindow(), tr("筛选失败"),
                tr("%1：%2").arg(QFileInfo(labelPath).fileName(), error));
            return;
        }

        bool matches = false;
        for (const Armor& armor : armors) {
            const int colorId = IdConvert::colorLetter2Id(armor.color);
            const int classId =
                IdConvert::classToken2Id(IdConvert::normalizeClasslToken(armor.cls));
            if (selectedKeys.contains(ui::filterCombinationKey(colorId, armor.size, classId))) {
                matches = true;
                break;
            }
        }
        if (matches)
            candidates.push_back({imagePath, labelPath});
    }

    if (candidates.isEmpty()) {
        progress.close();
        QMessageBox::information(
            QApplication::activeWindow(), tr("筛选完成"), tr("没有图片包含所选 Label 组合。"));
        return;
    }

    progress.setRange(0, candidates.size());
    progress.setValue(0);
    progress.setCancelButton(nullptr);
    entries.reserve(candidates.size());
    for (int index = 0; index < candidates.size(); ++index) {
        const FilterCandidate& candidate = candidates[index];
        const QString relativeImage      = QDir(imageDir).relativeFilePath(candidate.imagePath);
        const QFileInfo relativeInfo(relativeImage);
        const QString relativeLabel = relativeInfo.path() == QStringLiteral(".")
                                        ? relativeInfo.completeBaseName() + QStringLiteral(".txt")
                                        : relativeInfo.path() + QLatin1Char('/')
                                              + relativeInfo.completeBaseName()
                                              + QStringLiteral(".txt");
        ui::FilterReviewEntry entry;
        entry.filteredImagePath = QDir::cleanPath(QDir(filteringImagesDir).filePath(relativeImage));
        entry.filteredLabelPath = QDir::cleanPath(QDir(filteringLabelsDir).filePath(relativeLabel));
        entry.originalImagePath = candidate.imagePath;
        entry.originalLabelPath = candidate.labelPath;

        QString error;
        if (!moveFileWithoutOverwrite(candidate.labelPath, entry.filteredLabelPath, error)) {
            progress.close();
            QMessageBox::critical(QApplication::activeWindow(), tr("筛选移动失败"), error);
            break;
        }
        if (!moveFileWithoutOverwrite(candidate.imagePath, entry.filteredImagePath, error)) {
            QString rollbackError;
            moveFileWithoutOverwrite(entry.filteredLabelPath, candidate.labelPath, rollbackError);
            progress.close();
            QMessageBox::critical(QApplication::activeWindow(), tr("筛选移动失败"), error);
            break;
        }
        entries.push_back(entry);
        if (!ui::saveFilterManifest(filteringRoot, entries, &error)) {
            QString rollbackError;
            entries.removeLast();
            moveFileWithoutOverwrite(entry.filteredImagePath, candidate.imagePath, rollbackError);
            moveFileWithoutOverwrite(entry.filteredLabelPath, candidate.labelPath, rollbackError);
            progress.close();
            QMessageBox::critical(QApplication::activeWindow(), tr("筛选清单写入失败"), error);
            break;
        }
        progress.setValue(index + 1);
        progress.setLabelText(tr("正在移动匹配样本 %1/%2").arg(index + 1).arg(candidates.size()));
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }
    progress.close();

    if (entries.isEmpty())
        return;
    ui::FilterReviewDialog review(filteringRoot, entries, QApplication::activeWindow());
    review.exec();
    currentDataSet = DataSet::Auto;
    openDir(imageDir);
}
// 文件系统模型异步加载完成后，继续尝试打开第一张图片。
void FileService::selectFirst(const QString& path) {
    if (pendingDir_.isEmpty()) {
        emit busy(false);
        return;
    }
    if (!(path == pendingDir_ || path.startsWith(pendingDir_ + '/'))) {
        return;
    };
    if (formatDetectionAttempted_) {
        if (formatDetectionFinished_)
            tryOpenFirstAfterLoaded(pendingDir_);
        return;
    }
    startPendingImport();
}
// BFS 找第一张图片（跨多层）
QModelIndex FileService::findFirstImageUnder(const QModelIndex& proxyRoot) const {
    if (!proxyRoot.isValid())
        return {};
    QQueue<QModelIndex> q;
    q.enqueue(proxyRoot);

    while (!q.isEmpty()) {
        QModelIndex p  = q.dequeue();
        const int rows = proxy_->rowCount(p);
        for (int r = 0; r < rows; ++r) {
            QModelIndex idx = proxy_->index(r, 0, p);
            QModelIndex s   = mapFromProxyToSource(idx);
            if (!s.isValid())
                continue;

            if (fsModel_->isDir(s)) {
                q.enqueue(idx);
            } else {
                const QString path = fsModel_->filePath(s);
                if (isImageFile(path))
                    return idx;
            }
        }
    }
    return {};
}

bool FileService::openFileAt(const QModelIndex& proxyIndex) {
    if (currentDataSet == DataSet::Auto) {
        emit status(tr("标签格式尚未安全识别，无法打开图片"), 3000);
        return false;
    }
    const QModelIndex s = mapFromProxyToSource(proxyIndex);
    if (!s.isValid() || fsModel_->isDir(s))
        return false;

    const QString path = fsModel_->filePath(s);
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage img = reader.read();
    if (img.isNull()) {
        LOGE(QString("加载失败：%1 (%2)").arg(path, reader.errorString()));
        emit status(tr("加载失败：%1").arg(reader.errorString()), 1500);
        return false;
    }

    emit imageReady(img);
    emit status(tr("已打开：%1").arg(QFileInfo(path).fileName()), 800);

    currentImagePath_ = path;       // 记住路径（保存时用）
    currentImageSize_ = img.size(); // 记住尺寸（保存/反归一化）
    if (!conflictMode_)
        saveLastVisited(path);
    const QString lbl = conflictMode_ ? stageLabelForImage(path) : labelFileForImage(path);
    if (QFile::exists(lbl)) {
        QFile labelFile(lbl);
        if (labelFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&labelFile);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            in.setEncoding(QStringConverter::Utf8);
#else
            in.setCodec("UTF-8");
#endif
            QString labelText = in.readAll();
            emit labelTextChanged(labelText);
            labelFile.close();
        }
        QVector<Armor> armors;
        if (conflictMode_) {
            DataSet sourceFormat = DataSet::UnionSecret;
            for (const StageEntry& entry : stageEntries_) {
                if (QDir::cleanPath(entry.stageImagePath) == QDir::cleanPath(path)) {
                    if (entry.sourceFormat != DataSet::Auto)
                        sourceFormat = entry.sourceFormat;
                    break;
                }
            }
            QString readError;
            if (labelmaster::service::label_format::readLabelFile(
                    lbl, currentImageSize_, DataSet::LabelMasterV6, armors, &readError)) {
                emit status(tr("冲突标签已是 LabelMaster V6，保存或重新进入时可直接归位"), 4000);
            } else {
                QStringList lineErrors;
                readError.clear();
                if (!labelmaster::service::label_format::readLabelFileLenient(
                        lbl, currentImageSize_, sourceFormat, armors, lineErrors, &readError)) {
                    LOGE(QString("stage 标签读取失败：%1：%2").arg(lbl, readError));
                    emit status(
                        tr("冲突标签无法读取，将以空标注打开：%1").arg(readError), 5000);
                } else if (!lineErrors.isEmpty()) {
                    emit status(
                        tr("冲突模式：保留了 %1 个有效标注，需修复 %2 个错误行")
                            .arg(armors.size())
                            .arg(lineErrors.size()),
                        5000);
                }
            }
        } else {
            armors = readLabelFile(lbl, currentImageSize_, currentDataSet);
        }
        emit labelsLoaded(armors);
    } else {
        emit labelTextChanged("");
        emit labelsLoaded({});
    }
    return true;
}

void FileService::openIndex(const QModelIndex& proxyIndex) {
    if (!proxyIndex.isValid())
        return;
    proxyCurrent_ = proxyIndex;
    emit currentIndexChanged(proxyCurrent_);
    openFileAt(proxyCurrent_);
}

// ---------- 浏览 ----------
void FileService::next(bool allowAutoSave) {
    if (!proxyCurrent_.isValid())
        return;

    QModelIndex parent = proxyCurrent_.parent().isValid() ? proxyCurrent_.parent()
                                                          : static_cast<QModelIndex>(proxyRoot_);
    int r              = proxyCurrent_.row() + 1;
    const int rows     = proxy_->rowCount(parent);
    for (; r < rows; ++r) {
        const QModelIndex idx = proxy_->index(r, 0, parent);
        const QModelIndex s   = mapFromProxyToSource(idx);
        if (s.isValid() && !fsModel_->isDir(s) && isImageFile(fsModel_->filePath(s))) {
            if (controller::AppSettings::instance().autoSave() && allowAutoSave) {
                // 自动保存当前标注
                const bool resolvingConflict = conflictMode_;
                emit saveRequested();
                if (resolvingConflict)
                    return;
            }
            proxyCurrent_ = idx;
            emit currentIndexChanged(proxyCurrent_);
            openFileAt(proxyCurrent_);
            return;
        }
    }
    emit status(tr("已经是最后一张"), 900);
}

void FileService::prev() {
    if (!proxyCurrent_.isValid())
        return;

    QModelIndex parent = proxyCurrent_.parent().isValid() ? proxyCurrent_.parent()
                                                          : static_cast<QModelIndex>(proxyRoot_);
    int r              = proxyCurrent_.row() - 1;
    for (; r >= 0; --r) {
        const QModelIndex idx = proxy_->index(r, 0, parent);
        const QModelIndex s   = mapFromProxyToSource(idx);
        if (s.isValid() && !fsModel_->isDir(s) && isImageFile(fsModel_->filePath(s))) {
            if (controller::AppSettings::instance().autoSave()) {
                // 自动保存当前标注
                const bool resolvingConflict = conflictMode_;
                emit saveRequested();
                if (resolvingConflict)
                    return;
            }
            proxyCurrent_ = idx;
            emit currentIndexChanged(proxyCurrent_);
            openFileAt(proxyCurrent_);
            return;
        }
    }
    emit status(tr("已经是第一张"), 900);
}

// ---------- 删除 ----------
void FileService::deleteCurrent() {
    if (conflictMode_) {
        emit status(tr("冲突处理模式不能删除样本；请保存修复结果或使用强制合并"), 4000);
        return;
    }
    if (!proxyCurrent_.isValid())
        return;
    const QModelIndex s = mapFromProxyToSource(proxyCurrent_);
    if (!s.isValid() || fsModel_->isDir(s))
        return;

    const QString path = fsModel_->filePath(s);
    if (QFile::remove(path)) {
        LOGI(QString("已删除：%1").arg(path));
        const QString labelPath = labelFileForImage(path);
        if (QFile::exists(labelPath) && QFile::remove(labelPath)) {
            LOGI(QString("已删除：%1").arg(labelPath));
        }
        next(false);
        if (!proxyCurrent_.isValid()) {
            currentImagePath_.clear();
            currentImageSize_ = {};
        }
    } else {
        LOGE(QString("删除失败：%1").arg(path));
        emit status(tr("删除失败"), 1200);
    }
}

// ---------- 目录打开 ----------
QString FileService::dataSetRootForImageDir(const QString& imageDir) const {
    const QDir dir(QDir::cleanPath(imageDir));
    const QString name = QFileInfo(dir.absolutePath()).fileName().toLower();
    if (name == QStringLiteral("images") || name == QStringLiteral("image")
        || name == QStringLiteral("imgs") || name == QStringLiteral("img")) {
        QDir parent(dir.absolutePath());
        parent.cdUp();
        if (QFileInfo(parent.absolutePath())
                .fileName()
                .compare(QStringLiteral("stage"), Qt::CaseInsensitive)
            == 0) {
            parent.cdUp();
        }
        return QDir::cleanPath(parent.absolutePath());
    }
    return QDir::cleanPath(dir.absolutePath());
}

QString FileService::stageLabelForImage(const QString& stageImagePath) const {
    const QString relative = QDir(stageImagesDir_).relativeFilePath(stageImagePath);
    QFileInfo relativeInfo(relative);
    QString labelRelative = relativeInfo.path() == QStringLiteral(".")
                              ? relativeInfo.completeBaseName() + QStringLiteral(".txt")
                              : relativeInfo.path() + QLatin1Char('/')
                                    + relativeInfo.completeBaseName() + QStringLiteral(".txt");
    return QDir::cleanPath(QDir(stageLabelsDir_).filePath(labelRelative));
}

bool FileService::saveStageManifest(QString* error) const {
    if (stageRoot_.isEmpty())
        return true;
    if (!QDir().mkpath(stageRoot_)) {
        if (error)
            *error = tr("无法创建 stage 目录：%1").arg(stageRoot_);
        return false;
    }

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("originalImageDir"), originalImageDir_);
    QJsonArray entries;
    for (const StageEntry& entry : stageEntries_) {
        QJsonObject object;
        object.insert(QStringLiteral("stageImagePath"), entry.stageImagePath);
        object.insert(QStringLiteral("stageLabelPath"), entry.stageLabelPath);
        object.insert(QStringLiteral("originalImagePath"), entry.originalImagePath);
        object.insert(QStringLiteral("originalLabelPath"), entry.originalLabelPath);
        object.insert(QStringLiteral("error"), entry.error);
        object.insert(QStringLiteral("sourceFormat"), static_cast<int>(entry.sourceFormat));
        entries.push_back(object);
    }
    root.insert(QStringLiteral("entries"), entries);

    QSaveFile file(QDir(stageRoot_).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = tr("无法写入 stage 清单：%1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = tr("无法提交 stage 清单：%1").arg(file.errorString());
        return false;
    }
    return true;
}

bool FileService::loadStageManifest() {
    stageEntries_.clear();
    QFile file(QDir(stageRoot_).filePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        LOGE(QString("stage 清单损坏：%1").arg(parseError.errorString()));
        return false;
    }
    const QJsonObject root         = document.object();
    const QString savedOriginalDir = root.value(QStringLiteral("originalImageDir")).toString();
    if (!savedOriginalDir.isEmpty())
        originalImageDir_ = QDir::cleanPath(savedOriginalDir);
    for (const QJsonValue& value : root.value(QStringLiteral("entries")).toArray()) {
        const QJsonObject object = value.toObject();
        StageEntry entry;
        entry.stageImagePath    = object.value(QStringLiteral("stageImagePath")).toString();
        entry.stageLabelPath    = object.value(QStringLiteral("stageLabelPath")).toString();
        entry.originalImagePath = object.value(QStringLiteral("originalImagePath")).toString();
        entry.originalLabelPath = object.value(QStringLiteral("originalLabelPath")).toString();
        entry.error             = object.value(QStringLiteral("error")).toString();
        const int formatValue =
            object.value(QStringLiteral("sourceFormat")).toInt(static_cast<int>(DataSet::Auto));
        if (formatValue >= static_cast<int>(DataSet::LabelMaster)
            && formatValue <= static_cast<int>(DataSet::UnionSecret)) {
            entry.sourceFormat = static_cast<DataSet>(formatValue);
        }
        if (!entry.stageImagePath.isEmpty())
            stageEntries_.push_back(entry);
    }
    return true;
}

bool FileService::maybeEnterExistingStage(const QString& imageDir) {
    if (conflictMode_)
        return false;
    originalImageDir_ = QDir::cleanPath(imageDir);
    stageRoot_        = QDir(dataSetRootForImageDir(imageDir)).filePath(QStringLiteral("stage"));
    stageImagesDir_   = QDir(stageRoot_).filePath(QStringLiteral("images"));
    stageLabelsDir_   = QDir(stageRoot_).filePath(QStringLiteral("labels"));
    if (!QFileInfo(stageImagesDir_).isDir() || !directoryContainsImage(stageImagesDir_))
        return false;

    loadStageManifest();
    conflictMode_  = true;
    currentDataSet = DataSet::LabelMasterV6;
    controller::AppSettings::instance().setoutputFormat(
        static_cast<int>(LabelOutputFormat::LabelMasterV6));
    int remaining = 0;
    QDirIterator iterator(stageImagesDir_, kImgExt, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        ++remaining;
    }
    emit conflictModeChanged(true, remaining);
    emit status(tr("检测到未处理的 stage，进入冲突处理模式"), 5000);
    return true;
}

bool FileService::stageInvalidSample(
    const labelmaster::service::label_format::LabelFileSample& sample, DataSet sourceFormat,
    const QString& sampleError, QString& operationError) {
    if (sample.imagePath.isEmpty() || originalImageDir_.isEmpty()) {
        operationError = tr("无法确定冲突样本的原始图片路径");
        return false;
    }
    QDir().mkpath(stageImagesDir_);
    QDir().mkpath(stageLabelsDir_);

    const QString relativeImage = QDir(originalImageDir_).relativeFilePath(sample.imagePath);
    const QString stagedImage   = QDir::cleanPath(QDir(stageImagesDir_).filePath(relativeImage));
    const QString stagedLabel   = stageLabelForImage(stagedImage);

    if (!moveFileWithoutOverwrite(sample.path, stagedLabel, operationError))
        return false;
    if (!moveFileWithoutOverwrite(sample.imagePath, stagedImage, operationError)) {
        QString rollbackError;
        moveFileWithoutOverwrite(stagedLabel, sample.path, rollbackError);
        return false;
    }

    StageEntry entry;
    entry.stageImagePath    = stagedImage;
    entry.stageLabelPath    = stagedLabel;
    entry.originalImagePath = sample.imagePath;
    entry.originalLabelPath = sample.path;
    entry.error             = sampleError;
    entry.sourceFormat      = sourceFormat;
    stageEntries_.push_back(entry);
    if (!saveStageManifest(&operationError)) {
        stageEntries_.removeLast();
        QString rollbackError;
        moveFileWithoutOverwrite(stagedImage, sample.imagePath, rollbackError);
        moveFileWithoutOverwrite(stagedLabel, sample.path, rollbackError);
        return false;
    }
    return true;
}

bool FileService::openDir(const QString& dir) {
    const QString cleanDir = QDir::cleanPath(dir);
    if (!conflictMode_ && maybeEnterExistingStage(cleanDir))
        return openDir(stageImagesDir_);

    emit busy(true);

    QString lastDir           = fsModel_->rootPath();
    pendingDir_               = cleanDir; // 不清空 pendingTargetPath_，以便恢复时指定目标文件
    formatDetectionAttempted_ = false;
    formatDetectionFinished_  = false;
    pendingImageCount_        = -1;
    if (lastDir == cleanDir) {
        LOGI(QString("重新扫描已打开目录：%1").arg(cleanDir));
        QTimer::singleShot(0, this, &FileService::startPendingImport);
        return true;
    }
    const QModelIndex srcRoot = fsModel_->setRootPath(cleanDir); // 异步开始
    if (!srcRoot.isValid()) {
        LOGW(QString("无效目录：%1").arg(dir));
        emit busy(false);
        return false;
    }

    proxyRoot_ = mapFromSourceToProxy(srcRoot);
    if (proxyRoot_.isValid() && proxyRoot_.model() == proxy_) {
        emit rootChanged(proxyRoot_);
    }

    emit status(tr("打开目录：%1").arg(cleanDir));
    LOGI(QString("打开目录：%1").arg(cleanDir));
    // 数据集扫描不依赖 QFileSystemModel。立即排入事件循环，避免等待目录树加载完成后
    // 才出现进度对话框。
    QTimer::singleShot(0, this, [this, cleanDir] {
        if (pendingDir_ == cleanDir)
            startPendingImport();
    });
    return true;
}

bool FileService::setProxyRoot(const QString& dir) {
    if (!fsModel_ || !proxy_)
        return false;

    QModelIndex srcRoot = fsModel_->index(dir);
    if (!srcRoot.isValid())
        return false;

    QModelIndex pxRoot = mapFromSourceToProxy(srcRoot);
    if (!pxRoot.isValid())
        return false;

    if (pxRoot.model() != proxy_)
        return false;

    proxyRoot_     = pxRoot;
    const int rows = proxy_->rowCount(pxRoot);
    if (rows == 0)
        return false;
    return true;
}
void FileService::startPendingImport() {
    if (pendingDir_.isEmpty() || formatDetectionAttempted_)
        return;

    formatDetectionAttempted_ = true;
    const QString dir         = pendingDir_;
    const bool succeeded      = tryImportPendingDataSet();
    formatDetectionFinished_  = true;
    if (!succeeded) {
        if (pendingDir_ == dir)
            pendingDir_.clear();
        return;
    }
    if (pendingDir_ == dir)
        tryOpenFirstAfterLoaded(dir);
}

bool FileService::tryImportPendingDataSet() {
    if (conflictMode_ && QDir::cleanPath(pendingDir_) == QDir::cleanPath(stageImagesDir_)) {
        QStringList stageImages;
        QDirIterator iterator(stageImagesDir_, kImgExt, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext())
            stageImages.push_back(iterator.next());

        int alreadyV6Count = 0;
        int importedCount  = 0;
        int autoErrorCount = 0;
        for (const QString& stageImage : stageImages) {
            bool resolved  = false;
            bool converted = false;
            QString error;
            if (!tryAutoResolveConflict(stageImage, resolved, converted, error)) {
                ++autoErrorCount;
                LOGW(QString("stage 自动校验失败：%1：%2").arg(stageImage, error));
                continue;
            }
            if (!resolved)
                continue;
            if (converted)
                ++importedCount;
            else
                ++alreadyV6Count;
        }

        int count = 0;
        QDirIterator remainingIterator(
            stageImagesDir_, kImgExt, QDir::Files, QDirIterator::Subdirectories);
        while (remainingIterator.hasNext()) {
            remainingIterator.next();
            ++count;
        }
        pendingImageCount_ = count;
        currentDataSet     = DataSet::LabelMasterV6;
        controller::AppSettings::instance().setoutputFormat(
            static_cast<int>(LabelOutputFormat::LabelMasterV6));

        if (alreadyV6Count > 0 || importedCount > 0) {
            emit status(
                tr("stage 自动处理：%1 个 V6 原样归位，%2 个导入格式已转为 V6；剩余 %3 个冲突")
                    .arg(alreadyV6Count)
                    .arg(importedCount)
                    .arg(count),
                6000);
        } else if (autoErrorCount > 0) {
            emit status(tr("stage 中有 %1 个文件自动处理失败，已保留供手动处理")
                            .arg(autoErrorCount),
                        6000);
        }

        if (count == 0) {
            finishConflictModeIfEmpty();
            return false;
        }
        emit conflictModeChanged(true, count);
        return true;
    }

    QProgressDialog progressDialog(QApplication::activeWindow());
    progressDialog.setWindowTitle(tr("数据集格式"));
    progressDialog.setLabelText(tr("正在扫描图片..."));
    progressDialog.setCancelButton(nullptr);
    progressDialog.setRange(0, 0);
    progressDialog.setMinimumDuration(0);
    progressDialog.setAutoClose(false);
    progressDialog.setAutoReset(false);
    progressDialog.setWindowModality(Qt::ApplicationModal);
    progressDialog.show();
    progressDialog.raise();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const auto refreshProgress = [] {
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    const auto closeProgress = [&] {
        progressDialog.close();
        refreshProgress();
    };
    const auto updateProgress = [&](const QString& text, int current, int total) {
        const int maximum = total > 0 ? total : 1;
        progressDialog.setRange(0, maximum);
        progressDialog.setValue(total > 0 ? std::clamp(current, 0, total) : maximum);
        progressDialog.setLabelText(tr("%1 %2/%3").arg(text).arg(current).arg(total));
        refreshProgress();
    };

    QVector<labelmaster::service::label_format::LabelFileSample> samples;
    QSet<QString> visitedLabels;
    QDirIterator detectionIterator(pendingDir_, kImgExt, QDir::Files, QDirIterator::Subdirectories);
    QElapsedTimer scanRefreshTimer;
    scanRefreshTimer.start();
    int scannedImages = 0;
    while (detectionIterator.hasNext()) {
        const QString imagePath = detectionIterator.next();
        ++scannedImages;
        if (scannedImages == 1 || scanRefreshTimer.elapsed() >= 50) {
            progressDialog.setLabelText(tr("正在扫描图片 %1").arg(scannedImages));
            refreshProgress();
            scanRefreshTimer.restart();
        }

        const QString labelPath = labelFileForImage(imagePath);
        if (!QFile::exists(labelPath) || visitedLabels.contains(labelPath))
            continue;
        visitedLabels.insert(labelPath);

        QImageReader reader(imagePath);
        const QSize imageSize = reader.size();
        if (imageSize.isEmpty()) {
            closeProgress();
            emit busy(false);
            emit status(
                tr("格式识别失败：无法读取图片尺寸 %1").arg(QFileInfo(imagePath).fileName()), 4000);
            return false;
        }
        samples.push_back({labelPath, imageSize, imagePath});
    }
    pendingImageCount_ = scannedImages;

    const int sampleCount = static_cast<int>(samples.size());
    updateProgress(tr("正在验证数据集格式"), 0, sampleCount);
    auto detection = labelmaster::service::label_format::detectDataSetFormat(
        samples,
        [&](int current, int total) { updateProgress(tr("正在验证数据集格式"), current, total); });
    if (detection.v1UpcChoiceRequired) {
        closeProgress();
        DataSet selectedFormat = DataSet::LabelMaster;
        if (!chooseV1OrUpcFormat(QApplication::activeWindow(), selectedFormat)) {
            emit busy(false);
            emit status(tr("V1/UPC 格式未选择，未修改任何标签"), 4000);
            return false;
        }
        detection.format              = selectedFormat;
        detection.v1UpcChoiceRequired = false;
        progressDialog.setLabelText(tr("正在转换数据集格式..."));
        progressDialog.show();
        progressDialog.raise();
        refreshProgress();
    }
    if (detection.nineFieldChoiceRequired) {
        closeProgress();
        DataSet selectedFormat = DataSet::UnionSecret;
        if (!chooseNineFieldFormat(QApplication::activeWindow(), selectedFormat)) {
            emit busy(false);
            emit status(tr("9 字段类别编码未选择，未修改任何标签"), 4000);
            return false;
        }
        detection.format                  = selectedFormat;
        detection.nineFieldChoiceRequired = false;
        progressDialog.setLabelText(tr("正在转换数据集格式..."));
        progressDialog.show();
        progressDialog.raise();
        refreshProgress();
    }
    if (!detection.succeeded()) {
        closeProgress();
        emit busy(false);
        emit status(tr("标签格式冲突，未修改任何标签"), 5000);
        FormatConflictMessageBox dialog(detection, QApplication::activeWindow());
        dialog.exec();
        return false;
    }

    currentDataSet = detection.format;

    const bool directOpen = isDirectOpenFormat(currentDataSet);
    const LabelOutputFormat targetFormat = canonicalOutputFormat(currentDataSet);
    struct PendingConversion {
        QString labelPath;
        QSize imageSize;
        QVector<Armor> armors;
    };
    struct PendingConflict {
        labelmaster::service::label_format::LabelFileSample sample;
        QString error;
    };
    QVector<PendingConversion> conversions;
    QVector<PendingConflict> conflicts;

    // First parse every source file. A malformed label therefore cannot be replaced
    // with an empty or partially converted file.
    int parsedLabels = 0;
    for (const auto& sample : samples) {
        PendingConversion conversion{sample.path, sample.imageSize, {}};
        QString error;
        if (!labelmaster::service::label_format::readLabelFile(
                sample.path, sample.imageSize, currentDataSet, conversion.armors, &error)) {
            conflicts.push_back({sample, error});
            LOGW(QString("标签将移入 stage：%1：%2").arg(sample.path, error));
        } else {
            conversions.push_back(conversion);
        }
        ++parsedLabels;
        updateProgress(tr("正在转换数据集格式"), parsedLabels, sampleCount);
    }

    const int conversionCount = static_cast<int>(conversions.size());
    int writtenLabels         = 0;
    for (const PendingConversion& conversion : conversions) {
        if (!directOpen) {
            QString error;
            if (!labelmaster::service::label_format::writeLabelFile(
                    conversion.labelPath, conversion.imageSize, targetFormat, conversion.armors,
                    &error)) {
                closeProgress();
                emit busy(false);
                emit status(
                    tr("导入写入失败：%1：%2")
                        .arg(QFileInfo(conversion.labelPath).fileName(), error),
                    5000);
                LOGE(QString("导入写入失败：%1：%2").arg(conversion.labelPath, error));
                return false;
            }
        }
        ++writtenLabels;
        updateProgress(
            directOpen ? tr("正在校验 LabelMaster V6") : tr("正在写入转换结果"), writtenLabels,
            conversionCount);
    }

    if (!conflicts.isEmpty()) {
        originalImageDir_ = QDir::cleanPath(pendingDir_);
        stageRoot_ =
            QDir(dataSetRootForImageDir(originalImageDir_)).filePath(QStringLiteral("stage"));
        stageImagesDir_ = QDir(stageRoot_).filePath(QStringLiteral("images"));
        stageLabelsDir_ = QDir(stageRoot_).filePath(QStringLiteral("labels"));
        loadStageManifest();

        int stagedCount = 0;
        for (const PendingConflict& conflict : conflicts) {
            QString operationError;
            if (!stageInvalidSample(
                    conflict.sample, currentDataSet, conflict.error, operationError)) {
                closeProgress();
                emit busy(false);
                emit status(tr("无法隔离冲突标签：%1").arg(operationError), 6000);
                LOGE(QString("无法隔离冲突标签：%1").arg(operationError));
                return false;
            }
            ++stagedCount;
            updateProgress(tr("正在隔离冲突标签"), stagedCount, conflicts.size());
        }

        currentDataSet = DataSet::LabelMasterV6;
        controller::AppSettings::instance().setoutputFormat(
            static_cast<int>(LabelOutputFormat::LabelMasterV6));
        conflictMode_ = true;
        pendingTargetPath_.clear();
        closeProgress();
        emit conflictModeChanged(true, stageEntries_.size());
        emit status(
            (directOpen ? tr("已校验 %1 个 V6 标签；%2 个冲突样本进入 stage")
                        : tr("已转换 %1 个标签；%2 个冲突样本进入 stage"))
                .arg(conversions.size())
                .arg(stagedCount),
            6000);
        openDir(stageImagesDir_);
        return false;
    }

    currentDataSet = DataSet::LabelMasterV6;
    controller::AppSettings::instance().setoutputFormat(static_cast<int>(targetFormat));
    if (directOpen) {
        emit status(
            detection.hasAnnotations
                ? tr("自动识别为 LabelMaster V6；已逐文件校验并直接打开")
                : tr("未发现非空标签，默认使用 LabelMaster V6"),
            3000);
        closeProgress();
        return true;
    }
    emit status(
        tr("自动识别为 %1；已将 %2 个标签转换为 %3")
            .arg(labelmaster::service::label_format::dataSetName(detection.format))
            .arg(conversions.size())
            .arg(labelmaster::service::label_format::outputFormatName(targetFormat)),
        3000);
    closeProgress();
    return true;

    // Legacy conversion path kept below only as unreachable reference while the
    // surrounding file service is being decomposed.
#if 0
    if (currentDataSet != DataSet::LabelMaster2) {                                 // 开始导入
        auto fail = [&](const QString& tip, const QString& arg) {
            emit busy(false);
            emit status(tip, 1200);
            LOGE(QString("%1:%2").arg(tip).arg(arg));
        };
        const QModelIndex target = findFirstImageUnder(proxyRoot_);                // 找第一张图片
        if (target.isValid()) {
            for (int i = 0; i < proxy_->rowCount(target.parent()); i++) {
                QString imgPath = fsModel_->filePath(
                    fsModel_->index(i, 0, mapFromProxyToSource(target.parent()))); // 获取图片路径
                const QString labelPath = labelFileForImage(imgPath);              // 计算Label路径
                if (QFile::exists(labelPath)) {
                    QBuffer buffer;
                    buffer.open(QIODevice::ReadWrite);
                    QTextStream convertStream(&buffer);
                    QFile labelFile(labelPath);
                    if (!labelFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
                        fail("导入失败!无法打开Label", labelPath);
                        return false;
                    }
                    QTextStream ts(&labelFile);
                    switch (currentDataSet) {

                    case DataSet::LabelMaster: {                                   // v1 旧格式
                        // | color | label |
                        // | :---: | :---: |
                        // | 0 |  | G |
                        // | 1 |  | 1 |
                        // | 2 |  | 2 |
                        // | 3 |  | 3 |
                        // | 4 |  | 4 |
                        // | 5  | O(前哨站) |
                        // | 6 | Bs(基地小装甲) |
                        // | 7 | Bb(基地大装甲) |
                        while (!ts.atEnd()) {
                            QString raw = ts.readLine();
                            QStringList t;
                            if (!StringProcess::processLabelString(raw, t)) {
                                fail("格式错误!转换失败", labelPath);
                                continue;
                            }
                            int colorId, clsId, sizeId;
                            if (!StringProcess::InitLabelInfo(
                                    t, colorId, clsId, sizeId, currentDataSet)) {
                                fail("格式错误!转换失败", labelPath);
                                continue;
                            }
                            t.pop_front();
                            t.pop_front();
                            switch (clsId) {
                            case 5: clsId = 6; break;
                            case 6:
                            case 7: clsId = 7; break;
                            }
                            t.push_front(QString().number(clsId));
                            t.push_front(QString().number(sizeId));
                            t.push_front(QString().number(colorId));
                            convertStream << t.join(" ") << "\n";
                        }
                        break;
                    }
                    case DataSet::UPC:
                        while (!ts.atEnd()) {
                            QString raw = ts.readLine();
                            QStringList t;
                            if (!StringProcess::processLabelString(raw, t)) {
                                fail("格式错误!转换失败", labelPath);
                                continue;
                            }
                            int colorId, clsId, sizeId;
                            if (!StringProcess::InitLabelInfo(
                                    t, colorId, clsId, sizeId, currentDataSet)) {
                                fail("格式错误!转换失败", labelPath);
                                continue;
                            }
                            t.pop_front();
                            t.pop_front();
                            switch (clsId) {
                            case 8: clsId--; break;
                            case 9:
                            case 10:
                            case 11: clsId -= 6; break;
                            }
                            t.push_front(QString().number(clsId));
                            t.push_front(QString().number(sizeId));
                            t.push_front(QString().number(colorId));
                            convertStream << t.join(" ") << "\n";
                        }
                        break;
                    case DataSet::HITSZ: { // 南工骁鹰
                        while (!ts.atEnd()) {
                            QString raw = ts.readLine();
                            QStringList t;
                            if (!StringProcess::processLabelString(raw, t)) {
                                fail("格式错误!转换失败", labelPath);
                                continue;
                            }
                            int colorId, clsId, sizeId;
                            if (!StringProcess::InitLabelInfo(
                                    t, colorId, clsId, sizeId, currentDataSet)) {
                                fail("格式错误!转换失败", labelPath);
                                continue;
                            }
                            //[目标各点的x、y归一化坐标] <目标类id> <目标颜色id>
                            // 颜色字段:id
                            // 装甲板标注目标ID见下表
                            // 贴纸	ID
                            // G（哨兵）	0
                            // 1（一号）	1
                            // 2（二号）	2
                            // 3（三号）	3
                            // 4（四号）	4
                            // 5（五号）	5
                            // O（前哨站）	6
                            // Bs（基地）	7
                            // Bb（基地大装甲）	8
                            // L3（三号平衡）	9
                            // L4（四号平衡）	10
                            // L5（五号平衡）	11
                            // 颜色ID见下表
                            // 类别	color
                            // Blue	0
                            // Red	1
                            // N（熄灭) 2
                            // Purple	3
                            switch (clsId) {
                            case 8: clsId--; break;
                            case 9:
                            case 10:
                            case 11: clsId -= 6; break;
                            }
                            t.pop_back();
                            t.pop_back();
                            t.push_front(QString().number(clsId));
                            t.push_front(QString().number(sizeId));
                            t.push_front(QString().number(colorId));
                            convertStream << t.join(" ") << "\n";
                        }
                        break;
                    }
                    case DataSet::NWPU: {
                        while (!ts.atEnd()) {
                            QString raw = ts.readLine();
                            QStringList t;
                            if (!StringProcess::processLabelString(raw, t)) {
                                fail("格式错误!转换失败", labelPath);
                                continue;
                            }
                            int colorId, clsId, sizeId;
                            if (!StringProcess::InitLabelInfo(
                                    t, colorId, clsId, sizeId, currentDataSet)) {
                                fail("格式错误!转换失败", labelPath);
                                continue;
                            }
                            t.pop_front();
                            t.push_front(QString().number(clsId));
                            t.push_front(QString().number(sizeId));
                            t.push_front(QString().number(colorId));
                            //<id> [目标各点的x、y归一化坐标]
                            convertStream << t.join(" ") << "\n";
                        }
                        break;
                    }
                    default: break;
                    }
                    convertStream.seek(0);
                    QString Text = convertStream.readAll();
                    buffer.close();
                    labelFile.close();
                    if (labelFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                        QTextStream outStream(&labelFile);
# if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
                        outStream.setEncoding(QStringConverter::Utf8);
# else
                        outStream.setCodec("UTF-8");
# endif
                        outStream << Text;
                        // File closed by destructor
                    }
                }
            }
        } else {
            fail("导入失败!目标文件不存在!", fsModel_->filePath(target));
            return false;
        }
    }
    return true;
#endif
}
void FileService::tryOpenFirstAfterLoaded(const QString& dir) {
    if (!setProxyRoot(dir)) {
        // 格式扫描可能早于 QFileSystemModel 完成。目录确实没有图片时可以立即结束；
        // 否则等待后续 directoryLoaded 信号再次尝试。
        if (formatDetectionFinished_ && pendingImageCount_ == 0) {
            emit status(tr("目录下未找到图片"), 1200);
            emit busy(false);
            pendingDir_.clear();
        }
        return;
    };
    // 优先：若指定了目标文件（比如恢复上次图片）
    if (!pendingTargetPath_.isEmpty()) {
        const QModelIndex srcIdx = fsModel_->index(pendingTargetPath_);
        if (srcIdx.isValid() && !fsModel_->isDir(srcIdx)) {
            const QModelIndex px = mapFromSourceToProxy(srcIdx);
            if (px.isValid() && px.model() == proxy_) {
                proxyCurrent_ = px;
                emit currentIndexChanged(proxyCurrent_);
                openFileAt(proxyCurrent_);
                emit busy(false);
                pendingDir_.clear();
                pendingTargetPath_.clear();
                return;
            }
        }
    }
    // 定位失败则退化为第一张（不清空 pendingTargetPath_）

    const QModelIndex target = findFirstImageUnder(proxyRoot_);
    if (target.isValid()) {
        proxyCurrent_ = target;
        emit currentIndexChanged(proxyCurrent_);
        openFileAt(proxyCurrent_);
        emit busy(false);
        pendingDir_.clear();
    } else {
        if (pendingImageCount_ > 0)
            return;
        LOGW(QString("目录下未找到图片：%1").arg(dir));
        emit status(tr("目录下未找到图片"), 1200);
        emit busy(false);
        pendingDir_.clear();
    }
}

// ---------- 工具方法 ----------
QModelIndex FileService::mapFromProxyToSource(const QModelIndex& p) const {
    if (!proxy_)
        return {};
    if (!p.isValid())
        return {};
    if (p.model() != proxy_) {
        qWarning() << "mapFromProxyToSource: index model mismatch";
        return {};
    }
    return static_cast<QSortFilterProxyModel*>(proxy_)->mapToSource(p);
}

QModelIndex FileService::mapFromSourceToProxy(const QModelIndex& s) const {
    if (!proxy_)
        return {};
    if (!s.isValid())
        return {};
    if (s.model() != fsModel_) {
        qWarning() << "mapFromSourceToProxy: index model mismatch";
        return {};
    }
    return static_cast<QSortFilterProxyModel*>(proxy_)->mapFromSource(s);
}
bool FileService::isImageFile(const QString& path) const {
    const QString low = path.toLower();
    for (const auto& ext : kImgExt)
        if (low.endsWith(ext.mid(1)))
            return true;
    return false;
}

void FileService::openPaths(const QStringList& paths) {
    if (paths.isEmpty())
        return;
    if (conflictMode_) {
        emit status(tr("请先处理完 stage 中的冲突样本"), 5000);
        return;
    }

    QString dir;
    pendingTargetPath_.clear();
    currentDataSet = DataSet::Auto;

    for (QString p : paths) {
        if (p.startsWith("file://")) {
            QUrl u(p);
            if (u.isLocalFile())
                p = u.toLocalFile();
        }
        QFileInfo fi(p);
        if (!fi.exists())
            continue;

        if (fi.isDir()) {
            dir = fi.absoluteFilePath();
            pendingTargetPath_.clear();
            break;
        } else if (fi.isFile()) {
            if (dir.isEmpty())
                dir = fi.absolutePath();
            if (pendingTargetPath_.isEmpty())
                pendingTargetPath_ = fi.absoluteFilePath();
        }
    }

    if (!dir.isEmpty()) {
        openDir(dir);
    }
}

// ---------- 记忆 & 恢复 ----------
void FileService::saveLastVisited(const QString& imagePath) {
    // QSettings st("ATLabelMaster", "ATLabelMaster");
    // st.setValue("lastImagePath", imagePath);
    // st.setValue("lastDir", QFileInfo(imagePath).absolutePath());
    controller::AppSettings::instance().setlastImagePath(imagePath);
    controller::AppSettings::instance().setlastImageDir(QFileInfo(imagePath).absolutePath());
}

void FileService::tryRestoreLastVisited() {
    const QString lastImg = controller::AppSettings::instance().lastImagePath();
    const QString lastDir = controller::AppSettings::instance().lastImageDir();
    // QSettings st("ATLabelMaster", "ATLabelMaster");
    // const QString lastImg = st.value("lastImagePath").toString();
    // const QString lastDir = st.value("lastDir").toString();
    if (lastDir.isEmpty())
        return;

    if (!lastImg.isEmpty()) {
        pendingTargetPath_ = lastImg; // 先设目标，再 openDir
    }
    openDir(lastDir);
    // QTimer::singleShot(0, this, [this, lastDir] { tryOpenFirstAfterLoaded(lastDir); });
}

// ---------- 标注 I/O（归一化格式 + 兼容旧像素格式） ----------
QString FileService::labelFileForImage(const QString& imagePath) {
    QFileInfo fi(imagePath);
    const QString configuredDir = controller::AppSettings::instance().saveDir().trimmed();
    const QDir imageDir(fi.absolutePath());
    const QDir imageParent(QDir::cleanPath(imageDir.filePath(QStringLiteral(".."))));
    const QString labelFileName = fi.completeBaseName() + QStringLiteral(".txt");

    auto defaultDirs = [&](const QString& name) {
        return QStringList{
            QDir::cleanPath(imageParent.filePath(name)),
            QDir::cleanPath(imageDir.filePath(name)),
        };
    };
    auto labelPathIn = [&](const QString& dirPath) {
        return QDir::cleanPath(QDir(dirPath).absoluteFilePath(labelFileName));
    };

    if (configuredDir.isEmpty() || configuredDir == QStringLiteral("label")
        || configuredDir == QStringLiteral("labels")) {
        QStringList candidateDirs;
        if (configuredDir == QStringLiteral("labels")) {
            candidateDirs << defaultDirs(QStringLiteral("labels"))
                          << defaultDirs(QStringLiteral("label"));
        } else if (configuredDir == QStringLiteral("label")) {
            candidateDirs << defaultDirs(QStringLiteral("label"))
                          << defaultDirs(QStringLiteral("labels"));
        } else {
            candidateDirs << defaultDirs(QStringLiteral("labels"))
                          << defaultDirs(QStringLiteral("label"));
        }

        for (const QString& dirPath : candidateDirs) {
            const QString candidate = labelPathIn(dirPath);
            if (QFile::exists(candidate))
                return candidate;
        }
        for (const QString& dirPath : candidateDirs) {
            if (QFileInfo(dirPath).isDir())
                return labelPathIn(dirPath);
        }

        const QString fallbackName = configuredDir == QStringLiteral("labels")
                                       ? QStringLiteral("labels")
                                       : QStringLiteral("label");
        return labelPathIn(imageParent.filePath(fallbackName));
    }

    QDir labelDir(configuredDir);
    if (labelDir.isAbsolute())
        return labelPathIn(labelDir.absolutePath());

    const QString parentCandidate   = QDir::cleanPath(imageParent.filePath(configuredDir));
    const QString imageDirCandidate = QDir::cleanPath(imageDir.filePath(configuredDir));
    const QString parentLabelPath   = labelPathIn(parentCandidate);
    const QString imageDirLabelPath = labelPathIn(imageDirCandidate);
    if (QFile::exists(parentLabelPath))
        return parentLabelPath;
    if (QFile::exists(imageDirLabelPath))
        return imageDirLabelPath;
    if (QFileInfo(parentCandidate).isDir())
        return parentLabelPath;
    if (QFileInfo(imageDirCandidate).isDir())
        return imageDirLabelPath;
    return parentLabelPath;
}

bool FileService::writeLabelFile(
    const QString& labelPath, const QVector<Armor>& armors, const QSize& imgSize) {
    const int configured = controller::AppSettings::instance().outputFormat();
    const LabelOutputFormat format =
        configured >= static_cast<int>(LabelOutputFormat::Points11)
                && configured <= static_cast<int>(LabelOutputFormat::LabelMasterV6)
            ? static_cast<LabelOutputFormat>(configured)
            : LabelOutputFormat::LabelMasterV6;
    QString error;
    const bool written = labelmaster::service::label_format::writeLabelFile(
        labelPath, imgSize, format, armors, &error);
    if (!written)
        LOGE(QString("标签写入失败：%1：%2").arg(labelPath, error));
    return written;

#if 0
    if (imgSize.width() <= 0 || imgSize.height() <= 0)
        return false;

    QDir().mkpath(QFileInfo(labelPath).absolutePath());
    QFile f(labelPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;

    QTextStream ts(&f);
# if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    ts.setEncoding(QStringConverter::Utf8);
# else
    ts.setCodec("UTF-8");
# endif
    ts.setRealNumberNotation(QTextStream::FixedNotation);
    ts.setRealNumberPrecision(6); // 保留 6 位小数

    const double W = double(imgSize.width());
    const double H = double(imgSize.height());
    auto norm      = [&](const QPointF& p) { return QPointF(p.x() / W, p.y() / H); };

    // 获取输出格式设置: 0=pts-only(11), 1=xywh+pts(15), 2=cls+xywh+pts(13)
    int outputFormat = controller::AppSettings::instance().outputFormat();

    for (const auto& a : armors) {
        const int colorId = IdConvert::colorLetter2Id(a.color);
        const int classId = IdConvert::classToken2Id(
            IdConvert::normalizeClasslToken(
                a.cls));          // class size

        const QPointF q0 = norm(a.p0), q1 = norm(a.p1), q2 = norm(a.p2), q3 = norm(a.p3);

        if (outputFormat == 2) {
            // LabelMasterV4格式: cls x_c y_c w h x0 y0 x1 y1 x2 y2 x3 y3 (13字段)
            double x, y, w, h;
            // 计算SVG透视变换后的真实边界框
            const auto& svgTemplate = (a.size == 0)
                ? labelmaster::util::SvgConstants::smallArmor()
                : labelmaster::util::SvgConstants::bigArmor();

            QPolygonF svg_quad;
            svg_quad << QPointF(0., 0.)
                     << QPointF(0., svgTemplate.height)
                     << QPointF(svgTemplate.width, svgTemplate.height)
                     << QPointF(svgTemplate.width, 0.);

            QPolygonF img_anchors;
            img_anchors << a.p0 << a.p1 << a.p2 << a.p3;

            QTransform transform;
            if (QTransform::quadToQuad(svgTemplate.anchors, img_anchors, transform)) {
                QPolygonF img_corners = transform.map(svg_quad);

                double min_x = std::numeric_limits<double>::max();
                double min_y = std::numeric_limits<double>::max();
                double max_x = std::numeric_limits<double>::lowest();
                double max_y = std::numeric_limits<double>::lowest();

                for (const auto& pt : img_corners) {
                    min_x = std::min(min_x, pt.x());
                    min_y = std::min(min_y, pt.y());
                    max_x = std::max(max_x, pt.x());
                    max_y = std::max(max_y, pt.y());
                }

                w = (max_x - min_x) / W;
                h = (max_y - min_y) / H;
                x = (min_x + max_x) / (2.0 * W);
                y = (min_y + max_y) / (2.0 * H);
            } else {
                double min_x = std::min({a.p0.x(), a.p1.x(), a.p2.x(), a.p3.x()});
                double min_y = std::min({a.p0.y(), a.p1.y(), a.p2.y(), a.p3.y()});
                double max_x = std::max({a.p0.x(), a.p1.x(), a.p2.x(), a.p3.x()});
                double max_y = std::max({a.p0.y(), a.p1.y(), a.p2.y(), a.p3.y()});
                w = (max_x - min_x) / W;
                h = (max_y - min_y) / H;
                x = (min_x + max_x) / (2.0 * W);
                y = (min_y + max_y) / (2.0 * H);
            }

            // Clamp到[0,1]范围
            auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
            x = clamp01(x);
            y = clamp01(y);
            w = clamp01(w);
            h = clamp01(h);
            QPointF q0_clamped(clamp01(q0.x()), clamp01(q0.y()));
            QPointF q1_clamped(clamp01(q1.x()), clamp01(q1.y()));
            QPointF q2_clamped(clamp01(q2.x()), clamp01(q2.y()));
            QPointF q3_clamped(clamp01(q3.x()), clamp01(q3.y()));

            // 写入: cls x_c y_c w_h x0 y0 x1 y1 x2 y2 x3 y3
            ts << a.cls << ' '
               << x << ' ' << y << ' ' << w << ' ' << h << ' '
               << q0_clamped.x() << ' ' << q0_clamped.y() << ' '
               << q1_clamped.x() << ' ' << q1_clamped.y() << ' '
               << q2_clamped.x() << ' ' << q2_clamped.y() << ' '
               << q3_clamped.x() << ' ' << q3_clamped.y() << '\n';
        } else if (outputFormat == 1) {
            // 新格式: color size cls x y w h x0 y0 x1 y1 x2 y2 x3 y3
            // 四个点是PnP锚点，bbox需要计算SVG透视变换后的真实边界框
            // 每次都重新计算bbox，因为角点可能被用户修改
            double x, y, w, h;
            // 计算SVG透视变换后的真实边界框 - 使用集中管理的常量
            const auto& svgTemplate = (a.size == 0)
                ? labelmaster::util::SvgConstants::smallArmor()
                : labelmaster::util::SvgConstants::bigArmor();

            // SVG外框四个角 (TL, BL, BR, TR)
            QPolygonF svg_quad;
            svg_quad << QPointF(0., 0.)
                     << QPointF(0., svgTemplate.height)
                     << QPointF(svgTemplate.width, svgTemplate.height)
                     << QPointF(svgTemplate.width, 0.);

            // 图像中的四个锚点 (像素坐标)
            QPolygonF img_anchors;
            img_anchors << a.p0 << a.p1 << a.p2 << a.p3;

            // 计算从SVG坐标系到图像坐标系的单应性矩阵
            QTransform transform;
            if (QTransform::quadToQuad(svgTemplate.anchors, img_anchors, transform)) {
                // 将SVG外框四个角变换到图像坐标
                QPolygonF img_corners = transform.map(svg_quad);

                // 计算边界框 (像素坐标)
                double min_x = std::numeric_limits<double>::max();
                double min_y = std::numeric_limits<double>::max();
                double max_x = std::numeric_limits<double>::lowest();
                double max_y = std::numeric_limits<double>::lowest();

                for (const auto& pt : img_corners) {
                    min_x = std::min(min_x, pt.x());
                    min_y = std::min(min_y, pt.y());
                    max_x = std::max(max_x, pt.x());
                    max_y = std::max(max_y, pt.y());
                }

                // 转换为归一化坐标: 中心点x, 中心点y, 宽度, 高度
                w = (max_x - min_x) / W;
                h = (max_y - min_y) / H;
                x = (min_x + max_x) / (2.0 * W);
                y = (min_y + max_y) / (2.0 * H);
            } else {
                // 透视变换失败，使用锚点的边界框作为后备
                double min_x = std::min({a.p0.x(), a.p1.x(), a.p2.x(), a.p3.x()});
                double min_y = std::min({a.p0.y(), a.p1.y(), a.p2.y(), a.p3.y()});
                double max_x = std::max({a.p0.x(), a.p1.x(), a.p2.x(), a.p3.x()});
                double max_y = std::max({a.p0.y(), a.p1.y(), a.p2.y(), a.p3.y()});
                w = (max_x - min_x) / W;
                h = (max_y - min_y) / H;
                x = (min_x + max_x) / (2.0 * W);
                y = (min_y + max_y) / (2.0 * H);
            }

            // Clamp bbox和kpts到[0,1]范围
            auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
            x = clamp01(x);
            y = clamp01(y);
            w = clamp01(w);
            h = clamp01(h);
            QPointF q0_clamped(clamp01(q0.x()), clamp01(q0.y()));
            QPointF q1_clamped(clamp01(q1.x()), clamp01(q1.y()));
            QPointF q2_clamped(clamp01(q2.x()), clamp01(q2.y()));
            QPointF q3_clamped(clamp01(q3.x()), clamp01(q3.y()));

            ts << colorId << ' ' << a.size << ' ' << classId << ' '
               << x << ' ' << y << ' ' << w << ' ' << h << ' '
               << q0_clamped.x() << ' ' << q0_clamped.y() << ' '
               << q1_clamped.x() << ' ' << q1_clamped.y() << ' '
               << q2_clamped.x() << ' ' << q2_clamped.y() << ' '
               << q3_clamped.x() << ' ' << q3_clamped.y() << '\n';
        } else {
            // 旧格式: color size cls x0 y0 x1 y1 x2 y2 x3 y3
            ts << colorId << ' ' << a.size << ' ' << classId << ' ' << q0.x() << ' ' << q0.y() << ' '
               << q1.x() << ' ' << q1.y() << ' ' << q2.x() << ' ' << q2.y() << ' ' << q3.x() << ' '
               << q3.y() << '\n';
        }
    }
    ts.flush();
    // File closed by destructor
    return true;
#endif
}

QVector<Armor>
    FileService::readLabelFile(const QString& labelPath, const QSize& imgSize, DataSet format) {
    {
        QVector<Armor> parsed;
        QString error;
        if (!labelmaster::service::label_format::readLabelFile(
                labelPath, imgSize, format, parsed, &error)) {
            LOGE(QString("标签读取失败：%1：%2").arg(labelPath, error));
            return {};
        }
        return parsed;
    }

#if 0
    QVector<Armor> res;
    QFile f(labelPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return res;

    const double W = double(imgSize.width());
    const double H = double(imgSize.height());

    QTextStream ts(&f);
# if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    ts.setEncoding(QStringConverter::Utf8);
# else
    ts.setCodec("UTF-8");
# endif
    while (!ts.atEnd()) {
        QString raw = ts.readLine();
        int hash    = raw.indexOf('#');
        if (hash >= 0)
            raw = raw.left(hash);
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;

        // 支持 11字段、13字段(V4)和 15字段 格式
        const QStringList t = line.simplified().split(' ');
        if (t.size() != 11 && t.size() != 13 && t.size() != 15)
            continue;

        bool ok  = true;
        auto tod = [&](int i) -> double {
            bool o   = false;
            double v = t.at(i).toDouble(&o);
            ok &= o;
            return v;
        };

        Armor a;
        a.score = 0.f;

        // 检测格式类型：V4格式第一个字段是字符串(如"R1")，旧格式是数字
        bool isFirstFieldString = false;
        if (t.size() == 13) {
            // V4格式: cls x_c y_c w h x0 y0 x1 y1 x2 y2 x3 y3
            // 第一个字段可能是字符串如 "R1", "B2", "G3"
            QChar firstChar = t.at(0).isEmpty() ? QChar() : t.at(0).at(0);
            isFirstFieldString = firstChar.isLetter();
        }

        if (isFirstFieldString && t.size() == 13) {
            // LabelMasterV4 格式: cls x_c y_c w h x0 y0 x1 y1 x2 y2 x3 y3
            QString cls = t.at(0);
            if (!cls.isEmpty()) {
                a.color = cls.at(0).toUpper(); // R, B, G, P
            }
            a.cls = cls;

            // bbox: x_c y_c w h (归一化)
            double x = tod(1), y = tod(2), w = tod(3), h = tod(4);
            // 角点: x0 y0 x1 y1 x2 y2 x3 y3 (归一化)
            double x0 = tod(5), y0 = tod(6), x1 = tod(7), y1 = tod(8);
            double x2 = tod(9), y2 = tod(10), x3 = tod(11), y3 = tod(12);
            if (!ok)
                continue;

            // 存储归一化bbox
            auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
            a.norm_x = clamp01(x);
            a.norm_y = clamp01(y);
            a.norm_w = clamp01(w);
            a.norm_h = clamp01(h);

            // 反归一化角点到像素坐标
            a.p0 = QPointF(x0 * W, y0 * H);
            a.p1 = QPointF(x1 * W, y1 * H);
            a.p2 = QPointF(x2 * W, y2 * H);
            a.p3 = QPointF(x3 * W, y3 * H);

            // 存储归一化角点
            a.norm_p0 = QPointF(x0, y0);
            a.norm_p1 = QPointF(x1, y1);
            a.norm_p2 = QPointF(x2, y2);
            a.norm_p3 = QPointF(x3, y3);

            // 通过宽高比判断size
            double aspectRatio = w / (h + 1e-6);
            a.size = (aspectRatio > 2.5) ? 1 : 0;  // 1 = big, 0 = small
        } else {
            // 旧格式：前三个字段是数字
            bool okInt  = false;
            int colorId = t.at(0).toInt(&okInt);
            int size    = t.at(1).toInt(&okInt);
            int classId = t.at(2).toInt(&okInt);
            if (!okInt) {
                continue;
            }
            a.color   = IdConvert::colorId2Letter(colorId);
            a.cls     = IdConvert::idCollect2Token(classId);
            a.size    = size;

            if (t.size() == 15) {
            // 新格式: color size cls x y w h x0 y0 x1 y1 x2 y2 x3 y3
            double x = tod(3), y = tod(4), w = tod(5), h = tod(6);
            double x0 = tod(7), y0 = tod(8), x1 = tod(9), y1 = tod(10);
            double x2 = tod(11), y2 = tod(12), x3 = tod(13), y3 = tod(14);
            if (!ok)
                continue;

            // 归一化判定：坐标绝对值的最大值 <= 1.5 视为已归一化（留容错）
            const double mx = std::max({std::fabs(x0), std::fabs(x1), std::fabs(x2), std::fabs(x3)});
            const double my = std::max({std::fabs(y0), std::fabs(y1), std::fabs(y2), std::fabs(y3)});
            const bool normalized = (mx <= 1.5 && my <= 1.5 && W > 0 && H > 0);

            // 首先反归一化角点得到像素坐标（在判断之后，不先clamp）
            auto denorm = [&](double vx, double vy) -> QPointF {
                return normalized ? QPointF(vx * W, vy * H) : QPointF(vx, vy);
            };

            a.p0 = denorm(x0, y0);
            a.p1 = denorm(x1, y1);
            a.p2 = denorm(x2, y2);
            a.p3 = denorm(x3, y3);

            // 对于bbox，如果数据是归一化的，则存储clamp后的归一化值
            // 如果数据是像素坐标，需要先归一化再clamp
            auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
            if (normalized) {
                // 数据已经是归一化的，直接clamp并存储
                a.norm_x = clamp01(x);
                a.norm_y = clamp01(y);
                a.norm_w = clamp01(w);
                a.norm_h = clamp01(h);
            } else {
                // 数据是像素坐标，先归一化再clamp
                if (W > 0 && H > 0) {
                    a.norm_x = clamp01(x / W);
                    a.norm_y = clamp01(y / H);
                    a.norm_w = clamp01(w / W);
                    a.norm_h = clamp01(h / H);
                } else {
                    a.norm_x = a.norm_y = a.norm_w = a.norm_h = -1;
                }
            }
        } else {
            // 旧格式: color size cls x0 y0 x1 y1 x2 y2 x3 y3
            double x0 = tod(3), y0 = tod(4), x1 = tod(5), y1 = tod(6), x2 = tod(7), y2 = tod(8),
                   x3 = tod(9), y3 = tod(10);
            if (!ok)
                continue;

            // 归一化判定：坐标绝对值的最大值 <= 1.5 视为已归一化（留容错）
            const double mx = std::max({std::fabs(x0), std::fabs(x1), std::fabs(x2), std::fabs(x3)});
            const double my = std::max({std::fabs(y0), std::fabs(y1), std::fabs(y2), std::fabs(y3)});
            const bool normalized = (mx <= 1.5 && my <= 1.5 && W > 0 && H > 0);

            auto denorm = [&](double vx, double vy) -> QPointF {
                return normalized ? QPointF(vx * W, vy * H) : QPointF(vx, vy);
            };

            a.p0 = denorm(x0, y0);
            a.p1 = denorm(x1, y1);
            a.p2 = denorm(x2, y2);
            a.p3 = denorm(x3, y3);
            a.norm_x = -1; // 标记非新格式
        }
        }  // 旧格式（11/15字段）处理结束

        res.push_back(a);
    }
    return res;
#endif
}

// ---------- 保存标注（对外槽） ----------
bool FileService::tryAutoResolveConflict(
    const QString& stageImagePath, bool& resolved, bool& converted, QString& error) {
    resolved  = false;
    converted = false;

    const QString stageImage = QDir::cleanPath(stageImagePath);
    StageEntry entry;
    bool found = false;
    for (const StageEntry& candidate : stageEntries_) {
        if (QDir::cleanPath(candidate.stageImagePath) == stageImage) {
            entry = candidate;
            found = true;
            break;
        }
    }
    if (!found) {
        // 没有清单就无法可靠获知“本次导入格式”，但仍可安全识别已经是 V6 的文件。
        entry.stageImagePath = stageImage;
        entry.stageLabelPath = stageLabelForImage(stageImage);
        const QString relative = QDir(stageImagesDir_).relativeFilePath(stageImage);
        entry.originalImagePath = QDir::cleanPath(QDir(originalImageDir_).filePath(relative));
        entry.originalLabelPath = labelFileForImage(entry.originalImagePath);
        entry.sourceFormat = DataSet::Auto;
    }

    QImageReader reader(stageImage);
    const QSize imageSize = reader.size();
    if (imageSize.isEmpty()) {
        error = tr("无法读取 stage 图片尺寸");
        return false;
    }

    QVector<Armor> armors;
    QString validationError;
    if (labelmaster::service::label_format::readLabelFile(
            entry.stageLabelPath, imageSize, DataSet::LabelMasterV6, armors, &validationError)) {
        if (!restoreConflict(stageImage, false, error))
            return false;
        resolved = true;
        return true;
    }

    if (entry.sourceFormat == DataSet::Auto || entry.sourceFormat == DataSet::LabelMasterV6)
        return true;

    armors.clear();
    validationError.clear();
    if (!labelmaster::service::label_format::readLabelFile(
            entry.stageLabelPath, imageSize, entry.sourceFormat, armors, &validationError)) {
        return true;
    }
    if (!labelmaster::service::label_format::writeLabelFile(
            entry.stageLabelPath, imageSize, LabelOutputFormat::LabelMasterV6, armors, &error)) {
        return false;
    }

    QVector<Armor> verification;
    if (!labelmaster::service::label_format::readLabelFile(
            entry.stageLabelPath, imageSize, DataSet::LabelMasterV6, verification, &error)) {
        return false;
    }
    if (!restoreConflict(stageImage, false, error))
        return false;
    resolved  = true;
    converted = true;
    return true;
}

bool FileService::restoreConflict(const QString& stageImagePath, bool force, QString& error) {
    const QString stageImage = QDir::cleanPath(stageImagePath);
    int entryIndex           = -1;
    StageEntry entry;
    for (int index = 0; index < stageEntries_.size(); ++index) {
        if (QDir::cleanPath(stageEntries_[index].stageImagePath) == stageImage) {
            entryIndex = index;
            entry      = stageEntries_[index];
            break;
        }
    }

    if (entryIndex < 0) {
        const QString relative  = QDir(stageImagesDir_).relativeFilePath(stageImage);
        entry.stageImagePath    = stageImage;
        entry.stageLabelPath    = stageLabelForImage(stageImage);
        entry.originalImagePath = QDir::cleanPath(QDir(originalImageDir_).filePath(relative));
        entry.originalLabelPath = labelFileForImage(entry.originalImagePath);
    }

    if (!force
        && (QFile::exists(entry.originalImagePath) || QFile::exists(entry.originalLabelPath))) {
        error = tr("原目录已存在同名图片或标签，为避免覆盖已停止回迁");
        return false;
    }

    if (!copyFileReplacing(entry.stageImagePath, entry.originalImagePath, force, error))
        return false;
    if (!copyFileReplacing(entry.stageLabelPath, entry.originalLabelPath, force, error)) {
        if (!force)
            QFile::remove(entry.originalImagePath);
        return false;
    }

    if (!QFile::remove(entry.stageImagePath)) {
        error = tr("已复制回原目录，但无法删除 stage 图片：%1").arg(entry.stageImagePath);
        return false;
    }
    if (!QFile::remove(entry.stageLabelPath)) {
        error = tr("已复制回原目录，但无法删除 stage 标签：%1").arg(entry.stageLabelPath);
        return false;
    }

    if (entryIndex >= 0)
        stageEntries_.removeAt(entryIndex);
    if (!saveStageManifest(&error))
        return false;
    return true;
}

bool FileService::restoreCurrentConflict(bool force, QString& error) {
    return restoreConflict(currentImagePath_, force, error);
}

void FileService::finishConflictModeIfEmpty() {
    if (!conflictMode_)
        return;
    QDirIterator iterator(stageImagesDir_, kImgExt, QDir::Files, QDirIterator::Subdirectories);
    if (iterator.hasNext()) {
        int remaining = 0;
        while (iterator.hasNext()) {
            iterator.next();
            ++remaining;
        }
        emit conflictModeChanged(true, remaining);
        return;
    }

    QFile::remove(QDir(stageRoot_).filePath(QStringLiteral("manifest.json")));
    QDir().rmdir(stageLabelsDir_);
    QDir().rmdir(stageImagesDir_);
    QDir().rmdir(stageRoot_);
    conflictMode_ = false;
    stageEntries_.clear();
    emit conflictModeChanged(false, 0);
    emit status(tr("所有冲突样本已处理，返回原数据集"), 5000);
    currentDataSet = DataSet::Auto;
    pendingTargetPath_.clear();
    const QString destination = originalImageDir_;
    originalImageDir_.clear();
    openDir(destination);
}

void FileService::refreshConflictDirectory() {
    finishConflictModeIfEmpty();
    if (!conflictMode_)
        return;
    pendingTargetPath_.clear();
    currentDataSet = DataSet::LabelMasterV6;
    openDir(stageImagesDir_);
}

void FileService::forceMergeCurrentConflict() {
    if (!conflictMode_ || currentImagePath_.isEmpty())
        return;
    QMessageBox box(QApplication::activeWindow());
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("跳过校验并强制合并"));
    box.setText(tr("当前标签尚未通过导入格式或 LabelMaster V6 校验。"));
    box.setInformativeText(
        tr("继续会把 stage 中的原始图片和标签直接复制回数据集，并覆盖同名文件，不会执行导入转换。"
           "该标签以后可能无法正常打开。"));
    auto* mergeButton = box.addButton(tr("仍然合并"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() != mergeButton)
        return;

    QString error;
    if (!restoreCurrentConflict(true, error)) {
        QMessageBox::critical(QApplication::activeWindow(), tr("强制合并失败"), error);
        return;
    }
    emit status(tr("已跳过导入格式/V6 校验并强制合并当前冲突样本"), 5000);
    refreshConflictDirectory();
}

void FileService::saveData(const QVector<Armor>& armors, const QImage& image, bool needSaveImg) {
    if (!pendingDir_.isEmpty()) {
        emit status(tr("目录加载中，稍后保存"), 900);
        return;
    }

    QString imgPath = currentImagePath_;
    if (imgPath.isEmpty()) {
        if (!proxyCurrent_.isValid() || proxyCurrent_.model() != proxy_) {
            emit status(tr("未选中图片"), 900);
            return;
        }
        const QModelIndex s = mapFromProxyToSource(proxyCurrent_);
        if (!s.isValid() || fsModel_->isDir(s)) {
            emit status(tr("未选中图片"), 900);
            return;
        }
        imgPath = fsModel_->filePath(s);
    }

    // 获取图片尺寸（优先用已缓存尺寸；为空则从文件探测）
    QSize sz = currentImageSize_;
    if (sz.isEmpty()) {
        QImageReader rr(imgPath);
        sz = rr.size();
        if (sz.isEmpty()) {
            emit status(tr("无法获取图片尺寸"), 1200);
            return;
        }
    }
    // 保存图片
    if (needSaveImg) {
        if (image.save(imgPath)) {
            emit status(tr("已保存图片：%1").arg(QFileInfo(imgPath).fileName()), 900);
            LOGI(QString("保存图片：%1").arg(imgPath));
        } else {
            emit status(tr("保存图片失败"), 1200);
            LOGE(QString("保存图片失败：%1").arg(imgPath));
            if (conflictMode_) {
                QMessageBox::warning(
                    QApplication::activeWindow(), tr("冲突样本保存失败"),
                    tr("无法保存修改后的图片，请修复存储问题后重试。"));
                return;
            }
        }
    }
    // 保存标注
    QString lblPath = conflictMode_ ? stageLabelForImage(imgPath) : labelFileForImage(imgPath);
    if (conflictMode_) {
        QString error;
        if (!labelmaster::service::label_format::writeLabelFile(
                lblPath, sz, LabelOutputFormat::LabelMasterV6, armors, &error)) {
            QMessageBox::warning(
                QApplication::activeWindow(), tr("冲突标签仍不合法"),
                tr("无法保存为 LabelMaster V6：%1\n\n请继续修改标注。").arg(error));
            emit status(tr("冲突标签未通过 V6 校验，请继续修改"), 5000);
            return;
        }
        QVector<Armor> verification;
        if (!labelmaster::service::label_format::readLabelFile(
                lblPath, sz, DataSet::LabelMasterV6, verification, &error)) {
            QMessageBox::warning(
                QApplication::activeWindow(), tr("冲突标签仍不合法"),
                tr("保存后的文件未通过 LabelMaster V6 校验：%1\n\n请继续修改标注。").arg(error));
            emit status(tr("冲突标签未通过 V6 校验，请继续修改"), 5000);
            return;
        }
        if (!restoreCurrentConflict(false, error)) {
            QMessageBox::critical(QApplication::activeWindow(), tr("回迁失败"), error);
            emit status(tr("标签已通过 V6 校验，但回迁原目录失败"), 5000);
            return;
        }
        emit status(tr("冲突标签已通过 V6 校验并放回原数据集"), 5000);
        refreshConflictDirectory();
        return;
    }
    if (writeLabelFile(lblPath, armors, sz)) {
        emit status(tr("已保存标注：%1").arg(QFileInfo(lblPath).fileName()), 900);
        LOGI(QString("保存标注：%1").arg(lblPath));
    } else {
        emit status(tr("保存失败"), 1200);
        LOGE(QString("保存失败：%1").arg(lblPath));
    }
}
// 获取统计数据
void FileService::getStas(int colorId, int classId, int sizeId) {
    // 开始统计
    int targetCount    = 0;
    int fileCount      = 0;
    QModelIndex parent = proxyCurrent_.parent();
    for (int i = 0; i < proxy_->rowCount(parent); i++) {
        int hasTarget   = false;
        QString imgPath = fsModel_->filePath(fsModel_->index(i, 0, mapFromProxyToSource(parent)));
        const QString labelPath = labelFileForImage(imgPath);
        if (QFile::exists(labelPath)) {
            QImageReader reader(imgPath);
            const QVector<Armor> armors = readLabelFile(labelPath, reader.size(), currentDataSet);
            for (const Armor& armor : armors) {
                const int colId = IdConvert::colorLetter2Id(armor.color);
                const int sId   = armor.size;
                const int clsId =
                    IdConvert::classToken2Id(IdConvert::normalizeClasslToken(armor.cls));
                const auto matches = [](int value, int target) {
                    return target == -1 || value == target;
                };
                if (matches(colId, colorId) && matches(sId, sizeId) && matches(clsId, classId)) {
                    ++targetCount;
                    hasTarget = true;
                }
            }
            if (hasTarget)
                ++fileCount;
            continue;

            QFile file(labelPath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream ts(&file);
                while (!ts.atEnd()) {
                    QStringList t;
                    if (!StringProcess::processLabelString(ts.readLine(), t)) {
                        continue;
                    }
                    bool ok   = false;
                    int colId = t.at(0).toInt(&ok);
                    int sId   = t.at(1).toInt(&ok);
                    int clsId = t.at(2).toInt(&ok);
                    if (!ok) {
                        continue;
                    }
                    auto checkId = [&](const int& value, const int& target) {
                        if (target == -1) {
                            return true;
                        } else {
                            if (value == target) {
                                return true;
                            }
                        }
                        return false;
                    };
                    if (checkId(colId, colorId) && checkId(sId, sizeId)
                        && checkId(clsId, classId)) {
                        targetCount++;
                        hasTarget = true;
                    }
                }
                if (hasTarget) {
                    fileCount++;
                }
            }
        }
    }
    emit StasGetted(targetCount, fileCount);
}
