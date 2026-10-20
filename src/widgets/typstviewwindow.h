#ifndef TYPSTVIEWWINDOW_H
#define TYPSTVIEWWINDOW_H

#include "viewwindow2.h"

#include <QColor>
#include <QPointer>

#include <memory>

#include <vtextedit/vtypsteditor.h>

class QDialog;
class QLabel;
class QMimeData;
class QTextCursor;
class QSpinBox;

namespace vnotex {
class EditorStatusBarBinder;
class ImageViewer;
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
  bool supportsBufferReplacement() const override { return true; }
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
  void handleTypeAction(int p_action) override;
  void addAdditionalRightToolBarActions(QToolBar *p_toolBar) override;
  void addAdditionalViewToolBarActions(QToolBar *p_toolBar) override;
  void paintEvent(QPaintEvent *p_event) override;
  void changeEvent(QEvent *p_event) override;
  void hideEvent(QHideEvent *p_event) override;
  bool eventFilter(QObject *p_watched, QEvent *p_event) override;
  void syncEditorFromBuffer() override;
  void prepareBufferReplacement() override;
  void releaseBufferResources() override;
  void bindBufferResources() override;
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
  enum class InsertionOrigin { Clipboard, Drop };
  enum class InsertionResult { NotHandled, Cancelled, Inserted };
  enum class InsertionKind {
    Image,
    ImageFromUrl,
    ImageFromData,
    RelativeImage,
    Link,
    RelativeLink,
    Attachment,
    Text,
    FileContent
  };
  struct NativeInteraction;
  std::shared_ptr<NativeInteraction> retainNativeInteraction();
  struct Insertion;
  enum class ImageResourceAction { View, Copy };

  bool canViewResources() const;
  bool resourceContextCurrent(const vte::TypstResourceContext &p_context, quint64 p_generation,
                              bool p_forEdit = false) const;
  void trackContextMenu(QMenu *p_menu);
  void addResourceContextActions(const vte::TypstResourceContext &p_context, QMenu *p_menu);
  void requestContextImage(const vte::TypstResourceContext &p_context, ImageResourceAction p_action,
                           quint64 p_generation);
  void handleContextImageData(quint64 p_request, const QByteArray &p_data, const QString &p_mime,
                              const QString &p_error);
  void resizeContextImage(const vte::TypstResourceContext &p_context, quint64 p_generation);
  void invalidateContextResources(bool p_closeImage = true);
  void showImageViewer(const QImage &p_image);
  void closeImageViewer(bool p_restoreFocus = false);

  InsertionResult insertFromMimeData(const QMimeData *p_source, InsertionOrigin p_origin,
                                     const QTextCursor &p_target);
  void typeImage();
  void typeLink();
  void pastePlainText();
  bool canInsert() const;
  void invalidateInsertion();
  void syncInsertionActions();

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
  QAction *m_themeStyleInPreviewAction = nullptr;
  QVector<QAction *> m_pdfActions;
  ZoomToolBar *m_zoomToolBar = nullptr;
  QAction *m_previousPageAction = nullptr;
  QAction *m_nextPageAction = nullptr;
  QSpinBox *m_pageSpinBox = nullptr;
  QLabel *m_pageCountLabel = nullptr;
  FindOptions m_sourceFindOptions = FindOption::FindNone;
  bool m_sourceFindOptionsSaved = false;

  // Menus/callbacks share this lazy state; idle reuse avoids per-command allocation.
  std::shared_ptr<NativeInteraction> m_nativeInteraction;
  QPointer<QMenu> m_contextMenu;
  QPointer<QDialog> m_resizeImageDialog;
  QPointer<ImageViewer> m_imageOverlay;
  QPointer<QWidget> m_imageOriginalFocus;
  quint64 m_resourceGeneration = 0;
  quint64 m_nextImageRequest = 0;
  quint64 m_pendingImageRequest = 0;
  quint64 m_pendingImageGeneration = 0;
  vte::TypstResourceContext m_pendingImageContext;
  ImageResourceAction m_pendingImageAction = ImageResourceAction::View;
  QString m_pendingImageBufferId;
  QString m_pendingImageBufferPath;

  // Stack-owned operation; dialogs and callbacks may invalidate it, never replace it.
  Insertion *m_insertion = nullptr;
  quint64 m_insertionSerial = 0;
  bool m_nativeSourceDrop = false;
  QPointer<QDialog> m_insertionDialog;
  QAction *m_insertImageAction = nullptr;
  QAction *m_insertLinkAction = nullptr;
  QAction *m_plainPasteAction = nullptr;

  bool m_propagateEditorToBuffer = false;
  bool m_sourceAvailable = false;
};
} // namespace vnotex

#endif // TYPSTVIEWWINDOW_H
