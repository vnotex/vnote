#include "pdfviewwindowcontroller.h"

#include <QUrl>

#include <core/configmgr2.h>
#include <core/editorconfig.h>
#include <core/nodeidentifier.h>
#include <core/pdfviewerconfig.h>
#include <core/servicelocator.h>
#include <core/services/notebookcoreservice.h>
#include <core/vxpdfscheme.h>

using namespace vnotex;

PdfViewWindowController::PdfViewWindowController(ServiceLocator &p_services, QObject *p_parent)
    : QObject(p_parent), m_services(p_services) {}

bool PdfViewWindowController::checkAndUpdateConfigRevision() {
  bool changed = false;

  auto *configMgr = m_services.get<ConfigMgr2>();
  const auto &editorConfig = configMgr->getEditorConfig();

  if (m_editorConfigRevision != editorConfig.revision()) {
    changed = true;
    m_editorConfigRevision = editorConfig.revision();
  }

  if (m_pdfViewerConfigRevision != editorConfig.getPdfViewerConfig().revision()) {
    changed = true;
    m_pdfViewerConfigRevision = editorConfig.getPdfViewerConfig().revision();
  }

  return changed;
}

QString PdfViewWindowController::buildAbsolutePath(const NodeIdentifier &p_nodeId) const {
  // External file (e.g. drag&dropped into the main window): empty notebookId,
  // relativePath already holds the absolute on-disk path. vxcore's
  // path-build-absolute requires a valid notebook and would return NOT_FOUND,
  // so short-circuit here and return the absolute path directly.
  if (p_nodeId.notebookId.isEmpty()) {
    return p_nodeId.relativePath;
  }

  auto *notebookService = m_services.get<NotebookCoreService>();
  return notebookService->buildAbsolutePath(p_nodeId.notebookId, p_nodeId.relativePath);
}

PdfViewWindowController::PdfUrlState
PdfViewWindowController::preparePdfUrl(const QString &p_documentToken) {
  PdfUrlState state;

  if (p_documentToken.isEmpty()) {
    state.valid = false;
    return state;
  }

  const auto documentUrl = VxPdfScheme::documentUrl(p_documentToken);
  // Percent-encode the whole document URL so the `?file=` value survives the
  // viewer's own query parsing intact (`:` and `/` must not stay literal).
  const auto encodedUrl = QUrl::toPercentEncoding(documentUrl);

  QUrl viewerUrl(VxPdfScheme::viewerUrl());
  viewerUrl.setQuery(QStringLiteral("file=") + QString::fromLatin1(encodedUrl));

  state.templateUrl = viewerUrl;
  state.valid = true;
  return state;
}
