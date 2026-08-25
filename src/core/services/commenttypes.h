#ifndef COMMENTTYPES_H
#define COMMENTTYPES_H

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace vnotex {

// Value types for the per-file `comments.json` sidecar store.
//
// The store is deliberately NOT PDF-specific: only `Comment::m_anchor` carries
// file-type knowledge, so Markdown (and anything else) can reuse the same
// service, schema, controller and dock later by adding an anchor type.
//
// Schema (version 1):
//
//   {
//     "version": 1,
//     "comments": [
//       {
//         "id": "<uuid>",
//         "createdUtc": 1730000000,
//         "modifiedUtc": 1730000000,
//         "text": "the comment body",   // may be empty for a bare highlight
//         "color": "yellow",            // SEMANTIC token, never a literal hex
//         "anchor": {
//           "type": "pdf-quads",        // discriminator
//           "page": 3,                  // 0-based
//           "quads": [[x0,y0,x1,y1,x2,y2,x3,y3]],   // PDF page space
//           "text": "selected text"     // display + recovery if geometry drifts
//         }
//       }
//     ]
//   }
//
// FORWARD COMPATIBILITY IS A HARD REQUIREMENT. An older PDF-only build must be
// able to round-trip a newer build's Markdown comments untouched, so:
//   * an unknown `anchor.type` is preserved verbatim (`m_anchor` is stored as a
//     raw QJsonObject and never rebuilt from typed fields);
//   * unknown top-level keys on a comment are preserved in `m_extraKeys`;
//   * unknown top-level keys on the document are preserved in `m_extraKeys`.

// Semantic highlight colors. These are TOKENS, resolved to real colors by
// ThemeService and injected into the PDF template as CSS custom properties —
// a literal hex must never appear in the store, or a light-theme highlight
// would be unreadable after a switch to a dark theme.
namespace CommentColor {

inline QString defaultToken() { return QStringLiteral("yellow"); }

// The complete set. `CommentService` normalizes anything else to the default so
// a hand-edited or newer-schema file cannot produce an unstyled highlight.
inline QStringList all() {
  return QStringList{QStringLiteral("yellow"), QStringLiteral("green"), QStringLiteral("blue"),
                     QStringLiteral("pink"), QStringLiteral("purple")};
}

inline bool isValid(const QString &p_token) { return all().contains(p_token); }

// Human-readable, translated, capitalized name for a token ("yellow" ->
// "Yellow"). The token is the PAYLOAD written to comments.json and must never
// change; this is presentation only, and a translation can never affect what is
// stored.
//
// Both the comment dock and the PDF page context menu resolve their labels
// here, so the two cannot drift and a token added to all() cannot be offered by
// one and silently missing from the other.
QString displayName(const QString &p_token);

} // namespace CommentColor

// Helpers for the one anchor type implemented in v1.
namespace PdfQuadsAnchor {

inline QString type() { return QStringLiteral("pdf-quads"); }

// Upper bounds. Everything crossing the QWebChannel bridge is bounded, in the
// same spirit as PdfViewerAdapter::setOutline's 5000-entry cap: a runaway
// selection must not be able to freeze the UI or bloat the sidecar.
inline int maxQuadsPerComment() { return 512; }

inline int maxAnchorTextLength() { return 4096; }

// A quad is 8 doubles: four (x, y) corners in PDF page space.
inline int quadValueCount() { return 8; }

QJsonObject make(int p_page, const QVector<QVector<double>> &p_quads, const QString &p_text);

// Reads back an anchor that has already been validated. Returns -1 when absent.
int page(const QJsonObject &p_anchor);

QString text(const QJsonObject &p_anchor);

// Structural validation of a `pdf-quads` anchor. Anchors of ANY OTHER type are
// out of scope here and must be preserved untouched rather than validated.
bool isValid(const QJsonObject &p_anchor);

} // namespace PdfQuadsAnchor

// Freehand ink, drawn with the Draw tool.
//
//   { "type": "pdf-ink", "page": 3,
//     "strokes": [[x0,y0, x1,y1, ...], ...],   // PDF page space, flat pairs
//     "width": 1.5 }                            // stroke width in PDF units
//
// A separate anchor type rather than an extension of pdf-quads because ink is
// free-form geometry with no text behind it: it cannot be re-anchored to a text
// run, and it has no quoted text to show in the dock.
namespace PdfInkAnchor {

inline QString type() { return QStringLiteral("pdf-ink"); }

// One stroke is a polyline; a scribble is a handful of them. The caps bound
// both a runaway drag and the size of the sidecar.
//
// The AGGREGATE cap is the one that matters: per-stroke and per-anchor caps
// MULTIPLY, so bounding them independently would still permit
// 256 * 4096 = 1M points (2M JSON numbers) in a single comment, times 5000
// comments per file. That is hundreds of times the quads bound and is enough to
// stall validation, QWebChannel conversion, serialization and DOM projection.
inline int maxStrokesPerComment() { return 64; }

inline int maxPointsPerStroke() { return 4096; }

// Total points across ALL strokes of one anchor. Matches the JS tool's own
// per-drag budget, which is the largest thing that can legitimately arrive.
inline int maxPointsPerComment() { return 4096; }

inline double minWidth() { return 0.1; }

inline double maxWidth() { return 64.0; }

QJsonObject make(int p_page, const QVector<QVector<double>> &p_strokes, double p_width);

int page(const QJsonObject &p_anchor);

double width(const QJsonObject &p_anchor);

bool isValid(const QJsonObject &p_anchor);

} // namespace PdfInkAnchor

// A free-standing text box, placed with the Text tool.
//
//   { "type": "pdf-freetext", "page": 3,
//     "x": 100.0, "y": 640.0,     // top-left anchor, PDF page space
//     "fontSize": 12.0 }
//
// The BODY is Comment::m_text, not part of the anchor: a free-text box is a
// comment that happens to be rendered on the page, so editing it in the dock
// and editing it on the page are the same operation.
namespace PdfFreeTextAnchor {

inline QString type() { return QStringLiteral("pdf-freetext"); }

inline double minFontSize() { return 4.0; }

inline double maxFontSize() { return 144.0; }

QJsonObject make(int p_page, double p_x, double p_y, double p_fontSize);

int page(const QJsonObject &p_anchor);

double fontSize(const QJsonObject &p_anchor);

bool isValid(const QJsonObject &p_anchor);

} // namespace PdfFreeTextAnchor

// True for any anchor type this build can render and edit. An anchor of ANY
// other type is still valid-but-opaque and must be carried through untouched.
bool isKnownAnchorType(const QJsonObject &p_anchor);

// Structural validation dispatched on the anchor's own type. Returns false for
// a known type that is malformed, and TRUE for an unknown type (nothing to
// check, and it must survive).
bool isAnchorStructurallyValid(const QJsonObject &p_anchor);

// 0-based page for any known anchor type; -1 when absent or unknown.
int anchorPage(const QJsonObject &p_anchor);

struct Comment {
  static int maxTextLength() { return 16384; }

  // Generates a fresh comment with `id`, `createdUtc` and `modifiedUtc` set.
  static Comment create(const QJsonObject &p_anchor, const QString &p_text, const QString &p_color);

  static Comment fromJson(const QJsonObject &p_obj);

  QJsonObject toJson() const;

  // True when the comment carries enough to be rendered and re-anchored.
  // A comment whose anchor type is unknown to this build is still VALID: it is
  // simply not renderable here, and must survive the round trip.
  bool isValid() const;

  bool hasKnownAnchorType() const;

  QString m_id;

  qint64 m_createdUtc = 0;

  qint64 m_modifiedUtc = 0;

  // May be empty: a bare highlight is a comment with no body.
  QString m_text;

  QString m_color = CommentColor::defaultToken();

  // Stored verbatim. NEVER rebuilt from typed fields — see the note above.
  QJsonObject m_anchor;

  // Top-level keys this build does not know about, preserved on rewrite.
  QJsonObject m_extraKeys;
};

struct CommentSet {
  static int currentVersion() { return 1; }

  // Hard cap on the number of comments in one file. Beyond this the store stops
  // accepting additions rather than degrading the viewer.
  static int maxComments() { return 5000; }

  static CommentSet fromJson(const QJsonObject &p_obj);

  // Emits a STABLY ORDERED document (see sortComments) so a sync/git diff shows
  // only what actually changed and a conflict stays readable.
  QJsonObject toJson() const;

  // Sort key: anchor type, then page, then id. Deterministic for every anchor
  // type, including unknown ones (page defaults to -1).
  void sortComments();

  int indexOfId(const QString &p_id) const;

  int m_version = currentVersion();

  QVector<Comment> m_comments;

  // Unknown top-level document keys, preserved on rewrite.
  QJsonObject m_extraKeys;
};

} // namespace vnotex

#endif // COMMENTTYPES_H
