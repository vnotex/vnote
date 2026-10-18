#ifndef TYPSTVIEWWINDOW_H
#define TYPSTVIEWWINDOW_H

#include "viewwindow2.h"

#include <QColor>

#include <vtextedit/vtypsteditor.h>

class QLabel;
class QSpinBox;

namespace vnotex {
class EditorStatusBarBinder;
class InlineBanner;
class PresentationToolBarEffect;
class TypstViewWindowController;
class ZoomToolBar;

class TypstViewWindow : public ViewWindow2 {
  Q_OBJECT
public:
  friend class TextViewWindowHelper;

  TypstViewWindow(ServiceLocator &p_services, const Buffer2 &p_buffer, QWidget *p_parent = nullptr,
                  ViewWindowMode p_initialMode = ViewWindowMode::Edit);
  ~TypstViewWindow() override;

  QString getLatestContent() const override;
  QSharedPointer<OutlineProvider> getOutlineProvider() const override;
  bool isEncodingSupported() const override { return true; }
  void setMode(ViewWindowMode p_mode) override;
  int getCursorPosition() const override;
  int getScrollPosition() const override;
  ViewPositionState capturePositionState() const override;
  void restorePositionState(const ViewPositionState &p_state) override;
  void handleNodeRetargeted(const NodeIdentifier &p_newNodeId) override;
  bool aboutToClose(bool p_force) override;

public slots:
  void handleEditorConfigChange() override;
  void handleThemeChanged() override;
  void applySnippet(const QString &p_name) override;
  void applySnippet() override;
  void clearHighlights() override;
  void applyFileOpenSettings(const FileOpenSettings &p_settings) override;

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
  void addAdditionalMenuActions(QMenu *p_menu) override;
  void addAdditionalRightToolBarActions(QToolBar *p_toolBar) override;
  void addAdditionalViewToolBarActions(QToolBar *p_toolBar) override;
  void paintEvent(QPaintEvent *p_event) override;
  void syncEditorFromBuffer() override;
  void applyReadableWidth() override;
  bool isPrintSupported() const override { return false; }
  void scrollUp() override;
  void scrollDown() override;
  void zoom(bool p_zoomIn) override;
  void resetZoom() override;
  QPoint getFloatingWidgetPosition() override;
  QString selectedText() const override;
  void
  fetchWordCountInfo(const std::function<void(const WordCountInfo &)> &p_callback) const override;

private:
  void setupUI();
  void setupOutlineProvider();
  void refreshOutline(const QVector<vte::TypstHeadingInfo> &p_headings);
  void clearOutline();
  void navigateToHeading(int p_index);
  bool isActiveFileHeading(const vte::TypstHeadingInfo &p_heading) const;
  void updateCurrentHeading();
  void setupPdfToolBar(QToolBar *p_toolBar);
  void applyPdfZoom(qreal p_zoom);
  void updatePresentationActions();
  void updateFindBackend();
  void createEditor();
  void connectEditorSignals();
  QSharedPointer<vte::TypstEditorConfig> buildEditorConfig() const;
  void applyEditorConfig();
  void updateEditorFromConfig();
  void syncModeFromEditor();
  void revealSource();
  void applyProjectIdentity();
  void showSourceError(const QString &p_error);
  int getEditorMaxContentWidth() const;

  TypstViewWindowController *m_controller = nullptr;
  vte::VTypstEditor *m_typstEditor = nullptr;
  // Borrowed from the composite editor; never replaced or deleted separately.
  vte::VTextEditor *m_editor = nullptr;
  EditorStatusBarBinder *m_statusBinder = nullptr;
  InlineBanner *m_loadErrorBanner = nullptr;
  QSharedPointer<OutlineProvider> m_outlineProvider;
  // Index-aligned with the published outline, including repeated and imported headings.
  QVector<vte::TypstHeadingInfo> m_outlineHeadings;

  QAction *m_presentationAction = nullptr;
  PresentationToolBarEffect *m_presentationEffect = nullptr;
  QColor m_presentationBackground;

  QAction *m_livePreviewAction = nullptr;
  QAction *m_inplacePreviewAction = nullptr;
  QVector<QAction *> m_pdfActions;
  ZoomToolBar *m_zoomToolBar = nullptr;
  QAction *m_previousPageAction = nullptr;
  QAction *m_nextPageAction = nullptr;
  QSpinBox *m_pageSpinBox = nullptr;
  QLabel *m_pageCountLabel = nullptr;
  FindOptions m_sourceFindOptions = FindOption::FindNone;
  bool m_sourceFindOptionsSaved = false;

  bool m_propagateEditorToBuffer = false;
  bool m_sourceAvailable = false;
};
} // namespace vnotex

#endif // TYPSTVIEWWINDOW_H
