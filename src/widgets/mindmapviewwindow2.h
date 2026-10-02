#ifndef MINDMAPVIEWWINDOW2_H
#define MINDMAPVIEWWINDOW2_H

#include <QColor>
#include <QHash>

#include "viewwindow2.h"

class QComboBox;

namespace vnotex {

class InlineBanner;
class MindMapEditor;
class PresentationToolBarEffect;

// Single native editing surface; ViewWindow2 retains buffer/save/focus ownership.
class MindMapViewWindow2 : public ViewWindow2 {
  Q_OBJECT
public:
  explicit MindMapViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                              QWidget *p_parent = nullptr);
  ~MindMapViewWindow2() override;

  QString getLatestContent() const override;
  QSharedPointer<OutlineProvider> getOutlineProvider() const override;
  void setMode(ViewWindowMode p_mode) override;
  void handleNodeRetargeted(const NodeIdentifier &p_newNodeId) override;

public slots:
  void handleEditorConfigChange() override;
  void handleThemeChanged() override;
  void clearHighlights() override;

protected slots:
  void setModified(bool p_modified) override;
  void handleFindTextChanged(const QString &p_text, FindOptions p_options) override;
  void handleFindNext(const QStringList &p_texts, FindOptions p_options) override;
  void handleReplace(const QString &p_text, FindOptions p_options,
                     const QString &p_replaceText) override;
  void handleReplaceAll(const QString &p_text, FindOptions p_options,
                        const QString &p_replaceText) override;
  void handleFindAndReplaceWidgetClosed() override;
  void handleFindAndReplaceWidgetOpened() override;

protected:
  void paintEvent(QPaintEvent *p_event) override;
  void syncEditorFromBuffer() override;
  void scrollUp() override;
  void scrollDown() override;
  void zoom(bool p_zoomIn) override;
  void resetZoom() override;
  QString selectedText() const override;
  bool isPrintSupported() const override { return false; }
  void addAdditionalRightToolBarActions(QToolBar *p_toolBar) override;
  void addAdditionalViewToolBarActions(QToolBar *p_toolBar) override;

private:
  void setupUI();
  void setupToolBar();
  void connectEditorSignals();
  void syncZoomControls();
  void setupOutlineProvider();
  void refreshOutline();
  void clearOutline();
  void updateCurrentHeading();
  void applyEditorPalette();
  void findText(const QString &p_text, FindOptions p_options, bool p_incremental);

  // Owned by QObject.
  MindMapEditor *m_editor = nullptr;
  QAction *m_presentationAction = nullptr;
  PresentationToolBarEffect *m_presentationEffect = nullptr;
  QColor m_presentationBackground;
  QAction *m_zoomOutAction = nullptr;
  QAction *m_zoomInAction = nullptr;
  QAction *m_zoomComboAction = nullptr;
  QComboBox *m_zoomComboBox = nullptr;
  InlineBanner *m_loadErrorBanner = nullptr;
  QSharedPointer<OutlineProvider> m_outlineProvider;
  QStringList m_outlineNodeIds;
  QHash<QString, int> m_outlineIndexes;
  QString m_lastEditorError;
  quint64 m_generation = 0;
  bool m_propagateEditorToBuffer = true;
  bool m_contentLoaded = false;
};

} // namespace vnotex

#endif // MINDMAPVIEWWINDOW2_H
