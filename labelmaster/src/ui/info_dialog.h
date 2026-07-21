#include "centered_dialog.h"
#include "ui_info_dialog.h"
#include <qdialog.h>
#include <qglobal.h>
#include <qguiapplication_platform.h>
#include <qobjectdefs.h>
#include <qstringconverter.h>
#include <qtmetamacros.h>
#include <qwidget.h>
namespace ui {
class InfoDialog : public QDialog {
    Q_OBJECT
public:
    // 默认Gray, unknown
    explicit InfoDialog(QWidget* parent = nullptr);
    ~InfoDialog();
    // void centerOn(QWidget* parent);
    void updateInfo(
        bool isCurrent = false, const int& defaultClassId = 0, const int& defaultColorId = 0,
        const int& defaultSize = 0, bool visibilitySupported = false, int vis0 = 2, int vis1 = 2,
        int vis2 = 2, int vis3 = 2, bool pose14Classes = false);
signals:
    void InfoGetted(
        QString editedClass, QString color, int size, int vis0, int vis1, int vis2, int vis3,
        bool isCurrent);
public slots:
    void reject() override;
    void accept() override;

private:
    void updateSize(int index);
    bool _isCurrent = false;
    Ui::InfoDialog* ui;
};
} // namespace ui
