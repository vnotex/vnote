#ifndef ZOOMTOOLBAR_H
#define ZOOMTOOLBAR_H

#include <QObject>

class QAction;
class QComboBox;
class QIcon;
class QKeySequence;
class QToolBar;

namespace vnotex {

// Camera geometry and limits belong to the editor; this owns only the shared controls.
class ZoomToolBar : public QObject {
  Q_OBJECT
public:
  ZoomToolBar(const QIcon &p_zoomOutIcon, const QIcon &p_zoomInIcon, QToolBar *p_toolBar);

  // Positive values are manual factors; zero selects Fit Page, -1 selects Fit Width.
  void setZoom(qreal p_zoom);
  void setEnabled(bool p_enabled);
  void setVisible(bool p_visible);

  // Display only; empty sequences remove tips. The caller owns shortcut bindings.
  void setShortcutTips(const QKeySequence &p_zoomIn, const QKeySequence &p_zoomOut);

signals:
  void zoomRequested(bool p_zoomIn);
  void zoomSelected(qreal p_zoom);

protected:
  bool eventFilter(QObject *p_object, QEvent *p_event) override;

private:
  void applyInput();
  void applySelection(qreal p_zoom);
  void refreshText();

  QAction *m_zoomOutAction = nullptr;
  QAction *m_zoomInAction = nullptr;
  QAction *m_comboAction = nullptr;
  QComboBox *m_comboBox = nullptr;
  qreal m_zoom = 1.0;
  bool m_enabled = true;
};

} // namespace vnotex

#endif // ZOOMTOOLBAR_H
