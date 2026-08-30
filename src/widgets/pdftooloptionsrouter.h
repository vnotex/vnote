#ifndef PDFTOOLOPTIONSROUTER_H
#define PDFTOOLOPTIONSROUTER_H

#include <QString>

namespace vnotex {

class PdfViewerAdapter;
class PdfViewerConfig;

// The two-step "push to the adapter AND persist" that every per-tool settings
// change goes through: the toolbar menus, and the page context menu's
// Highlight ▸ <colour>.
//
// Free functions taking the config and the adapter EXPLICITLY, rather than
// methods on PdfViewWindow2, for one reason: PdfViewWindow2 needs a full window
// plus a WebEngine profile to construct, so a private method there is
// untestable. Routing that cannot be tested is exactly where the "settings
// persist but a new window forgets them" class of bug lives.
namespace PdfToolOptionsRouter {

// Seeds the adapter from persisted config. MUST run BEFORE the first
// false->true readiness transition: the reload latch republishes only what the
// adapter already holds, so without this a newly opened PDF window comes up on
// the adapter/JS defaults and the saved settings appear forgotten.
void hydrate(const PdfViewerConfig &p_config, PdfViewerAdapter *p_adapter);

// Persists FIRST, then pushes the value READ BACK from config, so the adapter
// and the store can never hold different normalizations of the same pick.
// Returns the normalized colour actually stored.
QString applyColor(PdfViewerConfig &p_config, PdfViewerAdapter *p_adapter, const QString &p_tool,
                   const QString &p_token);

// Ink width or free-text font size, whichever the tool carries. A tool with
// neither is a no-op.
double applyScalar(PdfViewerConfig &p_config, PdfViewerAdapter *p_adapter, const QString &p_tool,
                   double p_value);

// Ink stroke opacity. A tool that carries no opacity is a no-op returning 0.0.
double applyOpacity(PdfViewerConfig &p_config, PdfViewerAdapter *p_adapter, const QString &p_tool,
                    double p_value);

// The page context menu route: persist the pick as the HIGHLIGHT tool's colour
// (so the toolbar menu and the context menu cannot disagree), then ask the
// overlay to capture the current selection with that same normalized token.
void captureHighlight(PdfViewerConfig &p_config, PdfViewerAdapter *p_adapter,
                      const QString &p_token);

} // namespace PdfToolOptionsRouter

} // namespace vnotex

#endif // PDFTOOLOPTIONSROUTER_H
