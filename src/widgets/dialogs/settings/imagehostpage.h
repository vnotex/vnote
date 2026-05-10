#ifndef IMAGEHOSTPAGE_H
#define IMAGEHOSTPAGE_H

#include "settingspage.h"

#include <QJsonObject>
#include <QMap>
#include <QVector>

class QGroupBox;
class QLineEdit;
class QVBoxLayout;
class QComboBox;
class QCheckBox;

namespace vnotex {
class ImageHostController;
class IImageHostProvider;

class ImageHostPage : public SettingsPage {
  Q_OBJECT
public:
  explicit ImageHostPage(ServiceLocator &p_services, QWidget *p_parent = nullptr);

  QString title() const Q_DECL_OVERRIDE;

  QString slug() const Q_DECL_OVERRIDE;

protected:
  void loadInternal() Q_DECL_OVERRIDE;

  bool saveInternal() Q_DECL_OVERRIDE;

private:
  void setupUI();

  void newImageHost();

  QGroupBox *setupGroupBoxForProvider(IImageHostProvider *p_provider, QWidget *p_parent);

  void removeImageHost(const QString &p_hostName);

  void addWidgetToLayout(QWidget *p_widget);

  QJsonObject fieldsToConfig(const QVector<QLineEdit *> &p_fields,
                             IImageHostProvider *p_provider) const;

  void testImageHost(IImageHostProvider *p_provider);

  void removeLastStretch();

  QVBoxLayout *m_mainLayout = nullptr;

  ImageHostController *m_controller = nullptr;

  QMap<IImageHostProvider *, QVector<QLineEdit *>> m_hostToFields;

  // Parallel map: for each provider, stores config key names in same order as fields.
  QMap<IImageHostProvider *, QStringList> m_hostToKeys;

  QComboBox *m_defaultImageHostComboBox = nullptr;

  QCheckBox *m_clearObsoleteImageCheckBox = nullptr;
};
} // namespace vnotex

#endif // IMAGEHOSTPAGE_H
