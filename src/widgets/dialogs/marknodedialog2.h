#ifndef MARKNODEDIALOG2_H
#define MARKNODEDIALOG2_H

#include "scrolldialog.h"

class QLabel;
class QToolButton;

namespace vnotex {

// Color picker dialog for marking notebook nodes with text/background colors.
class MarkNodeDialog2 : public ScrollDialog {
  Q_OBJECT

public:
  MarkNodeDialog2(const QString &p_currentTextColor, const QString &p_currentBgColor,
                  QWidget *p_parent = nullptr);
  ~MarkNodeDialog2() override;

  QString textColor() const;
  QString backgroundColor() const;

protected:
  void acceptedButtonClicked() Q_DECL_OVERRIDE;

private:
  void setupUI();

  QWidget *buildColorSection(const QString &p_title, const QString &p_currentColor,
                             QToolButton **p_swatchButtons, int p_swatchCount,
                             QLabel *&p_selectedIndicator, QString &p_colorStorage);

  QToolButton *createSwatchButton(const QString &p_color, QWidget *p_parent);

  void updateSwatchHighlight(QToolButton **p_swatchButtons, int p_swatchCount,
                             const QString &p_activeColor);

  void updatePreview();

  void onSwatchClicked(const QString &p_color, QToolButton **p_swatchButtons, int p_swatchCount,
                       QLabel *p_indicator, QString &p_colorStorage);

  void onCustomColor(QToolButton **p_swatchButtons, int p_swatchCount, QLabel *p_indicator,
                     QString &p_colorStorage);

  void onClearColor(QToolButton **p_swatchButtons, int p_swatchCount, QLabel *p_indicator,
                    QString &p_colorStorage);

  // Selected colors.
  QString m_textColor;
  QString m_bgColor;

  // Preset swatch buttons (12 each).
  static const int c_swatchCount = 12;
  QToolButton *m_textSwatches[c_swatchCount] = {};
  QToolButton *m_bgSwatches[c_swatchCount] = {};

  // Selected-color indicator labels.
  QLabel *m_textColorIndicator = nullptr;
  QLabel *m_bgColorIndicator = nullptr;

  // Live preview.
  QLabel *m_previewLabel = nullptr;
};

} // namespace vnotex

#endif // MARKNODEDIALOG2_H
