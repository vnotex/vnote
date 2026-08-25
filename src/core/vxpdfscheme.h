#ifndef VXPDFSCHEME_H
#define VXPDFSCHEME_H

#include <QLatin1String>
#include <QString>
#include <QStringList>

namespace vnotex {

// The `vxpdf://` custom URL scheme used to serve the PDF viewer page, the vendored
// pdf.js asset tree and the PDF document bytes to QtWebEngine.
//
// Why a custom scheme at all: pdf.js v4+ ships ES modules only. The old
// `QWebEngineView::setHtml()` page has an opaque `data:` origin, so a module
// fetch to a `file://` URL is cross-origin and Chromium blocks it. Serving the
// page AND its modules from one real origin removes the whole problem.
//
// URL shape (host is always `pdf`; nothing else is served):
//
//   vxpdf://pdf/asset/<config-relative-path>   the extracted web/ tree in appData
//   vxpdf://pdf/document/<token>               the PDF bytes of a registered token
//
// The viewer page itself is `vxpdf://pdf/asset/web/pdf.js/web/pdf-viewer-template.html`,
// which the handler intercepts and answers with the GENERATED template rather than
// the on-disk file. Keeping it at that exact path is load-bearing: every pdf.js
// default option is resolved relative to the document URL, so `./images/`,
// `../web/cmaps/`, `../web/standard_fonts/`, `locale/locale.json` and
// `../build/pdf.worker.mjs` all land on the right asset only when the page sits
// where the stock `web/viewer.html` would.
//
// Header-only and Qt-Core-only on purpose: both `core_services`
// (HtmlTemplateService) and the GUI layer (the scheme handler) need it, and
// `core_services` must not gain a QtWebEngine dependency.
namespace VxPdfScheme {

inline QString scheme() { return QStringLiteral("vxpdf"); }

inline QString host() { return QStringLiteral("pdf"); }

inline QString origin() { return QStringLiteral("vxpdf://pdf"); }

inline QString assetPathPrefix() { return QStringLiteral("/asset/"); }

inline QString documentPathPrefix() { return QStringLiteral("/document/"); }

// The vendored pdf.js tree. Everything the viewer itself loads lives here.
inline QString assetRoot() { return QStringLiteral("web/pdf.js/"); }

// The VNote-owned classic scripts the PDF template injects from outside the
// pdf.js tree. This is an exact allowlist rather than a `web/js/` prefix: the
// handler must not become a proxy for the Markdown/MindMap asset trees, for
// `web/css/user.css`, or for the stale destination-only files the extra-data
// installer never prunes. Keep it in step with
// PdfViewerConfig::defaultViewerResource()'s `built_in` block.
inline QStringList extraAllowedAssets() {
  return QStringList{QStringLiteral("web/js/qwebchannel.js"),
                     QStringLiteral("web/js/eventemitter.js"), QStringLiteral("web/js/utils.js"),
                     QStringLiteral("web/js/vxcore.js")};
}

// Config-folder-relative path of the PDF viewer template. The handler answers
// this one path with the generated template instead of the file on disk.
inline QString viewerTemplateRelativePath() {
  return QStringLiteral("web/pdf.js/web/pdf-viewer-template.html");
}

// vxpdf://pdf/asset/<p_relativePath>
inline QString assetUrl(const QString &p_relativePath) {
  QString rel = p_relativePath;
  while (rel.startsWith(QLatin1Char('/'))) {
    rel.remove(0, 1);
  }
  return origin() + assetPathPrefix() + rel;
}

// vxpdf://pdf/document/<p_token>
inline QString documentUrl(const QString &p_token) {
  return origin() + documentPathPrefix() + p_token;
}

inline QString viewerUrl() { return assetUrl(viewerTemplateRelativePath()); }

// Rejects absolute paths, traversal (already-decoded or not), backslashes and
// anything that is neither inside the vendored pdf.js tree nor on the small
// exact allowlist of VNote-owned classic scripts. The handler decodes
// percent-escapes BEFORE calling this, so an encoded `%2e%2e` is caught here too.
inline bool isSafeAssetPath(const QString &p_relativePath) {
  if (p_relativePath.isEmpty()) {
    return false;
  }
  if (p_relativePath.contains(QLatin1Char('\\')) || p_relativePath.contains(QLatin1Char(':'))) {
    return false;
  }
  if (p_relativePath.startsWith(QLatin1Char('/'))) {
    return false;
  }

  // Segment hygiene is checked FIRST, so a traversal can never be smuggled
  // through the exact-match allowlist either.
  const auto parts = p_relativePath.split(QLatin1Char('/'));
  for (const auto &part : parts) {
    if (part == QStringLiteral("..") || part == QStringLiteral(".") || part.isEmpty()) {
      return false;
    }
  }

  return p_relativePath.startsWith(assetRoot()) || extraAllowedAssets().contains(p_relativePath);
}

// The routing decision of the scheme handler, factored out so it can be tested
// exhaustively. `QWebEngineUrlRequestJob` has no public constructor, so the only
// alternative would be to drive a real page load for every rejection case.
//
// This function is total: every request maps to exactly one Route, and the three
// Deny* values map 1:1 onto the QWebEngineUrlRequestJob::Error the handler
// reports.
struct Route {
  enum class Kind {
    ViewerTemplate, // serve the generated template; m_target is unused
    Asset,          // serve m_target, a validated config-folder-relative path
    Document,       // serve the document registered under the token m_target
    DenyMethod,     // not GET/HEAD          -> RequestDenied
    DenyPath,       // traversal / off-tree  -> RequestDenied
    NotFound        // unknown host or route -> UrlNotFound
  };

  Kind m_kind = Kind::NotFound;

  QString m_target;
};

// @p_method is the raw request method; @p_host and @p_path come from the request
// URL (QUrl::path() has already decoded percent-escapes, which is what lets the
// traversal check below see an encoded `%2e%2e`).
inline Route routeFor(const QByteArray &p_method, const QString &p_host, const QString &p_path) {
  Route route;

  if (p_method != QByteArrayLiteral("GET") && p_method != QByteArrayLiteral("HEAD")) {
    route.m_kind = Route::Kind::DenyMethod;
    return route;
  }

  if (p_host != host()) {
    route.m_kind = Route::Kind::NotFound;
    return route;
  }

  if (p_path.startsWith(documentPathPrefix())) {
    const auto token = p_path.mid(documentPathPrefix().size());
    if (token.isEmpty() || token.contains(QLatin1Char('/'))) {
      route.m_kind = Route::Kind::NotFound;
      return route;
    }
    route.m_kind = Route::Kind::Document;
    route.m_target = token;
    return route;
  }

  if (!p_path.startsWith(assetPathPrefix())) {
    route.m_kind = Route::Kind::NotFound;
    return route;
  }

  const auto rel = p_path.mid(assetPathPrefix().size());
  if (!isSafeAssetPath(rel)) {
    route.m_kind = Route::Kind::DenyPath;
    return route;
  }

  route.m_kind =
      (rel == viewerTemplateRelativePath()) ? Route::Kind::ViewerTemplate : Route::Kind::Asset;
  route.m_target = rel;
  return route;
}

} // namespace VxPdfScheme

} // namespace vnotex

#endif // VXPDFSCHEME_H
