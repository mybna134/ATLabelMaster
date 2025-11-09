#include "ui_info_dialog.h"
#include <qdialog.h>
#include <qglobal.h>
#include <qguiapplication_platform.h>
#include <qobjectdefs.h>
#include <qtmetamacros.h>
#include <qwidget.h>
namespace ui {
    class InfoDialog : public QDialog {
        Q_OBJECT
    public:
        // 默认Gray, unknown
        explicit InfoDialog(QWidget* parent = nullptr);
        ~InfoDialog();
        void centerOn(QWidget* parent);
        void updateInfo(
            bool isCurrent = false, const QString& defaultClass = "unknown",
            const QString& defaultColor = "Gray");
    signals:
        void dataChanged(QString EditedClass, QString Color, bool isCurrent);
    public slots:
        void reject() override;
        void accept() override;

    private:
        bool _isCurrent = false;
        Ui::InfoDialog* ui;
    };
} // namespace ui
