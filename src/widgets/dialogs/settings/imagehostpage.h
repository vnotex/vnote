#ifndef IMAGEHOSTPAGE_H
#define IMAGEHOSTPAGE_H

#include "settingspage.h"

#include <QVector>
#include <QMap>

class QGroupBox;
class QLineEdit;
class QVBoxLayout;
class QComboBox;
class QCheckBox;

namespace vnotex
{
    class ImageHost;

    class ImageHostPage : public SettingsPage
    {
        Q_OBJECT
    public:
        explicit ImageHostPage(QWidget *p_parent = nullptr);

        QString title() const Q_DECL_OVERRIDE;

    protected:
        void loadInternal() Q_DECL_OVERRIDE;

        bool saveInternal() Q_DECL_OVERRIDE;

    private:
        void setupUI();

        void newImageHost();

        QGroupBox *setupGroupBoxForImageHost(ImageHost *p_host, QWidget *p_parent);

        void removeImageHost(const QString &p_hostName);

        void addWidgetToLayout(QWidget *p_widget);

        QJsonObject fieldsToConfig(const QVector<QLineEdit *> &p_fields) const;

        void testImageHost(const QString &p_hostName);

        QGroupBox *setupGeneralBox(QWidget *p_parent);

        void removeLastStretch();

        QVBoxLayout *m_mainLayout = nullptr;

        // [host] -> list of related fields.
        QMap<ImageHost *, QVector<QLineEdit *>> m_hostToFields;

        QComboBox *m_defaultImageHostComboBox = nullptr;

        QCheckBox *m_clearObsoleteImageCheckBox = nullptr;
    };
}

#endif // IMAGEHOSTPAGE_H
