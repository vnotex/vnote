#ifndef SETTINGSPAGEHELPER_H
#define SETTINGSPAGEHELPER_H

#include <QString>

class QCheckBox;
class QFrame;
class QLabel;
class QVBoxLayout;
class QWidget;

namespace vnotex {

// Static helper for creating modern card-based settings page layouts.
// Cards provide visual grouping with subtle background lift from the page.
class SettingsPageHelper {
public:
  SettingsPageHelper() = delete;

  // Create a card container (QFrame with SettingsCard property).
  // Returns a QFrame with a QVBoxLayout already set. Add rows/widgets to its layout.
  static QFrame *createCard(QWidget *p_parent = nullptr);

  // Create a bold section title label (SettingsCardTitle property).
  static QLabel *createCardTitle(const QString &p_title, QWidget *p_parent = nullptr);

  // Create a muted description label (SettingsDescription property).
  static QLabel *createDescription(const QString &p_text, QWidget *p_parent = nullptr);

  // Create a thin horizontal separator line (SettingsSeparator property).
  static QFrame *createSeparator(QWidget *p_parent = nullptr);

  // Create a setting row: label on the left, control widget on the right.
  // p_description is optional muted text shown below the label.
  // Returns a QWidget with SettingsRow property set.
  static QWidget *createSettingRow(const QString &p_label, const QString &p_description,
                                   QWidget *p_control, QWidget *p_parent = nullptr);

  // Create a checkbox row: checkbox on the left with optional description below.
  // Returns a QWidget with SettingsRow property set.
  static QWidget *createCheckBoxRow(QCheckBox *p_checkBox, const QString &p_description,
                                    QWidget *p_parent = nullptr);

  // Convenience: create a card with title, optional description, and return its inner layout.
  // Adds the card to p_parentLayout. Returns the card's inner QVBoxLayout for adding rows.
  static QVBoxLayout *addSection(QVBoxLayout *p_parentLayout, const QString &p_title,
                                 const QString &p_description = QString(),
                                 QWidget *p_parent = nullptr);
};

} // namespace vnotex

#endif // SETTINGSPAGEHELPER_H
