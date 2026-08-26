#include "commenttypes.h"

#include <algorithm>

#include <cmath>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QUuid>

using namespace vnotex;

namespace {

const char *const c_keyVersion = "version";
const char *const c_keyComments = "comments";
const char *const c_keyId = "id";
const char *const c_keyCreatedUtc = "createdUtc";
const char *const c_keyModifiedUtc = "modifiedUtc";
const char *const c_keyText = "text";
const char *const c_keyColor = "color";
const char *const c_keyAnchor = "anchor";
const char *const c_keyType = "type";
const char *const c_keyPage = "page";
const char *const c_keyQuads = "quads";
const char *const c_keyStrokes = "strokes";
const char *const c_keyWidth = "width";
const char *const c_keyX = "x";
const char *const c_keyY = "y";
const char *const c_keyFontSize = "fontSize";

// These keys are Qt-only: vxcore never reads comments.json, so they stay OUT of
// <vxcore/notebook_json_keys.h> and out of test_json_key_drift's gated list.

QJsonObject extraKeysOf(const QJsonObject &p_obj, const QStringList &p_known) {
  QJsonObject extra;
  for (auto it = p_obj.constBegin(); it != p_obj.constEnd(); ++it) {
    if (!p_known.contains(it.key())) {
      extra.insert(it.key(), it.value());
    }
  }
  return extra;
}

void mergeExtraKeys(QJsonObject &p_target, const QJsonObject &p_extra) {
  for (auto it = p_extra.constBegin(); it != p_extra.constEnd(); ++it) {
    // A known key always wins: m_extraKeys must never be able to shadow the
    // typed fields written above it.
    if (!p_target.contains(it.key())) {
      p_target.insert(it.key(), it.value());
    }
  }
}

// Shared by every anchor type that stores geometry: reject NaN/Inf, which would
// silently poison the overlay projection instead of failing loudly.
bool isFiniteNumber(const QJsonValue &p_value) {
  return p_value.isDouble() && qIsFinite(p_value.toDouble());
}

// A page index must be a NON-NEGATIVE EXACT INTEGER. JSON has only doubles, so
// without this a `"page": 0.5` validates (toInt() truncates to 0), passes the
// page-bound check, and is then stored verbatim -- after which the web side
// buckets it under the key "0.5", never matches a real page, and the annotation
// persists forever without ever rendering.
bool isValidPageValue(const QJsonValue &p_value) {
  if (!isFiniteNumber(p_value)) {
    return false;
  }
  const double raw = p_value.toDouble();
  if (raw < 0.0 || raw > 2147483647.0) {
    return false;
  }
  return raw == std::floor(raw);
}

} // namespace

// ============ CommentColor ============

QString CommentColor::displayName(const QString &p_token) {
  // Explicit table rather than capitalizing the token: the token is an internal
  // identifier, and mechanically upper-casing it would produce untranslatable
  // strings and break for any future multi-word token.
  static const QHash<QString, QString> c_names = {
      {QStringLiteral("yellow"), QCoreApplication::translate("CommentColor", "Yellow")},
      {QStringLiteral("green"), QCoreApplication::translate("CommentColor", "Green")},
      {QStringLiteral("blue"), QCoreApplication::translate("CommentColor", "Blue")},
      {QStringLiteral("pink"), QCoreApplication::translate("CommentColor", "Pink")},
      {QStringLiteral("purple"), QCoreApplication::translate("CommentColor", "Purple")}};

  const auto name = c_names.value(p_token);
  if (!name.isEmpty()) {
    return name;
  }
  // A token with no entry is a bug, but showing the raw token beats showing an
  // empty combo-box row.
  qWarning() << "CommentColor::displayName: no display name for token" << p_token;
  return p_token;
}

// ============ PdfToolOptions ============

QStringList PdfToolOptions::toolNames() {
  return QStringList{highlightTool(), inkTool(), freeTextTool()};
}

bool PdfToolOptions::isValidTool(const QString &p_tool) { return toolNames().contains(p_tool); }

bool PdfToolOptions::hasWidth(const QString &p_tool) { return p_tool == inkTool(); }

bool PdfToolOptions::hasFontSize(const QString &p_tool) { return p_tool == freeTextTool(); }

QJsonObject PdfToolOptions::defaults(const QString &p_tool) {
  QJsonObject obj;
  if (!isValidTool(p_tool)) {
    // An unknown tool has no options at all, rather than a plausible-looking
    // colour-only object a caller might then persist.
    return obj;
  }
  obj.insert(colorKey(), CommentColor::defaultToken());
  if (hasWidth(p_tool)) {
    obj.insert(widthKey(), defaultWidth());
  }
  if (hasFontSize(p_tool)) {
    obj.insert(fontSizeKey(), defaultFontSize());
  }
  return obj;
}

namespace {
// The scalar half of the Task 0 table, shared by width and font size.
//
// Non-finite is NOT clamped: a NaN carries no intent to preserve, whereas
// "width 1e9" plainly means "as thick as possible" and clamping respects it.
double normalizeScalar(const QJsonValue &p_value, double p_default, double p_min, double p_max) {
  if (!p_value.isDouble()) {
    // Absent, or a string/bool/array where a number belongs.
    return p_default;
  }
  const double value = p_value.toDouble();
  if (!std::isfinite(value)) {
    return p_default;
  }
  return qBound(p_min, value, p_max);
}
} // namespace

QJsonObject PdfToolOptions::normalize(const QString &p_tool, const QJsonObject &p_options) {
  QJsonObject obj = defaults(p_tool);
  if (obj.isEmpty()) {
    return obj;
  }

  const auto colorValue = p_options.value(colorKey());
  if (colorValue.isString() && CommentColor::isValid(colorValue.toString())) {
    obj.insert(colorKey(), colorValue.toString());
  }

  if (hasWidth(p_tool)) {
    obj.insert(widthKey(), normalizeScalar(p_options.value(widthKey()), defaultWidth(),
                                           PdfInkAnchor::minWidth(), PdfInkAnchor::maxWidth()));
  }
  if (hasFontSize(p_tool)) {
    obj.insert(fontSizeKey(),
               normalizeScalar(p_options.value(fontSizeKey()), defaultFontSize(),
                               PdfFreeTextAnchor::minFontSize(), PdfFreeTextAnchor::maxFontSize()));
  }

  return obj;
}

// ============ PdfQuadsAnchor ============

QJsonObject PdfQuadsAnchor::make(int p_page, const QVector<QVector<double>> &p_quads,
                                 const QString &p_text) {
  QJsonArray quads;
  for (const auto &quad : p_quads) {
    QJsonArray values;
    for (double v : quad) {
      values.append(v);
    }
    quads.append(values);
  }

  QJsonObject anchor;
  anchor.insert(QLatin1String(c_keyType), type());
  anchor.insert(QLatin1String(c_keyPage), p_page);
  anchor.insert(QLatin1String(c_keyQuads), quads);
  anchor.insert(QLatin1String(c_keyText), p_text.left(maxAnchorTextLength()));
  return anchor;
}

int PdfQuadsAnchor::page(const QJsonObject &p_anchor) {
  const auto value = p_anchor.value(QLatin1String(c_keyPage));
  return isValidPageValue(value) ? value.toInt() : -1;
}

QString PdfQuadsAnchor::text(const QJsonObject &p_anchor) {
  return p_anchor.value(QLatin1String(c_keyText)).toString();
}

bool PdfQuadsAnchor::isValid(const QJsonObject &p_anchor) {
  if (p_anchor.value(QLatin1String(c_keyType)).toString() != type()) {
    return false;
  }

  const int pageIndex = page(p_anchor);
  if (pageIndex < 0) {
    return false;
  }

  const auto quadsValue = p_anchor.value(QLatin1String(c_keyQuads));
  if (!quadsValue.isArray()) {
    return false;
  }

  const auto quads = quadsValue.toArray();
  if (quads.isEmpty() || quads.size() > maxQuadsPerComment()) {
    return false;
  }

  for (const auto &quadValue : quads) {
    if (!quadValue.isArray()) {
      return false;
    }
    const auto quad = quadValue.toArray();
    if (quad.size() != quadValueCount()) {
      return false;
    }
    for (const auto &coordinate : quad) {
      // NaN/Inf would silently poison the overlay projection on the web side.
      if (!coordinate.isDouble() || !qIsFinite(coordinate.toDouble())) {
        return false;
      }
    }
  }

  return true;
}

// ============ PdfInkAnchor ============

QJsonObject PdfInkAnchor::make(int p_page, const QVector<QVector<double>> &p_strokes,
                               double p_width) {
  QJsonArray strokes;
  for (const auto &stroke : p_strokes) {
    QJsonArray points;
    for (double v : stroke) {
      points.append(v);
    }
    strokes.append(points);
  }

  QJsonObject anchor;
  anchor.insert(QLatin1String(c_keyType), type());
  anchor.insert(QLatin1String(c_keyPage), p_page);
  anchor.insert(QLatin1String(c_keyStrokes), strokes);
  anchor.insert(QLatin1String(c_keyWidth), qBound(minWidth(), p_width, maxWidth()));
  return anchor;
}

int PdfInkAnchor::page(const QJsonObject &p_anchor) {
  const auto value = p_anchor.value(QLatin1String(c_keyPage));
  return isValidPageValue(value) ? value.toInt() : -1;
}

double PdfInkAnchor::width(const QJsonObject &p_anchor) {
  const auto value = p_anchor.value(QLatin1String(c_keyWidth));
  return value.isDouble() ? value.toDouble() : 1.0;
}

bool PdfInkAnchor::isValid(const QJsonObject &p_anchor) {
  if (p_anchor.value(QLatin1String(c_keyType)).toString() != type()) {
    return false;
  }
  if (page(p_anchor) < 0) {
    return false;
  }

  const auto widthValue = p_anchor.value(QLatin1String(c_keyWidth));
  if (!isFiniteNumber(widthValue)) {
    return false;
  }
  const double w = widthValue.toDouble();
  if (w < minWidth() || w > maxWidth()) {
    return false;
  }

  const auto strokesValue = p_anchor.value(QLatin1String(c_keyStrokes));
  if (!strokesValue.isArray()) {
    return false;
  }
  const auto strokes = strokesValue.toArray();
  if (strokes.isEmpty() || strokes.size() > maxStrokesPerComment()) {
    return false;
  }
  // The per-stroke and per-anchor caps MULTIPLY, so the aggregate is tracked
  // separately -- see maxPointsPerComment() for why that matters.
  qint64 totalPoints = 0;
  for (const auto &strokeValue : strokes) {
    if (!strokeValue.isArray()) {
      return false;
    }
    const auto stroke = strokeValue.toArray();
    // Flat x,y pairs, so an odd count is malformed. A single point is a legal
    // dot; an empty stroke is not.
    if (stroke.size() < 2 || (stroke.size() % 2) != 0 || stroke.size() > maxPointsPerStroke() * 2) {
      return false;
    }
    totalPoints += stroke.size() / 2;
    if (totalPoints > maxPointsPerComment()) {
      return false;
    }
    for (const auto &coordinate : stroke) {
      if (!isFiniteNumber(coordinate)) {
        return false;
      }
    }
  }

  return true;
}

// ============ PdfFreeTextAnchor ============

QJsonObject PdfFreeTextAnchor::make(int p_page, double p_x, double p_y, double p_fontSize) {
  QJsonObject anchor;
  anchor.insert(QLatin1String(c_keyType), type());
  anchor.insert(QLatin1String(c_keyPage), p_page);
  anchor.insert(QLatin1String(c_keyX), p_x);
  anchor.insert(QLatin1String(c_keyY), p_y);
  anchor.insert(QLatin1String(c_keyFontSize), qBound(minFontSize(), p_fontSize, maxFontSize()));
  return anchor;
}

int PdfFreeTextAnchor::page(const QJsonObject &p_anchor) {
  const auto value = p_anchor.value(QLatin1String(c_keyPage));
  return isValidPageValue(value) ? value.toInt() : -1;
}

double PdfFreeTextAnchor::fontSize(const QJsonObject &p_anchor) {
  const auto value = p_anchor.value(QLatin1String(c_keyFontSize));
  return value.isDouble() ? value.toDouble() : 12.0;
}

bool PdfFreeTextAnchor::isValid(const QJsonObject &p_anchor) {
  if (p_anchor.value(QLatin1String(c_keyType)).toString() != type()) {
    return false;
  }
  if (page(p_anchor) < 0) {
    return false;
  }
  if (!isFiniteNumber(p_anchor.value(QLatin1String(c_keyX))) ||
      !isFiniteNumber(p_anchor.value(QLatin1String(c_keyY)))) {
    return false;
  }

  const auto sizeValue = p_anchor.value(QLatin1String(c_keyFontSize));
  if (!isFiniteNumber(sizeValue)) {
    return false;
  }
  const double size = sizeValue.toDouble();
  return size >= minFontSize() && size <= maxFontSize();
}

// ============ Anchor dispatch ============

bool vnotex::isKnownAnchorType(const QJsonObject &p_anchor) {
  const auto type = p_anchor.value(QLatin1String(c_keyType)).toString();
  return type == PdfQuadsAnchor::type() || type == PdfInkAnchor::type() ||
         type == PdfFreeTextAnchor::type();
}

bool vnotex::isAnchorStructurallyValid(const QJsonObject &p_anchor) {
  const auto type = p_anchor.value(QLatin1String(c_keyType)).toString();
  if (type == PdfQuadsAnchor::type()) {
    return PdfQuadsAnchor::isValid(p_anchor);
  }
  if (type == PdfInkAnchor::type()) {
    return PdfInkAnchor::isValid(p_anchor);
  }
  if (type == PdfFreeTextAnchor::type()) {
    return PdfFreeTextAnchor::isValid(p_anchor);
  }
  // Unknown but non-empty: nothing this build can check, and it must survive a
  // round trip untouched.
  return !type.isEmpty();
}

int vnotex::anchorPage(const QJsonObject &p_anchor) {
  const auto type = p_anchor.value(QLatin1String(c_keyType)).toString();
  if (type == PdfQuadsAnchor::type() || type == PdfInkAnchor::type() ||
      type == PdfFreeTextAnchor::type()) {
    const auto value = p_anchor.value(QLatin1String(c_keyPage));
    return isValidPageValue(value) ? value.toInt() : -1;
  }
  return -1;
}

// ============ Comment ============

Comment Comment::create(const QJsonObject &p_anchor, const QString &p_text,
                        const QString &p_color) {
  const qint64 now = QDateTime::currentSecsSinceEpoch();

  Comment comment;
  comment.m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  comment.m_createdUtc = now;
  comment.m_modifiedUtc = now;
  comment.m_text = p_text.left(maxTextLength());
  comment.m_color = CommentColor::isValid(p_color) ? p_color : CommentColor::defaultToken();
  comment.m_anchor = p_anchor;
  return comment;
}

Comment Comment::fromJson(const QJsonObject &p_obj) {
  static const QStringList c_known{QLatin1String(c_keyId),          QLatin1String(c_keyCreatedUtc),
                                   QLatin1String(c_keyModifiedUtc), QLatin1String(c_keyText),
                                   QLatin1String(c_keyColor),       QLatin1String(c_keyAnchor)};

  Comment comment;
  comment.m_id = p_obj.value(QLatin1String(c_keyId)).toString();
  comment.m_createdUtc =
      static_cast<qint64>(p_obj.value(QLatin1String(c_keyCreatedUtc)).toDouble());
  comment.m_modifiedUtc =
      static_cast<qint64>(p_obj.value(QLatin1String(c_keyModifiedUtc)).toDouble());
  comment.m_text = p_obj.value(QLatin1String(c_keyText)).toString().left(maxTextLength());

  const auto color = p_obj.value(QLatin1String(c_keyColor)).toString();
  comment.m_color = CommentColor::isValid(color) ? color : CommentColor::defaultToken();

  comment.m_anchor = p_obj.value(QLatin1String(c_keyAnchor)).toObject();
  comment.m_extraKeys = extraKeysOf(p_obj, c_known);
  return comment;
}

QJsonObject Comment::toJson() const {
  QJsonObject obj;
  obj.insert(QLatin1String(c_keyId), m_id);
  obj.insert(QLatin1String(c_keyCreatedUtc), static_cast<double>(m_createdUtc));
  obj.insert(QLatin1String(c_keyModifiedUtc), static_cast<double>(m_modifiedUtc));
  obj.insert(QLatin1String(c_keyText), m_text);
  obj.insert(QLatin1String(c_keyColor), m_color);
  obj.insert(QLatin1String(c_keyAnchor), m_anchor);
  mergeExtraKeys(obj, m_extraKeys);
  return obj;
}

bool Comment::hasKnownAnchorType() const { return isKnownAnchorType(m_anchor); }

bool Comment::isValid() const {
  if (m_id.isEmpty()) {
    return false;
  }
  if (m_anchor.value(QLatin1String(c_keyType)).toString().isEmpty()) {
    return false;
  }
  // An anchor type this build does not implement is valid-but-opaque: it is
  // carried through untouched so a newer VNote's comments survive a round trip
  // through this one.
  // Dispatches on the anchor's own type, and returns true for an unknown one:
  // valid-but-opaque, carried through untouched.
  return isAnchorStructurallyValid(m_anchor);
}

// ============ CommentSet ============

CommentSet CommentSet::fromJson(const QJsonObject &p_obj) {
  static const QStringList c_known{QLatin1String(c_keyVersion), QLatin1String(c_keyComments)};

  CommentSet set;
  const auto version = p_obj.value(QLatin1String(c_keyVersion));
  set.m_version = version.isDouble() ? version.toInt() : currentVersion();

  const auto comments = p_obj.value(QLatin1String(c_keyComments)).toArray();
  set.m_comments.reserve(comments.size());
  for (const auto &value : comments) {
    if (!value.isObject()) {
      continue;
    }
    auto comment = Comment::fromJson(value.toObject());
    // A structurally broken entry is DROPPED rather than carried: keeping it
    // would let one corrupt record break the overlay on every future load.
    if (!comment.isValid()) {
      continue;
    }
    if (set.m_comments.size() >= maxComments()) {
      break;
    }
    set.m_comments.append(comment);
  }

  set.m_extraKeys = extraKeysOf(p_obj, c_known);
  return set;
}

QJsonObject CommentSet::toJson() const {
  CommentSet ordered = *this;
  ordered.sortComments();

  QJsonArray comments;
  for (const auto &comment : ordered.m_comments) {
    comments.append(comment.toJson());
  }

  QJsonObject obj;
  obj.insert(QLatin1String(c_keyVersion), m_version);
  obj.insert(QLatin1String(c_keyComments), comments);
  mergeExtraKeys(obj, m_extraKeys);
  return obj;
}

void CommentSet::sortComments() {
  std::stable_sort(m_comments.begin(), m_comments.end(),
                   [](const Comment &p_a, const Comment &p_b) {
                     const auto typeA = p_a.m_anchor.value(QLatin1String(c_keyType)).toString();
                     const auto typeB = p_b.m_anchor.value(QLatin1String(c_keyType)).toString();
                     if (typeA != typeB) {
                       return typeA < typeB;
                     }
                     // page is meaningful for pdf-quads and defaults to -1 for
                     // any anchor type that does not carry one, which keeps the
                     // ordering total for unknown types too.
                     const int pageA = anchorPage(p_a.m_anchor);
                     const int pageB = anchorPage(p_b.m_anchor);
                     if (pageA != pageB) {
                       return pageA < pageB;
                     }
                     return p_a.m_id < p_b.m_id;
                   });
}

int CommentSet::indexOfId(const QString &p_id) const {
  for (int i = 0; i < m_comments.size(); ++i) {
    if (m_comments[i].m_id == p_id) {
      return i;
    }
  }
  return -1;
}
