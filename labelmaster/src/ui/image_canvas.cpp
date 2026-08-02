#include "image_canvas.hpp"
#include "../util/bridge.hpp"
#include "../util/id_convert.hpp"
#include "../util/keyboard_shortcuts.hpp"
#include "../util/svg_constants.hpp"
#include "controller/settings.hpp"
#include "dataset/dataset.h"
#include "info_dialog.h"
#include <QDebug>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSvgRenderer>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <opencv2/core.hpp>
#include <opencv2/core/mat.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <qbrush.h>
#include <qcolor.h>
#include <qdebug.h>
#include <qeventloop.h>
#include <qglobal.h>
#include <qhash.h>
#include <qimage.h>
#include <qinputdialog.h>
#include <qlist.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpainter.h>
#include <qpen.h>
#include <qpoint.h>
#include <qsize.h>
#include <qtmetamacros.h>
#include <qvariant.h>

// ---------- JSON 工具 ----------
static QJsonArray toJsonPt(const QPointF& p) { return QJsonArray{p.x(), p.y()}; }
static QPointF fromJsonPt(const QJsonArray& a) {
    return (a.size() == 2) ? QPointF(a.at(0).toDouble(), a.at(1).toDouble()) : QPointF{};
}
static QJsonObject armorToJson(const Armor& a) {
    QJsonObject o;
    o["cls"]                 = a.cls;
    o["color"]               = a.color;
    o["size"]                = a.size;
    o["keypoint_visibility"] = QJsonArray{
        a.keypointVisibility[0], a.keypointVisibility[1], a.keypointVisibility[2],
        a.keypointVisibility[3]};
    o["p0"]   = toJsonPt(a.p0);
    o["p1"]   = toJsonPt(a.p1);
    o["p2"]   = toJsonPt(a.p2);
    o["p3"]   = toJsonPt(a.p3);
    o["bbox"] = QJsonArray{a.norm_x, a.norm_y, a.norm_w, a.norm_h};
    return o;
}

static bool armorFromJson(const QJsonObject& o, Armor& a) {
    if (!o.contains("cls") || !o.contains("p0") || !o.contains("p1") || !o.contains("p2")
        || !o.contains("p3"))
        return false;
    a.cls                       = o.value("cls").toString();
    a.color                     = o.value("color").toString("G");
    a.size                      = o.value("size").toInt();
    const QJsonArray visibility = o.value("keypoint_visibility").toArray();
    if (visibility.size() == 4) {
        for (int i = 0; i < 4; ++i)
            a.keypointVisibility[i] = visibility[i].toInt(2);
    }
    a.p0                  = fromJsonPt(o.value("p0").toArray());
    a.p1                  = fromJsonPt(o.value("p1").toArray());
    a.p2                  = fromJsonPt(o.value("p2").toArray());
    a.p3                  = fromJsonPt(o.value("p3").toArray());
    const QJsonArray bbox = o.value("bbox").toArray();
    if (bbox.size() == 4) {
        a.norm_x = bbox[0].toDouble(-1.0);
        a.norm_y = bbox[1].toDouble(-1.0);
        a.norm_w = bbox[2].toDouble(-1.0);
        a.norm_h = bbox[3].toDouble(-1.0);
    }
    return true;
}

// ---------- 构造 ----------
ImageCanvas::ImageCanvas(QWidget* parent)
    : QLabel(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(100, 80);
    setContextMenuPolicy(Qt::NoContextMenu); // 防止右键被菜单吃掉
    setupSvg();                              // 初始化SVG

    visibilityShortcutTimer_ = new QTimer(this);
    visibilityShortcutTimer_->setSingleShot(true);
    connect(
        visibilityShortcutTimer_, &QTimer::timeout, this,
        &ImageCanvas::commitPendingVisibilityToggle);

    const auto publishAnnotations = [this] {
        emit annotationsChanged(dets_);
        if (!coalescingHistoryEdit_ && !restoringAnnotationHistory_)
            recordAnnotationHistory();
    };
    connect(
        this, static_cast<void (ImageCanvas::*)(int, const Armor&)>(&ImageCanvas::detectionUpdated),
        this, publishAnnotations);
    connect(
        this,
        static_cast<void (ImageCanvas::*)(QVector<int>, const Armor&)>(
            &ImageCanvas::detectionUpdated),
        this, publishAnnotations);
    connect(this, &ImageCanvas::detectionRemoved, this, publishAnnotations);

    resetAnnotationHistory();

    qRegisterMetaType<Armor>("ImageCanvas::Armor");
    qRegisterMetaType<QVector<Armor>>("QVector<ImageCanvas::Armor>");
}

/* ===== 图像 & 视图 ===== */

bool ImageCanvas::loadImage(const QString& path) {
    QImage tmp(path);
    if (tmp.isNull())
        return false;
    setImage(tmp);
    imgPath_ = path;
    return true;
}

void ImageCanvas::setImage(const QImage& img) {
    raw_img = img_ = img;
    imgPath_.clear();
    enhanceV_ = false;
    // 清空Masks
    clearMasks();
    // 切图即清空标注
    clearDetections();
    selectedIndex_       = -1;
    hoverIndex_          = -1;
    draggingRect_        = false;
    dragHandle_          = -1;
    hoverHandle_         = -1;
    dragBBoxHandle_      = -1;
    hoverBBoxHandle_     = -1;
    bboxDragOppositeImg_ = {};
    dragRectImg_         = QRect();

    if (!img_.isNull() && modelInputSize_.isValid() && modelInputSize_ == img_.size()) {
        roiImg_ = QRect(QPoint(0, 0), img_.size());
        emit roiChanged(roiImg_);
        emit roiCommitted(roiImg_);
    } else {
        clearRoi();
    }
    resetView();
    if (controller::AppSettings::instance().autoEnhanceV()) { // 切换图片时自动增强亮度
        histEqualize();
    }
    update();
}

void ImageCanvas::setModelInputSize(const QSize& s) {
    modelInputSize_ = s.isValid() ? s : QSize();
    if (!img_.isNull() && modelInputSize_.isValid() && modelInputSize_ == img_.size()) {
        roiImg_ = QRect(QPoint(0, 0), img_.size());
        emit roiChanged(roiImg_);
        emit roiCommitted(roiImg_);
        update();
    }
}

void ImageCanvas::setRoiMode(RoiMode m) {
    roiMode_ = m;
    if (roiMode_ == RoiMode::FixedToModelSize && !modelInputSize_.isValid())
        roiMode_ = RoiMode::Free;
    update();
}

void ImageCanvas::clearRoi() {
    roiImg_      = QRect();
    draggingRoi_ = false;
    emit roiChanged(roiImg_);
    update();
}

QImage ImageCanvas::cropRoi() const {
    if (img_.isNull() || roiImg_.isNull())
        return {};
    return img_.copy(clampRectToImage(roiImg_));
}

void ImageCanvas::resetView() {
    scale_ = 1.0;
    pan_   = {0, 0};
    updateFitRect();
}

/* ===== 检测请求 ===== */
void ImageCanvas::requestDetect() {
    const QImage crop = cropRoi();
    if (!crop.isNull()) {
        emit detectRequested(crop);
    } else {
        // 绘制Masks
        QImage temp_img = img_.copy();
        QPainter p(&temp_img);
        drawMasks(maskRects_, p, false);
        emit detectRequested(temp_img);
    }
}

/* ===== 外部读写 ===== */
void ImageCanvas::setDetections(const QVector<Armor>& dets) {
    replaceDetections(dets, false);
    recordAnnotationHistory();
    emit annotationsChanged(dets_);
}

void ImageCanvas::loadDetections(const QVector<Armor>& dets) { replaceDetections(dets, true); }

void ImageCanvas::replaceDetections(const QVector<Armor>& dets, bool resetHistory) {
    // qDebug() << "setDetections: " << dets.size();
    cancelPendingVisibilityShortcut();
    dets_ = dets;
    if (bboxEditingSupported()) {
        for (Armor& armor : dets_) {
            if (armor.norm_w < 0 || armor.norm_h < 0)
                updateBBoxFromCorners(armor);
        }
    }
    if (dets_.isEmpty()) {
        // qDebug() << "setDetections: empty";
        selectedIndex_ = -1;
    } else if (selectedIndex_ >= dets_.size())
        selectedIndex_ = dets_.size() - 1;
    if (hoverIndex_ >= dets_.size()) {
        hoverIndex_ = -1;
        emit detectionHovered(-1);
    }
    emit detectionSelected(selectedIndex_);
    update();
    if (resetHistory)
        resetAnnotationHistory();
}
void ImageCanvas::clearDetections() {
    cancelPendingVisibilityShortcut();
    dets_.clear();
    selectedIndex_ = -1;
    hoverIndex_    = -1;
    dragHandle_ = hoverHandle_ = -1;
    dragBBoxHandle_ = hoverBBoxHandle_ = -1;
    bboxDragOppositeImg_               = {};
    emit detectionSelected(-1);
    emit detectionHovered(-1);
    update();
    resetAnnotationHistory();
}

void ImageCanvas::resetAnnotationHistory() {
    annotationHistory_.clear();
    annotationHistory_.append(dets_);
    annotationHistoryIndex_     = 0;
    restoringAnnotationHistory_ = false;
    coalescingHistoryEdit_      = false;
    emit historyAvailabilityChanged(false, false);
}

void ImageCanvas::recordAnnotationHistory() {
    if (restoringAnnotationHistory_)
        return;
    if (annotationHistoryIndex_ >= 0 && annotationHistoryIndex_ < annotationHistory_.size()
        && annotationHistory_[annotationHistoryIndex_] == dets_) {
        return;
    }

    if (annotationHistoryIndex_ + 1 < annotationHistory_.size())
        annotationHistory_.resize(annotationHistoryIndex_ + 1);
    annotationHistory_.append(dets_);
    annotationHistoryIndex_ = annotationHistory_.size() - 1;

    if (annotationHistory_.size() > kMaxAnnotationHistory) {
        annotationHistory_.removeFirst();
        --annotationHistoryIndex_;
    }
    emit historyAvailabilityChanged(annotationHistoryIndex_ > 0, false);
}

void ImageCanvas::restoreAnnotationHistory(int index) {
    if (index < 0 || index >= annotationHistory_.size() || index == annotationHistoryIndex_)
        return;

    cancelPendingVisibilityShortcut();
    restoringAnnotationHistory_ = true;
    annotationHistoryIndex_     = index;
    dets_                       = annotationHistory_[index];
    if (dets_.isEmpty()) {
        selectedIndex_ = -1;
    } else if (selectedIndex_ < 0 || selectedIndex_ >= dets_.size()) {
        selectedIndex_ = dets_.size() - 1;
    }
    hoverIndex_ = -1;
    dragHandle_ = hoverHandle_ = -1;
    dragBBoxHandle_ = hoverBBoxHandle_ = -1;
    bboxDragOppositeImg_               = {};
    emit detectionSelected(selectedIndex_);
    emit detectionHovered(-1);
    emit annotationsChanged(dets_);
    update();
    restoringAnnotationHistory_ = false;
    emit historyAvailabilityChanged(
        annotationHistoryIndex_ > 0, annotationHistoryIndex_ + 1 < annotationHistory_.size());
}

void ImageCanvas::undo() {
    if (annotationHistoryIndex_ <= 0) {
        emit shortcutFeedback(tr("没有可撤销的标注编辑"));
        return;
    }
    restoreAnnotationHistory(annotationHistoryIndex_ - 1);
    emit shortcutFeedback(tr("已撤销标注编辑"));
}

void ImageCanvas::redo() {
    if (annotationHistoryIndex_ < 0 || annotationHistoryIndex_ + 1 >= annotationHistory_.size()) {
        emit shortcutFeedback(tr("没有可重做的标注编辑"));
        return;
    }
    restoreAnnotationHistory(annotationHistoryIndex_ + 1);
    emit shortcutFeedback(tr("已重做标注编辑"));
}
void ImageCanvas::addDetection(const Armor& a0) {
    Armor a = a0;
    if (a.norm_w < 0 || a.norm_h < 0)
        updateBBoxFromCorners(a);
    dets_.append(a);
    const int idx = dets_.size() - 1;
    emit detectionUpdated(idx, dets_.back());
    update();
}
void ImageCanvas::updateDetection(int index, const Armor& a0) {
    if (index < 0 || index >= dets_.size())
        return;
    dets_[index] = a0;
    if (dets_[index].norm_w < 0 || dets_[index].norm_h < 0)
        updateBBoxFromCorners(dets_[index]);
    emit detectionUpdated(index, dets_[index]);
    update();
}
void ImageCanvas::removeDetection(int index) {
    if (index < 0 || index >= dets_.size())
        return;
    dets_.removeAt(index);
    emit detectionRemoved(index);

    if (dets_.isEmpty()) {
        selectedIndex_ = -1;
        hoverIndex_    = -1;
    } else {
        if (selectedIndex_ == index)
            selectedIndex_ = -1;
        else if (selectedIndex_ > index)
            selectedIndex_ -= 1;

        if (hoverIndex_ == index)
            hoverIndex_ = -1;
        else if (hoverIndex_ > index)
            hoverIndex_ -= 1;
    }
    emit detectionSelected(selectedIndex_);
    emit detectionHovered(hoverIndex_);
    update();
}

bool ImageCanvas::setSelectedIndex(int idx) {
    if (idx < -1 || idx >= dets_.size())
        return false;
    selectedIndex_ = idx;
    dragHandle_ = hoverHandle_ = -1;
    dragBBoxHandle_ = hoverBBoxHandle_ = -1;
    bboxDragOppositeImg_               = {};
    emit detectionSelected(selectedIndex_);
    update();
    return true;
}
bool ImageCanvas::setSelectedClass(const QString& cls) {
    if (selectedIndex_ < 0 || selectedIndex_ >= dets_.size())
        return false;
    dets_[selectedIndex_].cls = cls.isEmpty() ? QStringLiteral("unknown") : cls;
    emit detectionUpdated(selectedIndex_, dets_[selectedIndex_]);
    update();
    return true;
}

bool ImageCanvas::selectDetectionInDirection(int key) {
    if (dets_.isEmpty()) {
        emit shortcutFeedback(tr("当前图片没有 Detector"));
        return true;
    }

    const auto centerOf = [](const Armor& armor) {
        return (armor.p0 + armor.p1 + armor.p2 + armor.p3) / 4.0;
    };

    // 尚未选中时，四个方向键都从最靠上的目标开始，同一高度取最靠左者。
    if (selectedIndex_ < 0 || selectedIndex_ >= dets_.size()) {
        int topLeftIndex         = 0;
        QPointF topLeft          = centerOf(dets_.front());
        constexpr double epsilon = 0.001;
        for (int index = 1; index < dets_.size(); ++index) {
            const QPointF candidate = centerOf(dets_[index]);
            if (candidate.y() < topLeft.y() - epsilon
                || (std::abs(candidate.y() - topLeft.y()) <= epsilon
                    && candidate.x() < topLeft.x())) {
                topLeftIndex = index;
                topLeft      = candidate;
            }
        }
        setSelectedIndex(topLeftIndex);
        emit shortcutFeedback(tr("已选中左上角 Detector"));
        return true;
    }

    const QPointF origin = centerOf(dets_[selectedIndex_]);
    int bestIndex        = -1;
    double bestScore     = std::numeric_limits<double>::max();
    for (int index = 0; index < dets_.size(); ++index) {
        if (index == selectedIndex_)
            continue;
        const QPointF delta  = centerOf(dets_[index]) - origin;
        double primary       = 0.0;
        double perpendicular = 0.0;
        switch (key) {
        case Qt::Key_W:
            primary       = -delta.y();
            perpendicular = std::abs(delta.x());
            break;
        case Qt::Key_A:
            primary       = -delta.x();
            perpendicular = std::abs(delta.y());
            break;
        case Qt::Key_S:
            primary       = delta.y();
            perpendicular = std::abs(delta.x());
            break;
        case Qt::Key_D:
            primary       = delta.x();
            perpendicular = std::abs(delta.y());
            break;
        default: return false;
        }
        if (primary <= 0.0)
            continue;

        // 欧氏距离保证选择附近目标，轻微惩罚横向偏移以优先同一行/列。
        const double score = std::hypot(delta.x(), delta.y()) + perpendicular * 0.25;
        if (score < bestScore) {
            bestScore = score;
            bestIndex = index;
        }
    }

    if (bestIndex >= 0) {
        setSelectedIndex(bestIndex);
        emit shortcutFeedback(tr("已切换选中的 Detector"));
    } else {
        emit shortcutFeedback(tr("该方向没有其他 Detector"));
    }
    return true;
}

int ImageCanvas::visibilityPointForKey(int key, Qt::KeyboardModifiers modifiers) const {
    using labelmaster::util::KeyboardAction;
    const auto& keyboard = labelmaster::util::KeyboardManager::instance();
    // Armor 内部顺序：TL, BL, BR, TR；默认键位依次为 F/G/C/V。
    if (keyboard.matches(KeyboardAction::VisibilityTopLeft, key, modifiers))
        return 0;
    if (keyboard.matches(KeyboardAction::VisibilityTopRight, key, modifiers))
        return 3;
    if (keyboard.matches(KeyboardAction::VisibilityBottomLeft, key, modifiers))
        return 1;
    if (keyboard.matches(KeyboardAction::VisibilityBottomRight, key, modifiers))
        return 2;
    return -1;
}

void ImageCanvas::setKeypointVisibility(
    int detectionIndex, int pointIndex, int visibility, const QString& action) {
    if (detectionIndex < 0 || detectionIndex >= dets_.size() || pointIndex < 0 || pointIndex > 3)
        return;

    static const std::array<QString, 4> pointNames{
        tr("左上角"), tr("左下角"), tr("右下角"), tr("右上角")};
    Armor& armor                         = dets_[detectionIndex];
    armor.keypointVisibility[pointIndex] = visibility;
    emit detectionUpdated(detectionIndex, armor);
    emit shortcutFeedback(tr("%1已设置为%2").arg(pointNames[pointIndex], action));
    update();
}

void ImageCanvas::commitPendingVisibilityToggle() {
    if (!visibilityShortcutTimer_ || pendingVisibilityPointIndex_ < 0)
        return;

    visibilityShortcutTimer_->stop();
    const int detectionIndex         = pendingVisibilityDetectionIndex_;
    const int pointIndex             = pendingVisibilityPointIndex_;
    pendingVisibilityDetectionIndex_ = -1;
    pendingVisibilityPointIndex_     = -1;
    if (detectionIndex < 0 || detectionIndex >= dets_.size())
        return;

    const int current = dets_[detectionIndex].keypointVisibility[pointIndex];
    const int next    = current == 2 ? 0 : 2;
    setKeypointVisibility(detectionIndex, pointIndex, next, next == 2 ? tr("可见") : tr("不可见"));
}

void ImageCanvas::cancelPendingVisibilityShortcut() {
    if (visibilityShortcutTimer_)
        visibilityShortcutTimer_->stop();
    pendingVisibilityDetectionIndex_ = -1;
    pendingVisibilityPointIndex_     = -1;
}

bool ImageCanvas::handleVisibilityShortcut(int pointIndex) {
    if (selectedIndex_ < 0 || selectedIndex_ >= dets_.size()) {
        commitPendingVisibilityToggle();
        emit shortcutFeedback(tr("请先选中一个 Detector"));
        return true;
    }

    if (visibilityShortcutTimer_ && visibilityShortcutTimer_->isActive()
        && pendingVisibilityDetectionIndex_ == selectedIndex_
        && pendingVisibilityPointIndex_ == pointIndex) {
        visibilityShortcutTimer_->stop();
        const int detectionIndex         = pendingVisibilityDetectionIndex_;
        pendingVisibilityDetectionIndex_ = -1;
        pendingVisibilityPointIndex_     = -1;
        setKeypointVisibility(detectionIndex, pointIndex, 1, tr("不在范围内"));
        return true;
    }

    // 按下了另一个可见性键时，先提交前一个键的单按操作。
    commitPendingVisibilityToggle();
    pendingVisibilityDetectionIndex_ = selectedIndex_;
    pendingVisibilityPointIndex_     = pointIndex;
    visibilityShortcutTimer_->start(QApplication::doubleClickInterval());
    return true;
}

bool ImageCanvas::handleEditorShortcut(int key, Qt::KeyboardModifiers modifiers) {
    using labelmaster::util::KeyboardAction;
    const auto& keyboard      = labelmaster::util::KeyboardManager::instance();
    const int visibilityPoint = visibilityPointForKey(key, modifiers);

    if (visibilityPoint >= 0)
        return handleVisibilityShortcut(visibilityPoint);

    // 任意其他按键都会结束上一可见性键的双按等待，并落实为单按。
    commitPendingVisibilityToggle();

    if (keyboard.matches(KeyboardAction::CancelCanvas, key, modifiers)) {
        draggingRect_        = false;
        dragHandle_          = -1;
        hoverHandle_         = -1;
        dragBBoxHandle_      = -1;
        hoverBBoxHandle_     = -1;
        bboxDragOppositeImg_ = {};
        if (coalescingHistoryEdit_) {
            coalescingHistoryEdit_ = false;
            recordAnnotationHistory();
        }
        update();
        return true;
    }

    if (keyboard.matches(KeyboardAction::EditSelected, key, modifiers)) {
        promptEditSelectedInfo();
        return true;
    }

    if (keyboard.matches(KeyboardAction::SelectUp, key, modifiers))
        return selectDetectionInDirection(Qt::Key_W);
    if (keyboard.matches(KeyboardAction::SelectLeft, key, modifiers))
        return selectDetectionInDirection(Qt::Key_A);
    if (keyboard.matches(KeyboardAction::SelectDown, key, modifiers))
        return selectDetectionInDirection(Qt::Key_S);
    if (keyboard.matches(KeyboardAction::SelectRight, key, modifiers))
        return selectDetectionInDirection(Qt::Key_D);

    QString color;
    QString colorName;
    QString cls;
    QString className;
    if (keyboard.matches(KeyboardAction::ColorRed, key, modifiers)) {
        color     = QStringLiteral("R");
        colorName = QStringLiteral("Red");
    } else if (keyboard.matches(KeyboardAction::ColorGray, key, modifiers)) {
        color     = QStringLiteral("G");
        colorName = QStringLiteral("Gray");
    } else if (keyboard.matches(KeyboardAction::ColorBlue, key, modifiers)) {
        color     = QStringLiteral("B");
        colorName = QStringLiteral("Blue");
    } else if (keyboard.matches(KeyboardAction::ColorPurple, key, modifiers)) {
        color     = QStringLiteral("P");
        colorName = QStringLiteral("Purple");
    } else if (keyboard.matches(KeyboardAction::ClassSentry, key, modifiers)) {
        cls       = QStringLiteral("G");
        className = QStringLiteral("G");
    } else if (keyboard.matches(KeyboardAction::Class1, key, modifiers)) {
        cls = className = QStringLiteral("1");
    } else if (keyboard.matches(KeyboardAction::Class2, key, modifiers)) {
        cls = className = QStringLiteral("2");
    } else if (keyboard.matches(KeyboardAction::Class3, key, modifiers)) {
        cls = className = QStringLiteral("3");
    } else if (keyboard.matches(KeyboardAction::Class4, key, modifiers)) {
        cls = className = QStringLiteral("4");
    } else if (keyboard.matches(KeyboardAction::Class5, key, modifiers)) {
        cls = className = QStringLiteral("5");
    } else if (keyboard.matches(KeyboardAction::ClassOutpost, key, modifiers)) {
        cls       = QStringLiteral("O");
        className = QStringLiteral("Outpost");
    } else if (keyboard.matches(KeyboardAction::ClassBase, key, modifiers)) {
        cls       = QStringLiteral("B");
        className = QStringLiteral("Base");
    }

    const bool setBig   = keyboard.matches(KeyboardAction::SizeBig, key, modifiers);
    const bool setSmall = keyboard.matches(KeyboardAction::SizeSmall, key, modifiers);
    if (color.isEmpty() && cls.isEmpty() && !setBig && !setSmall)
        return false;

    if (selectedIndex_ < 0 || selectedIndex_ >= dets_.size()) {
        emit shortcutFeedback(tr("请先选中一个 Detector"));
        return true;
    }

    Armor& selected = dets_[selectedIndex_];
    QString feedback;
    if (!color.isEmpty()) {
        selected.color = color;
        feedback       = tr("颜色已设置为 %1").arg(colorName);
    } else if (!cls.isEmpty()) {
        selected.cls = cls;
        feedback     = tr("Class 已设置为 %1").arg(className);
    } else {
        selected.size = setBig ? 1 : 0;
        updateBBoxFromCorners(selected);
        feedback = setBig ? tr("大小已设置为 Big") : tr("大小已设置为 Small");
    }
    emit detectionUpdated(selectedIndex_, selected);
    emit shortcutFeedback(feedback);
    update();
    return true;
}

bool ImageCanvas::setSelectedInfo(
    const QString& cls, const QString& color, const int& size, int vis0, int vis1, int vis2,
    int vis3) {
    if (selectedIndex_ < 0 || selectedIndex_ >= dets_.size())
        return false;
    dets_[selectedIndex_].size               = size;
    dets_[selectedIndex_].color              = color.isEmpty() ? "Gray" : color;
    dets_[selectedIndex_].cls                = cls.isEmpty() ? QStringLiteral("unknown") : cls;
    dets_[selectedIndex_].keypointVisibility = {vis0, vis1, vis2, vis3};
    // 尺寸变化会影响BBox计算（不同尺寸使用不同SVG锚点），重新计算
    updateBBoxFromCorners(dets_[selectedIndex_]);
    emit detectionUpdated(selectedIndex_, dets_[selectedIndex_]);
    update();
    return true;
}

/* ===== BBox计算 ===== */
void ImageCanvas::updateBBoxFromCorners(Armor& a) const {
    // 使用SVG透视变换计算归一化边界框
    // 逻辑与 file.cpp 中的 writeLabelFile() 完全相同
    const double W = double(img_.width());
    const double H = double(img_.height());

    // SVG固有尺寸和锚点 - 使用集中管理的常量
    const auto& svgTemplate = (a.size == 0) ? labelmaster::util::SvgConstants::smallArmor()
                                            : labelmaster::util::SvgConstants::bigArmor();

    // SVG外框四个角 (TL, BL, BR, TR)
    QPolygonF svg_quad;
    svg_quad << QPointF(0., 0.) << QPointF(0., svgTemplate.height)
             << QPointF(svgTemplate.width, svgTemplate.height) << QPointF(svgTemplate.width, 0.);

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
        if (W > 0 && H > 0) {
            a.norm_w = (max_x - min_x) / W;
            a.norm_h = (max_y - min_y) / H;
            a.norm_x = (min_x + max_x) / (2.0 * W);
            a.norm_y = (min_y + max_y) / (2.0 * H);
        }
    } else {
        // 透视变换失败，使用锚点的边界框作为后备
        double min_x = std::min({a.p0.x(), a.p1.x(), a.p2.x(), a.p3.x()});
        double min_y = std::min({a.p0.y(), a.p1.y(), a.p2.y(), a.p3.y()});
        double max_x = std::max({a.p0.x(), a.p1.x(), a.p2.x(), a.p3.x()});
        double max_y = std::max({a.p0.y(), a.p1.y(), a.p2.y(), a.p3.y()});
        if (W > 0 && H > 0) {
            a.norm_w = (max_x - min_x) / W;
            a.norm_h = (max_y - min_y) / H;
            a.norm_x = (min_x + max_x) / (2.0 * W);
            a.norm_y = (min_y + max_y) / (2.0 * H);
        }
    }
}

bool ImageCanvas::bboxEditingSupported() const {
    const int configured = controller::AppSettings::instance().outputFormat();
    if (configured < static_cast<int>(LabelOutputFormat::Points11)
        || configured > static_cast<int>(LabelOutputFormat::LabelMasterV6)) {
        return false;
    }
    return supportsBoundingBox(static_cast<LabelOutputFormat>(configured));
}

std::array<QPointF, 4> ImageCanvas::bboxCornersInImage(const Armor& armor) const {
    const double cx    = armor.norm_x * img_.width();
    const double cy    = armor.norm_y * img_.height();
    const double halfW = armor.norm_w * img_.width() / 2.0;
    const double halfH = armor.norm_h * img_.height() / 2.0;
    return {
        QPointF(cx - halfW, cy - halfH),
        QPointF(cx - halfW, cy + halfH),
        QPointF(cx + halfW, cy + halfH),
        QPointF(cx + halfW, cy - halfH),
    };
}

/* ===== 导入/导出 ===== */

/* ===== 绘制 ===== */
void ImageCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (img_.isNull())
        return;

    const QRectF R = imageRectOnWidget();
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(R, img_);
    drawMasks(maskRects_, p); // <<< 新增：绘制Mask
    drawDetections(p);
    drawRoi(p);
    drawSvg(p, dets_);
    drawDragRect(p);          // <<< 新增：拖框时的虚线矩形
    drawCrosshair(p);         // 十字准心
}

void ImageCanvas::drawDragRect(QPainter& p) const {
    if (!(draggingRect_ && !dragRectImg_.isNull()))
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (isMaskMode)
        p.setClipRect(imageRectOnWidget());

    const QRect rw = QRect(
                         imageToWidget(dragRectImg_.topLeft()).toPoint(),
                         imageToWidget(dragRectImg_.bottomRight()).toPoint())
                         .normalized();

    p.setPen(QPen(Qt::green, 2, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rw);
    p.restore();
}
void ImageCanvas::drawMasks(const QVector<QRect> rects, QPainter& painter, bool isToWidget) const {
    painter.save();
    QPen pen;
    pen.setColor(QColorConstants::Black);
    pen.setWidth(1);
    QBrush brush;
    brush.setColor(QColorConstants::Black);
    brush.setStyle(Qt::SolidPattern);
    painter.setPen(pen);
    painter.setBrush(brush);
    if (isToWidget) {
        for (QRect rect : rects) {
            QPolygonF poly;
            poly << imageToWidget(rect.topLeft()) << imageToWidget(rect.bottomLeft())
                 << imageToWidget(rect.bottomRight()) << imageToWidget(rect.topRight());
            painter.drawPolygon(poly);
        }
    } else {
        painter.drawRects(rects);
    }
    painter.restore();
}
void ImageCanvas::clearMasks() { maskRects_.clear(); }

void ImageCanvas::drawDetections(QPainter& p) const {
    if (dets_.isEmpty())
        return;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    // 使用集中管理的颜色映射
    auto colorOf = [](const QString& c) -> QColor {
        return labelmaster::util::ColorMapper::colorForLetter(c);
    };

    // 除纯角点旧格式外，显示保存于标签中的 bbox。
    const int outputFormat     = controller::AppSettings::instance().outputFormat();
    const bool showRectOverlay = outputFormat != static_cast<int>(LabelOutputFormat::Points11);
    const double W             = img_.width();
    const double H             = img_.height();

    for (int i = 0; i < dets_.size(); ++i) {
        const auto& d = dets_[i];
        QPolygonF poly;
        poly << imageToWidget(d.p0) << imageToWidget(d.p1) << imageToWidget(d.p2)
             << imageToWidget(d.p3);

        const bool isSel   = (i == selectedIndex_);
        const bool isHover = (i == hoverIndex_);
        const QColor base  = colorOf(d.color);

        // 绘制SVG边界矩形 (当使用15字段格式时，优先使用文件中的BBox)
        if (showRectOverlay) {
            QRectF rectImg;

            // 优先使用文件中存储的归一化 bbox 值
            if (d.norm_w >= 0 && d.norm_h >= 0) {
                // 从归一化坐标转换为像素坐标
                double cx = d.norm_x * W;
                double cy = d.norm_y * H;
                double w  = d.norm_w * W;
                double h  = d.norm_h * H;
                rectImg   = QRectF(cx - w / 2, cy - h / 2, w, h);
            } else {
                // 没有存储的 bbox，动态计算SVG透视变换后的真实边界框
                // SVG固有尺寸和锚点 - 使用集中管理的常量
                const auto& svgTemplate = (d.size == 0)
                                            ? labelmaster::util::SvgConstants::smallArmor()
                                            : labelmaster::util::SvgConstants::bigArmor();

                // SVG外框四个角 (TL, BL, BR, TR)
                QPolygonF svg_quad;
                svg_quad << QPointF(0., 0.) << QPointF(0., svgTemplate.height)
                         << QPointF(svgTemplate.width, svgTemplate.height)
                         << QPointF(svgTemplate.width, 0.);

                // 图像中的四个锚点
                QPolygonF img_anchors;
                img_anchors << d.p0 << d.p1 << d.p2 << d.p3;

                // 计算单应性矩阵并变换SVG外框
                QTransform transform;
                if (QTransform::quadToQuad(svgTemplate.anchors, img_anchors, transform)) {
                    QPolygonF img_corners = transform.map(svg_quad);

                    // 计算边界框
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

                    rectImg = QRectF(min_x, min_y, max_x - min_x, max_y - min_y);
                }
            }

            // 转换为Widget坐标并绘制
            if (!rectImg.isEmpty()) {
                QRectF rectW =
                    QRectF(imageToWidget(rectImg.topLeft()), imageToWidget(rectImg.bottomRight()))
                        .normalized();

                QPen rectPen(base.lighter(150), 1, Qt::DashLine);
                p.setPen(rectPen);
                p.setBrush(Qt::NoBrush);
                p.drawRect(rectW);

                if (isSel && bboxEditingSupported()) {
                    const auto corners = bboxCornersInImage(d);
                    for (int k = 0; k < int(corners.size()); ++k) {
                        const QPointF w = imageToWidget(corners[k]);
                        const bool hot  = k == hoverBBoxHandle_ || k == dragBBoxHandle_;
                        p.setPen(QPen(hot ? Qt::yellow : base.lighter(150), 1));
                        p.setBrush(hot ? Qt::yellow : base.darker(115));
                        p.drawRect(QRectF(w.x() - 5, w.y() - 5, 10, 10));
                    }
                }
            }
        }

        // 叠加填充（选中/悬停）
        if (isSel || isHover) {
            p.setPen(Qt::NoPen);
            QColor fill = base;
            fill.setAlpha(isSel ? 60 : 45);
            p.setBrush(fill);
            p.drawPolygon(poly);
        }

        // 轮廓
        QPen pen = isSel ? QPen(base, 3) : isHover ? QPen(base.lighter(125), 3) : QPen(base, 2);
        pen.setJoinStyle(Qt::MiterJoin);
        pen.setCapStyle(Qt::SquareCap);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPolygon(poly);

        // 文本（描边 + 主色）- 添加 [R+P] 标记表示使用矩形+角点格式
        const QPointF tl   = poly.boundingRect().topLeft();
        const QString text = showRectOverlay ? QString("%1%2 [R+P]").arg(d.color).arg(d.cls)
                                             : QString("%1%2").arg(d.color).arg(d.cls);
        QFont f            = p.font();
        f.setPointSizeF(f.pointSizeF() + 1);
        p.setFont(f);
        p.setPen(QPen(Qt::black, 4));         // 外描边
        p.drawText(tl + QPointF(2, -2), text);
        p.setPen(QPen(base.lighter(120), 1)); // 主色文字
        p.drawText(tl + QPointF(2, -2), text);

        // 选中时角点
        if (isSel) {
            for (int k = 0; k < 4; ++k) {
                const QPointF w = imageToWidget(
                    k == 0   ? d.p0
                    : k == 1 ? d.p1
                    : k == 2 ? d.p2
                             : d.p3);
                const bool hot = (k == hoverHandle_ || k == dragHandle_);
                QColor c       = hot ? base.lighter(120) : base;
                if (d.keypointVisibility[k] != 2) {
                    p.setPen(QPen(c, 2));
                    p.setBrush(Qt::NoBrush);
                } else {
                    p.setPen(Qt::NoPen);
                    p.setBrush(c);
                }
                p.drawEllipse(w, kHandleRadius_, kHandleRadius_);
            }
        }
    }
    p.restore();
}

void ImageCanvas::drawRoi(QPainter& p) const {
    if (roiImg_.isNull())
        return;
    const QRect rw = QRect(
                         imageToWidget(roiImg_.topLeft()).toPoint(),
                         imageToWidget(roiImg_.bottomRight()).toPoint())
                         .normalized();

    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 100));
    QPainterPath path;
    path.addRect(rect());
    QPainterPath hole;
    hole.addRect(rw);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.drawPath(path - hole);
    p.restore();

    p.setPen(QPen(Qt::yellow, 2));
    p.drawRect(rw);
    p.setPen(Qt::white);
    p.drawText(
        rw.adjusted(4, 4, -4, -4), Qt::AlignLeft | Qt::AlignTop,
        QString("%1×%2").arg(roiImg_.width()).arg(roiImg_.height()));
}

void ImageCanvas::drawCrosshair(QPainter& p) const {
    if (!mouseInside_ || img_.isNull())
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setClipRect(rect());
    p.setPen(QPen(QColor(0, 255, 0, 180), 1));
    p.drawLine(QPoint(mousePosW_.x(), rect().top()), QPoint(mousePosW_.x(), rect().bottom()));
    p.drawLine(QPoint(rect().left(), mousePosW_.y()), QPoint(rect().right(), mousePosW_.y()));
    p.restore();
}
// 直方图均衡化
void ImageCanvas::histEqualize() {
    if (enhanceV_) {
        img_ = raw_img;
    } else {
        cv::Mat res = qimageToMat(raw_img);
        cv::Mat channels[3];
        // 像素值
        //  res.convertTo(res, -1, 2.2, 50);
        // 直方图均衡化       cv::Mat channel[4];
        // cv::split(res, channels);
        // cv::equalizeHist(channels[0], channels[0]);
        // cv::equalizeHist(channels[1], channels[1]);
        // cv::equalizeHist(channels[2], channels[2]);
        // cv::merge(channels, 3, res);
        // cv::cvtColor(res, res, cv::COLOR_BGR2RGB);
        // 线性LUT函数
        auto createLookUpTable = []() {
            cv::Mat lookUpTable;
            lookUpTable.Mat::create(1, 256, CV_8UC1);
            for (int i = 0; i <= 255; i++) {
                if (i * controller::AppSettings::instance().vRate() > 255)
                    lookUpTable.at<uchar>(0, i) = 255;
                else
                    lookUpTable.at<uchar>(0, i) =
                        uchar(round(controller::AppSettings::instance().vRate() * i));
            }
            return lookUpTable;
        };
        cv::cvtColor(res, res, cv::COLOR_BGR2HSV);
        cv::split(res, channels);
        cv::LUT(channels[2], createLookUpTable(), channels[2]);
        cv::merge(channels, 3, res);
        cv::cvtColor(res, res, cv::COLOR_HSV2RGB);
        // 伽马矫正(非线性LUT函数)
        // auto createGammaLookUpTable = [](double gamma) {
        //     cv::Mat lookUpTable(1, 256, CV_8U);
        //     uchar* p = lookUpTable.ptr();
        //     for (int i = 0; i < 256; ++i) {
        //         // I_out = 255 * (I_in / 255)^gamma
        //         p[i] = cv::saturate_cast<uchar>(pow(i / 255.0, gamma) * 255.0);
        //     }
        //     return lookUpTable;
        // };
        // double gamma        = 0.4;
        // cv::Mat lookUpTable = createGammaLookUpTable(gamma);
        // // 使用 LUT 函数进行伽马校正，并将结果存储在 res 中
        // LUT(res, lookUpTable, res);
        // cv::cvtColor(res, res, cv::COLOR_BGR2RGB);
        img_ = QImage(res.data, res.cols, res.rows, res.step, QImage::Format_RGB888).copy();
    }
    enhanceV_ = !enhanceV_;
    update();
}
/* ===== 交互 ===== */
void ImageCanvas::wheelEvent(QWheelEvent* e) {
    if (img_.isNull()) {
        e->accept();
        return;
    }
    const QPointF cursorW = e->position();
    const QPointF beforeI = widgetToImage(cursorW);
    const double step     = e->angleDelta().y() > 0 ? 1.15 : (1.0 / 1.15);
    const double newScale = std::clamp(scale_ * step, kMinScale_, kMaxScale_);
    scale_                = newScale;
    const QPointF afterW  = imageToWidget(beforeI);
    pan_ += (cursorW - afterW);
    update();
    e->accept();
}

void ImageCanvas::mousePressEvent(QMouseEvent* e) {
    if (img_.isNull())
        return;
    lastMousePos_ = e->pos();
    mousePosW_    = e->pos();
    mouseInside_  = rect().contains(mousePosW_);
    if (e->button() == Qt::LeftButton) {
        if ((e->modifiers() & Qt::ControlModifier)) { // 绘制Mask
            // 绘制Mask不需要判断1, 2
            isMaskMode = true;

        } else {
            isMaskMode = false;
            // 1) 若有选中，优先检测角点拖动
            if (selectedIndex_ >= 0 && selectedIndex_ < dets_.size()) {
                hoverBBoxHandle_ = hitBBoxHandleOnSelected(e->pos());
                if (hoverBBoxHandle_ >= 0) {
                    dragBBoxHandle_        = hoverBBoxHandle_;
                    const auto corners     = bboxCornersInImage(dets_[selectedIndex_]);
                    bboxDragOppositeImg_   = corners[(dragBBoxHandle_ + 2) % 4];
                    coalescingHistoryEdit_ = true;
                    update();
                    return;
                }
                hoverHandle_ = hitHandleOnSelected(e->pos());
                if (hoverHandle_ >= 0) {
                    dragHandle_            = hoverHandle_;
                    coalescingHistoryEdit_ = true;
                    update();
                    return;
                }
            }

            // 2) 命中已有目标 → 选中，不画框
            const int hit = hitDetectionStrict(e->pos());
            if (hit >= 0) {
                if (selectedIndex_ != hit) {
                    selectedIndex_ = hit;
                    dragHandle_ = hoverHandle_ = -1;
                    dragBBoxHandle_ = hoverBBoxHandle_ = -1;
                    bboxDragOppositeImg_               = {};
                    emit detectionSelected(selectedIndex_);
                }
                update();
                return;
            }
        }
        // 3) 空白 → 开始画新框
        draggingRect_   = true;
        dragRectStartW_ = e->pos();
        const QPoint a  = widgetToImage(dragRectStartW_).toPoint();
        dragRectImg_    = QRect(a, a);
        update();
        return;
    } else if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        setCursor(Qt::ClosedHandCursor);
    } else if (e->button() == Qt::RightButton) {
        const int hit = hitDetectionStrict(e->pos()); // 命中Detection
        if (hit >= 0) {
            removeDetection(hit);
            update();
            return;
        }
        const int hitMask = hitMaskStrict(e->pos());  // 命中Mask
        if (hitMask >= 0) {
            maskRects_.removeAt(hitMask);
            update();
            return;
        }
        // 未命中：啥也不做（但确保没有遗留拖拽状态）
        draggingRect_        = false;
        dragHandle_          = -1;
        hoverHandle_         = -1;
        dragBBoxHandle_      = -1;
        hoverBBoxHandle_     = -1;
        bboxDragOppositeImg_ = {};
        update();
        return;
    }
}
void ImageCanvas::createNewDetection() { // 画框
    const QRect r = dragRectImg_.normalized();
    Armor a;
    a.p0                 = QPointF(r.left(), r.top());
    a.p1                 = QPointF(r.left(), r.bottom());
    a.p2                 = QPointF(r.right(), r.bottom());
    a.p3                 = QPointF(r.right(), r.top());
    a.cls                = currentClass_.isEmpty() ? QStringLiteral("unknown") : currentClass_;
    a.color              = currentColor_.isEmpty() ? QStringLiteral("G") : currentColor_;
    a.size               = currentSize_;
    a.keypointVisibility = currentKeypointVisibility_;
    currentColor_        = "";
    // 自动计算归一化BBox
    updateBBoxFromCorners(a);
    dets_.append(a);
    emit annotationCommitted(a);
    emit detectionUpdated(dets_.size() - 1, a);
    selectedIndex_ = dets_.size() - 1;
    emit detectionSelected(selectedIndex_);
    dragRectImg_ = QRect();
    update();
}
void ImageCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        // A. 结束画框 → 立刻新增
        if (draggingRect_) {
            draggingRect_ = false;
            if (!dragRectImg_.isNull()) {
                const QRect rawRect = dragRectImg_.normalized();
                // Mask 保持在图像内；装甲板 bbox 和关键点允许位于图像外。
                if (e->modifiers() & Qt::ControlModifier) {
                    const QRect r = clampRectToImage(rawRect);
                    if (r.width() >= 2 && r.height() >= 2) {
                        maskRects_.append(r); // 添加到Mask信息用于绘制
                        dragRectImg_ = QRect();
                    }
                } else if (rawRect.width() >= 2 && rawRect.height() >= 2) {
                    promptEditSelectedInfo(true);
                    // TL, BL, BR, TR  (CCW)
                }
            }
            return;
        }

        if (dragBBoxHandle_ >= 0) {
            dragBBoxHandle_        = -1;
            bboxDragOppositeImg_   = {};
            coalescingHistoryEdit_ = false;
            if (selectedIndex_ >= 0 && selectedIndex_ < dets_.size())
                emit detectionUpdated(selectedIndex_, dets_[selectedIndex_]);
            update();
            return;
        }

        if (dragHandle_ >= 0) {
            dragHandle_            = -1;
            coalescingHistoryEdit_ = false;
            if (selectedIndex_ >= 0 && selectedIndex_ < dets_.size()) {
                emit detectionUpdated(selectedIndex_, dets_[selectedIndex_]);
            }
            update();
            return;
        }

        if (draggingRoi_)
            endFreeRoi();
    } else if (e->button() == Qt::MiddleButton && panning_) {
        panning_ = false;
        setCursor(Qt::ArrowCursor);
    }
}

void ImageCanvas::mouseMoveEvent(QMouseEvent* e) {
    mousePosW_   = e->pos();
    mouseInside_ = rect().contains(mousePosW_);
    // 拖动图片
    if (panning_) {
        const QPoint d = e->pos() - lastMousePos_;
        pan_ += d;
        lastMousePos_ = e->pos();
        update();
        return;
    }
    // 绘制辅助框
    if (draggingRect_) {
        QPoint a     = widgetToImage(dragRectStartW_).toPoint();
        QPoint b     = widgetToImage(e->pos()).toPoint();
        dragRectImg_ = QRect(a, b).normalized();
        update();
        return;
    }
    // 拖动 bbox 方角柄；关键点保持不变。
    if (dragBBoxHandle_ >= 0 && selectedIndex_ >= 0 && selectedIndex_ < dets_.size()) {
        Armor& armor           = dets_[selectedIndex_];
        const QPointF opposite = bboxDragOppositeImg_;
        QPointF current        = widgetToImage(e->pos());
        switch (dragBBoxHandle_) {
        case 0:
            current.setX(std::min(current.x(), opposite.x() - 2.0));
            current.setY(std::min(current.y(), opposite.y() - 2.0));
            break;
        case 1:
            current.setX(std::min(current.x(), opposite.x() - 2.0));
            current.setY(std::max(current.y(), opposite.y() + 2.0));
            break;
        case 2:
            current.setX(std::max(current.x(), opposite.x() + 2.0));
            current.setY(std::max(current.y(), opposite.y() + 2.0));
            break;
        case 3:
            current.setX(std::max(current.x(), opposite.x() + 2.0));
            current.setY(std::min(current.y(), opposite.y() - 2.0));
            break;
        }
        const double minX   = std::min(opposite.x(), current.x());
        const double minY   = std::min(opposite.y(), current.y());
        const double maxX   = std::max(opposite.x(), current.x());
        const double maxY   = std::max(opposite.y(), current.y());
        const double width  = maxX - minX;
        const double height = maxY - minY;
        armor.norm_x        = (minX + maxX) / (2.0 * img_.width());
        armor.norm_y        = (minY + maxY) / (2.0 * img_.height());
        armor.norm_w        = width / img_.width();
        armor.norm_h        = height / img_.height();
        emit detectionUpdated(selectedIndex_, armor);
        update();
        return;
    }
    // 拖动角点
    if (dragHandle_ >= 0 && selectedIndex_ >= 0 && selectedIndex_ < dets_.size()) {
        auto& A                = dets_[selectedIndex_];
        const QPointF pi       = widgetToImage(e->pos());
        const auto ensureBound = [](int index) {
            index = index > 3 ? index - 4 : index;
            index = index < 0 ? 4 + index : index;
            return index;
        };
        const auto getPosByIndex = [&](const int& index) {
            switch (index) {
            case 0: return A.p0;
            case 1: return A.p1;
            case 2: return A.p2;
            default: return A.p3;
            }
        };
        const auto setPosByIndex = [&](const int& index, const QPointF& point) {
            switch (index) {
            case 0: A.p0 = point; break;
            case 1: A.p1 = point; break;
            case 2: A.p2 = point; break;
            default: A.p3 = point;
            }
        };
        if (e->modifiers() == Qt::KeyboardModifier::AltModifier) { // 平行绘制模式
            QPointF res;
            res.setY(pi.y()); // <-- 现在固定 y，根据平行约束求 x
            qreal tx;

            switch (dragHandle_) {
            case 0: {         // p0：左上 —— 和右边 p3->p2 平行
                const QPointF t = A.p3 - A.p2;
                if (qFuzzyIsNull(t.y())) {
                    // 平行线是垂直的，直接使用输入的x坐标
                    tx = pi.x();
                } else {
                    tx = A.p1.x() + (t.x() / t.y()) * (A.p0.y() - A.p1.y());
                }
                break;
            }
            case 1: { // p1：左下 —— 和右边 p3->p2 平行
                const QPointF t = A.p3 - A.p2;
                if (qFuzzyIsNull(t.y())) {
                    tx = pi.x();
                } else {
                    tx = A.p0.x() + (t.x() / t.y()) * (A.p1.y() - A.p0.y());
                }
                break;
            }
            case 2: { // p2：右下 —— 和左边 p0->p1 平行
                const QPointF t = A.p0 - A.p1;
                if (qFuzzyIsNull(t.y())) {
                    tx = pi.x();
                } else {
                    tx = A.p3.x() + (t.x() / t.y()) * (A.p2.y() - A.p3.y());
                }
                break;
            }
            case 3: { // p3：右上 —— 和左边 p0->p1 平行
                const QPointF t = A.p0 - A.p1;
                if (qFuzzyIsNull(t.y())) {
                    tx = pi.x();
                } else {
                    tx = A.p2.x() + (t.x() / t.y()) * (A.p3.y() - A.p2.y());
                }
                break;
            }
            }

            res.setX(tx);
            setPosByIndex(dragHandle_, res);
        } else if (e->modifiers() == Qt::KeyboardModifier::ShiftModifier) { // 平行四边形绘制模式
            int diagonP1       = dragHandle_ - 1;
            int diagonP2       = dragHandle_ + 1;
            int another        = dragHandle_ + 2;
            diagonP1           = ensureBound(diagonP1);
            diagonP2           = ensureBound(diagonP2);
            another            = ensureBound(another);
            QPointF anotherPos = getPosByIndex(diagonP1) + getPosByIndex(diagonP2) - pi;
            setPosByIndex(another, anotherPos);
            setPosByIndex(dragHandle_, pi);
        } else {
            setPosByIndex(dragHandle_, pi);
        }
        // 自动更新归一化BBox（使用SVG透视变换）
        updateBBoxFromCorners(A);
        // 不在移动中重排，避免把当前拖拽句柄"换角"
        emit detectionUpdated(selectedIndex_, dets_[selectedIndex_]);
        update();
        return;
    }

    // 仅选中时更新悬停角点
    if (selectedIndex_ >= 0 && selectedIndex_ < dets_.size()) {
        hoverBBoxHandle_ = hitBBoxHandleOnSelected(e->pos());
        hoverHandle_     = hoverBBoxHandle_ >= 0 ? -1 : hitHandleOnSelected(e->pos());
    } else {
        hoverHandle_     = -1;
        hoverBBoxHandle_ = -1;
    }

    // 悬停命中（最后）
    const int hitNow = hitDetectionStrict(e->pos());
    if (hitNow != hoverIndex_) {
        hoverIndex_ = hitNow;
        emit detectionHovered(hoverIndex_);
    }

    update(); // 始终刷新（十字线/轻微移动也更新）
}
// 编辑颜色和类别
void ImageCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton)
        return;
    const int hit = hitDetectionStrict(e->pos());
    if (hit >= 0) {
        setSelectedIndex(hit);
        promptEditSelectedInfo();
    }
}

void ImageCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->isAutoRepeat()) {
        e->ignore();
        return;
    }

    if (handleEditorShortcut(e->key(), e->modifiers())) {
        e->accept();
        return;
    }

    QLabel::keyPressEvent(e);
}

void ImageCanvas::leaveEvent(QEvent*) {
    mouseInside_ = false;
    if (hoverIndex_ != -1) {
        hoverIndex_ = -1;
        emit detectionHovered(-1);
    }
    update();
}

void ImageCanvas::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    updateFitRect();
    update();
}

/* ===== 几何 & 命中 ===== */
void ImageCanvas::updateFitRect() {
    if (img_.isNull()) {
        fitRect_ = QRectF();
        return;
    }
    const QSizeF W = size();
    QSizeF sc      = img_.size();
    sc.scale(W, Qt::KeepAspectRatio);
    const QPointF off((W.width() - sc.width()) / 2.0, (W.height() - sc.height()) / 2.0);
    fitRect_ = QRectF(off, sc);
}
QRectF ImageCanvas::imageRectOnWidget() const {
    if (img_.isNull())
        return {};
    const QPointF c = fitRect_.center();
    const QSizeF s  = fitRect_.size() * scale_;
    QRectF r(QPointF(0, 0), s);
    r.moveCenter(c + pan_);
    return r;
}
QPointF ImageCanvas::widgetToImage(const QPointF& p) const {
    const QRectF R = imageRectOnWidget();
    if (img_.isNull() || R.isEmpty())
        return {};
    const double sx = img_.width() / R.width(), sy = img_.height() / R.height();
    return QPointF((p.x() - R.x()) * sx, (p.y() - R.y()) * sy);
}
QPointF ImageCanvas::imageToWidget(const QPointF& p) const {
    const QRectF R = imageRectOnWidget();
    if (img_.isNull() || R.isEmpty())
        return {};
    const double sx = R.width() / img_.width(), sy = R.height() / img_.height();
    return QPointF(R.x() + p.x() * sx, R.y() + p.y() * sy);
}
QRect ImageCanvas::widgetRectToImageRect(const QRect& rw) const {
    const QPointF tl = widgetToImage(rw.topLeft());
    const QPointF br = widgetToImage(rw.bottomRight());
    QRect r          = QRect(tl.toPoint(), br.toPoint()).normalized();
    return clampRectToImage(r);
}
QRect ImageCanvas::clampRectToImage(const QRect& r) const {
    if (img_.isNull())
        return {};
    return r.intersected(QRect(0, 0, img_.width(), img_.height()));
}

// 仅在“选中目标”上测试角点命中
int ImageCanvas::hitBBoxHandleOnSelected(const QPoint& wpos) const {
    if (!bboxEditingSupported() || selectedIndex_ < 0 || selectedIndex_ >= dets_.size())
        return -1;
    const Armor& armor = dets_[selectedIndex_];
    if (armor.norm_w < 0 || armor.norm_h < 0)
        return -1;
    const auto corners = bboxCornersInImage(armor);
    for (int i = 0; i < int(corners.size()); ++i) {
        if (QLineF(imageToWidget(corners[i]), wpos).length() <= kHandleRadius_ * 1.8)
            return i;
    }
    return -1;
}

int ImageCanvas::hitHandleOnSelected(const QPoint& wpos) const {
    if (selectedIndex_ < 0 || selectedIndex_ >= dets_.size())
        return -1;
    const auto& A = dets_[selectedIndex_];
    const std::array<QPointF, 4> pts{A.p0, A.p1, A.p2, A.p3};
    for (int i = 0; i < 4; ++i) {
        if (QLineF(imageToWidget(pts[i]), wpos).length() <= kHandleRadius_ * 1.6)
            return i;
    }
    return -1;
}

int ImageCanvas::hitDetectionStrict(const QPoint& wpos) const {
    if (dets_.isEmpty())
        return -1;
    const QPointF w = wpos;
    for (int i = dets_.size() - 1; i >= 0; --i) { // 逆序：前景优先
        const auto& d = dets_[i];
        QPolygonF poly;
        poly << imageToWidget(d.p0) << imageToWidget(d.p1) << imageToWidget(d.p2)
             << imageToWidget(d.p3);
        if (pointInsidePolyW(poly, w))
            return i;
    }
    return -1;
}
int ImageCanvas::hitMaskStrict(const QPoint& wpos) const {
    if (maskRects_.isEmpty())
        return -1;
    const QPointF w = wpos;
    for (int i = maskRects_.size() - 1; i >= 0; --i) {
        const QRect& d = maskRects_[i];
        QPolygonF poly;
        poly << imageToWidget(d.topLeft()) << imageToWidget(d.bottomLeft())
             << imageToWidget(d.bottomRight()) << imageToWidget(d.topRight());
        if (pointInsidePolyW(poly, w))
            return i;
    }
    return -1;
}
bool ImageCanvas::pointInsidePolyW(const QPolygonF& polyW, const QPointF& w) const {
    return polyW.containsPoint(w, Qt::WindingFill);
}

/* ===== ROI 交互 ===== */
void ImageCanvas::beginFreeRoi(const QPoint& wpos) {
    draggingRoi_ = true;
    dragStartW_  = wpos;
    roiImg_      = QRect();
}
void ImageCanvas::updateFreeRoi(const QPoint& wpos) {
    QRect rw = QRect(dragStartW_, wpos).normalized();
    roiImg_  = widgetRectToImageRect(rw);
    emit roiChanged(roiImg_);
    update();
}
void ImageCanvas::endFreeRoi() {
    draggingRoi_ = false;
    if (!roiImg_.isNull())
        emit roiCommitted(roiImg_);
    update();
}
void ImageCanvas::placeFixedRoiAt(const QPoint& wpos) {
    if (!modelInputSize_.isValid())
        return;
    const QPointF cI = widgetToImage(wpos);
    QRect r(
        QPoint(
            int(cI.x() - modelInputSize_.width() / 2.0),
            int(cI.y() - modelInputSize_.height() / 2.0)),
        modelInputSize_);
    roiImg_ = clampRectToImage(r);
    emit roiChanged(roiImg_);
}

/* ===== UI 帮助 ===== */
void ImageCanvas::promptEditSelectedInfo(bool isCurrent) {
    if (!isCurrent) {
        if (selectedIndex_ < 0 || selectedIndex_ >= dets_.size())
            return;
    }
    // const QString oldCls = dets_[selectedIndex_].cls;
    // bool ok              = false;
    // const QString cls    = QInputDialog::getText(
    //     this, tr("Edit Class"), tr("Class label:"), QLineEdit::Normal, oldCls, &ok);
    // if (ok)
    //     setSelectedClass(cls.trimmed());
    ui::InfoDialog* dialog = new ui::InfoDialog(this);
    connect(dialog, &ui::InfoDialog::InfoGetted, this, &ImageCanvas::ProcessInfoChanged);
    const bool visibilitySupported = controller::AppSettings::instance().outputFormat()
                                  == static_cast<int>(LabelOutputFormat::LabelMasterV6);
    if (isCurrent) {
        dialog->updateInfo(
            true, 0, 0, 0, visibilitySupported, currentKeypointVisibility_[0],
            currentKeypointVisibility_[1], currentKeypointVisibility_[2],
            currentKeypointVisibility_[3]);
    } else {
        dialog->updateInfo(
            false, IdConvert::classToken2Id(dets_[selectedIndex_].cls),
            IdConvert::colorLetter2Id(dets_[selectedIndex_].color), dets_[selectedIndex_].size,
            visibilitySupported, dets_[selectedIndex_].keypointVisibility[0],
            dets_[selectedIndex_].keypointVisibility[1],
            dets_[selectedIndex_].keypointVisibility[2],
            dets_[selectedIndex_].keypointVisibility[3]);
    }
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void ImageCanvas::setupSvg() {
    auto icons_dir = controller::AppSettings::instance().assetsDir() + "/icons";
    // G (哨兵) - class_id = 0, 通过 size 区分大小
    svgCache_[0][0] = new QSvgRenderer(icons_dir + "/Gs.svg", this); // 小装甲
    svgCache_[0][1] = new QSvgRenderer(icons_dir + "/Gb.svg", this); // 大装甲
    // 1 (一号大装甲) - class_id = 1
    svgCache_[1][0] = new QSvgRenderer(icons_dir + "/1.svg", this);
    svgCache_[1][1] = svgCache_[1][0];
    // 2 (二号) - class_id = 2
    svgCache_[2][0] = new QSvgRenderer(icons_dir + "/2.svg", this);
    // 3 (三号) - class_id = 3
    svgCache_[3][0] = new QSvgRenderer(icons_dir + "/3.svg", this);
    svgCache_[3][1] = new QSvgRenderer(icons_dir + "/B3.svg", this);
    // 4 (四号) - class_id = 4
    svgCache_[4][0] = new QSvgRenderer(icons_dir + "/4.svg", this);
    svgCache_[4][1] = new QSvgRenderer(icons_dir + "/B4.svg", this);
    // 5 (五号) - class_id = 5
    svgCache_[5][0] = new QSvgRenderer(icons_dir + "/5.svg", this);
    svgCache_[5][1] = new QSvgRenderer(icons_dir + "/B5.svg", this);
    // O (前哨站) - class_id = 6
    svgCache_[6][0] = new QSvgRenderer(icons_dir + "/O.svg", this);
    // B (基地) - class_id = 7, 通过 size 区分大小
    svgCache_[7][0] = new QSvgRenderer(icons_dir + "/Bs.svg", this);
    svgCache_[7][1] = new QSvgRenderer(icons_dir + "/Bb.svg", this);
    qInfo() << "SVG loaded.";
}
static bool isBigType(const QString& t) {
    // 规则：1 / Bb / B3 / B4 / B5 按"大装甲"；其余按"小装甲"
    return (t == "1" || t == "Bb" || t == "B3" || t == "B4" || t == "B5");
}

// 拆分类别：首字母颜色(B/R/G/P)，后缀是图案类型（用来选 svg）
static inline void splitClass(const QString& cls, QString& color, QString& type) {
    const QString s = cls.trimmed();
    if (s.isEmpty()) {
        color.clear();
        type.clear();
        return;
    }
    color = s.left(1).toUpper(); // B / R / G /
    type  = s.mid(1);            // "1","2","Bs","Bb",...
}

void ImageCanvas::drawSvg(QPainter& p, const QVector<Armor>& armors) const {
    if (armors.isEmpty())
        return;

    p.save();

    // ---- 1) 预备：两套 SVG 外框四角（viewBox）和两套“锚点”（全是 SVG 坐标，顺序 TL, BL, BR, TR）
    QPolygonF big_svg_quad, small_svg_quad;
    big_svg_quad << QPointF(0., 0.) << QPointF(0., 478.) << QPointF(871., 478.)
                 << QPointF(871., 0.);
    small_svg_quad << QPointF(0., 0.) << QPointF(0., 516.) << QPointF(557., 516.)
                   << QPointF(557., 0.);

    QPolygonF big_anchors, small_anchors; // TL, BL, BR, TR（单位：SVG）
    big_anchors << QPointF(0., 140.61) << QPointF(0., 347.39) << QPointF(871., 347.39)
                << QPointF(871., 140.61);
    small_anchors << QPointF(0., 143.26) << QPointF(0., 372.74) << QPointF(557., 372.74)
                  << QPointF(557., 143.26);

    // 画布（控件）四角（顺序也用 TL, BL, BR, TR）
    QPolygonF painter_quad;
    painter_quad << QPointF(0., 0.) << QPointF(0., height()) << QPointF(width(), height())
                 << QPointF(width(), 0.);

    // 先把 SVG 外框四角 -> 画布四角，得到“把 SVG 坐标投到画布坐标”的仿射/投影
    QTransform big_svg2painter, small_svg2painter;
    QTransform::quadToQuad(big_svg_quad, painter_quad, big_svg2painter);
    QTransform::quadToQuad(small_svg_quad, painter_quad, small_svg2painter);

    // 把“SVG 的锚点”变到画布坐标（作为 quadToQuad 的 src）
    const QPolygonF big_src_on_painter   = big_svg2painter.map(big_anchors);
    const QPolygonF small_src_on_painter = small_svg2painter.map(small_anchors);

    for (const auto& a : armors) {
        // —— 解析类别：取颜色 & 图案类型（用类型去找 svg）
        QString color;
        int type, size;
        color = a.color;
        type  = IdConvert::classToken2Id(a.cls);
        size  = a.size;
        // splitClass(a.cls, color, type);

        // 找到对应的 QSvgRenderer（建议你的 svgCache_ 用“类型名”做 key，比如
        // "1","2","Bb","Bs","S","O"...）
        auto hash = svgCache_.find(type);
        if (hash == svgCache_.end()) {
            qWarning() << "SVG not found for type" << a.cls;
            continue;
        }
        auto it = hash->find(size);
        if (it == hash->end() || it.value() == nullptr) {
            qWarning() << "SVG not found for type and size" << a.cls << (a.size ? "Big" : "Small");
            continue;
        }
        QSvgRenderer* renderer = it.value();
        if (!renderer->isValid())
            continue;

        // —— 目标四点（画布坐标）；注意 Armor 的顺序：p0=TL, p1=BL, p2=BR, p3=TR
        QPolygonF dst;
        dst << imageToWidget(a.p0)  // TL
            << imageToWidget(a.p1)  // BL
            << imageToWidget(a.p2)  // BR
            << imageToWidget(a.p3); // TR

        // 选择 big/small 的“源四点”（同样是 TL, BL, BR, TR；已在画布坐标）
        const QPolygonF& src = a.size ? big_src_on_painter : small_src_on_painter;
        // —— 求单应 & 渲染
        QTransform H;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        if (!QTransform::quadToQuad(src, dst, H))
            continue;
#else
        if (!QTransform::quadToQuad(src, dst, H))
            continue;
#endif
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setTransform(H, true);
        renderer->render(&p); // SVG 将按“锚点→目标四点”透视过去
        p.restore();

        // （可选）根据 color 设置边框主色等，你可以在别处画：B/R/G/P → 蓝/红/灰/紫
        // 例如：if (color=="B") pen = blue; ...
    }

    p.restore();
}

void ImageCanvas::requestSave() {
    // qDebug() << "requestSave called"; // 处理图片,绘制Mask
    commitPendingVisibilityToggle();
    QPainter p(&raw_img);
    drawMasks(maskRects_, p, false);
    emit annotationsPublished(dets_, raw_img, !maskRects_.isEmpty());
}
void ImageCanvas::ProcessInfoChanged(
    const QString& EditedClass, const QString& Color, const int& size, int vis0, int vis1, int vis2,
    int vis3, bool isCurrent) {
    if (isCurrent) {
        currentClass_              = EditedClass;
        currentColor_              = Color;
        currentSize_               = size;
        currentKeypointVisibility_ = {vis0, vis1, vis2, vis3};
        createNewDetection();
    } else {
        setSelectedInfo(EditedClass.trimmed(), Color, size, vis0, vis1, vis2, vis3);
    }
}
