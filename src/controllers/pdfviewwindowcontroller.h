#ifndef PDFVIEWWINDOWCONTROLLER_H
#define PDFVIEWWINDOWCONTROLLER_H

#include <QObject>
#include <QString>
#include <QUrl>

namespace vnotex {

class ServiceLocator;
struct NodeIdentifier;

class PdfViewWindowController : public QObject {
  Q_OBJECT
public:
  explicit PdfViewWindowController(ServiceLocator &p_services, QObject *p_parent = nullptr);

  // State snapshot for applying PDF URL to the viewer.
  struct PdfUrlState {
    QUrl templateUrl; // PDF.js template URL with ?file= query
    bool valid = false;
  };

  // ============ Config Revision Tracking ============

  // Check if editor config or PDF viewer config has changed.
  // Updates internal revision tracking. Returns true if config changed since last check.
  bool checkAndUpdateConfigRevision();

  // ============ Path Resolution ============

  // Resolve a NodeIdentifier to an absolute file path on disk.
  // Queries NotebookCoreService for the notebook root folder.
  QString buildAbsolutePath(const NodeIdentifier &p_nodeId) const;

  // ============ URL Construction ============

  // Prepare the PDF viewer URL state from buffer node identity.
  // @p_contentPath: absolute path to the PDF file on disk.
  // @p_templatePath: path to the PDF.js HTML template.
  // Pure logic — no GUI dependencies.
  static PdfUrlState preparePdfUrl(const QString &p_contentPath, const QString &p_templatePath);

private:
  ServiceLocator &m_services;
  int m_editorConfigRevision = 0;
  int m_pdfViewerConfigRevision = 0;
};

} // namespace vnotex

#endif // PDFVIEWWINDOWCONTROLLER_H
