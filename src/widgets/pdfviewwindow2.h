#ifndef PDFVIEWWINDOW2_H
#define PDFVIEWWINDOW2_H

#include "commentprovider.h"
#include "editors/pdfvieweradapter.h"
#include "outlineprovider.h"
#include "viewwindow2.h"

#include <QHash>

#include <core/pdfviewerconfig.h>

class QActionGroup;
class QToolButton;

namespace vnotex {
class CommentController;
class InlineBanner;
class PdfAnnotationToolBar;
class PdfViewer;
class PdfViewerAdapter;
class PdfViewWindowController;

// Concrete ViewWindow2 subclass for PDF files.
// Provides a read-only PDF viewer (PdfViewer via WebEngine + PDF.js)
// in the new architecture (ServiceLocator + Buffer2).
//
// This is a read-only window (ViewWindowMode::Read).
// Mode switching is not supported for PDF files.
class PdfViewWindow2 : public ViewWindow2 {
  Q_OBJECT
public:
  explicit PdfViewWindow2(ServiceLocator &p_services, const Buffer2 &p_buffer,
                          QWidget *p_parent = nullptr);

  ~PdfViewWindow2() Q_DECL_OVERRIDE;

  QString getLatestContent() const Q_DECL_OVERRIDE;

  void setMode(ViewWindowMode p_mode) Q_DECL_OVERRIDE;

  QSharedPointer<OutlineProvider> getOutlineProvider() const Q_DECL_OVERRIDE;

  QSharedPointer<CommentProvider> getCommentProvider() const Q_DECL_OVERRIDE;

  // A PDF is never edited in place, so this window can never be modified and
  // the Save action could never leave its disabled state:
  //   - onEditorContentsChanged() is not connected here, so m_editorDirty
  //     cannot become true;
  //   - setModified() below is inert and nothing writes buffer content;
  //   - comments are side data, persisted to comments.json by CommentController
  //     and CommentService, which never touch Buffer2.
  // If any of those three stops holding, this must go back to true.
  bool isSaveSupported() const Q_DECL_OVERRIDE { return false; }

public slots:
  void handleEditorConfigChange() Q_DECL_OVERRIDE;

  void handleThemeChanged() Q_DECL_OVERRIDE;

protected slots:
  void setModified(bool p_modified) Q_DECL_OVERRIDE;

protected:
  void syncEditorFromBuffer() Q_DECL_OVERRIDE;

  void handleNodeRetargeted(const NodeIdentifier &p_newNodeId) Q_DECL_OVERRIDE;

  void scrollUp() Q_DECL_OVERRIDE;

  void scrollDown() Q_DECL_OVERRIDE;

  void zoom(bool p_zoomIn) Q_DECL_OVERRIDE;

  void addAdditionalRightToolBarActions(QToolBar *p_toolBar) Q_DECL_OVERRIDE;

private:
  void setupUI();

  void setupToolBar();

  void setupViewer();

  void setupOutlineProvider();

  void setupComments();

  void setupAnnotationToolBarActions(QToolBar *p_toolBar);

  void setActiveTool(PdfViewerAdapter::Tool p_tool);

  // Routes one settings change through PdfToolOptionsRouter (normalize ->
  // persist -> push to the adapter), then repaints the ticks. The routing
  // itself deliberately lives in the router, which is testable without a window
  // and a WebEngine profile.
  void applyToolOptions(const QString &p_tool, bool p_isColor, const QString &p_token,
                        double p_value);

  void setToolColor(const QString &p_tool, const QString &p_token);

  void setToolScalar(const QString &p_tool, double p_value);

  void setToolOpacity(const QString &p_tool, double p_value);

  // Current per-tool options, read from PdfViewerConfig.
  QHash<QString, PdfViewerConfig::ToolOptions> currentToolOptions() const;

  // Seeds the adapter from persisted config BEFORE the first ready transition.
  // Without it, menu picks persist to JSON but a newly opened PDF window comes
  // up on adapter/JS defaults — the saved settings would appear forgotten, and
  // the reload latch cannot compensate (it republishes only what the adapter
  // already holds).
  void hydrateToolOptions();

  // Builds the ThemeService-backed swatch resolver and hands it, plus the
  // themed border, to every widget that draws a colour chip.
  void applySwatchResolvers();

  // Repaints the toggles from the ADAPTER's state, which is the single source
  // of truth: the web side can leave a tool by itself (Esc, or the one-shot
  // Text tool completing), and the toolbar must follow rather than diverge.
  void syncToolBarState();

  // Enables/disables the authoring tools. Called on editability changes: a
  // read-only file must not be drawable at all, rather than silently refusing
  // each gesture after the fact.
  void setAuthoringEnabled(bool p_enabled);

  // Push the controller's set onto the overlay bridge as a JSON array.
  void publishCommentsToViewer();

  PdfViewerAdapter *adapter() const;

  // Drops the current vxpdf document token, if any, so the handler's registry
  // cannot grow for the process lifetime. Idempotent.
  void revokeDocumentToken();

  // Managed by QObject parent (this).
  PdfViewWindowController *m_controller = nullptr;

  // Managed by QObject.
  PdfViewer *m_viewer = nullptr;

  // Token registered with WebEngineProfileService for the currently displayed
  // document. Replaced on every (re)load and revoked in the destructor.
  QString m_documentToken;

  QSharedPointer<OutlineProvider> m_outlineProvider;

  QSharedPointer<CommentProvider> m_commentProvider;

  // Managed by QObject parent (this).
  CommentController *m_commentController = nullptr;

  // Lazily created on the first comment-store failure; managed by QObject.
  InlineBanner *m_commentBanner = nullptr;

  // Owns the three tool buttons and their settings menus. Managed by QObject
  // parent (this).
  PdfAnnotationToolBar *m_annotationToolBar = nullptr;
};
} // namespace vnotex

#endif // PDFVIEWWINDOW2_H
