#ifndef FILEOPENSETTINGS_H
#define FILEOPENSETTINGS_H

#include <QStringList>
#include <QVariantMap>

#include "global.h"

namespace vnotex {

// Search highlight context for in-page search highlighting after opening a file.
// Plain value struct — not a QObject.
struct SearchHighlightContext {
  // Keyword patterns to highlight.
  QStringList m_patterns;

  // Find options (case-sensitive, whole-word, regex, etc.).
  FindOptions m_options = FindNone;

  // 0-based line number of the current match to scroll to. -1 means none.
  int m_currentMatchLine = -1;

  // Whether this context carries valid search data.
  bool m_isValid = false;

  QVariantMap toVariantMap() const {
    QVariantMap map;
    map[QStringLiteral("patterns")] = QVariant::fromValue(m_patterns);
    map[QStringLiteral("options")] = static_cast<int>(m_options);
    map[QStringLiteral("currentMatchLine")] = m_currentMatchLine;
    map[QStringLiteral("isValid")] = m_isValid;
    return map;
  }

  static SearchHighlightContext fromVariantMap(const QVariantMap &p_map) {
    SearchHighlightContext ctx;
    if (p_map.contains(QStringLiteral("patterns"))) {
      ctx.m_patterns = p_map.value(QStringLiteral("patterns")).toStringList();
    }
    if (p_map.contains(QStringLiteral("options"))) {
      ctx.m_options = static_cast<FindOptions>(p_map.value(QStringLiteral("options")).toInt());
    }
    if (p_map.contains(QStringLiteral("currentMatchLine"))) {
      ctx.m_currentMatchLine = p_map.value(QStringLiteral("currentMatchLine")).toInt();
    }
    if (p_map.contains(QStringLiteral("isValid"))) {
      ctx.m_isValid = p_map.value(QStringLiteral("isValid")).toBool();
    }
    return ctx;
  }
};

// Clean, service-layer struct describing how to open a file buffer.
// Unlike FileOpenParameters (UI-layer), this struct contains no UI-specific types
// (no Node*, SearchToken, std::function hooks, etc.) and is safe to pass through
// the service layer and into hook arguments.
struct FileOpenSettings {
  ViewWindowMode m_mode = ViewWindowMode::Read;

  // Force to enter m_mode.
  bool m_forceMode = false;

  // Whether focus to the opened window.
  bool m_focus = true;

  // Whether it is a new file.
  bool m_newFile = false;

  // Open as read-only.
  bool m_readOnly = false;

  // If m_lineNumber > -1, it indicates the line to scroll to after opening the file.
  // 0-based.
  int m_lineNumber = -1;

  // Whether always open a new window for file.
  bool m_alwaysNewWindow = false;

  // Search highlight context for in-page highlighting after open.
  SearchHighlightContext m_searchHighlight;

  // Serialize all fields into a flat QVariantMap suitable for hook arguments.
  QVariantMap toVariantMap() const {
    QVariantMap map;
    map[QStringLiteral("mode")] = static_cast<int>(m_mode);
    map[QStringLiteral("forceMode")] = m_forceMode;
    map[QStringLiteral("focus")] = m_focus;
    map[QStringLiteral("newFile")] = m_newFile;
    map[QStringLiteral("readOnly")] = m_readOnly;
    map[QStringLiteral("lineNumber")] = m_lineNumber;
    map[QStringLiteral("alwaysNewWindow")] = m_alwaysNewWindow;
    if (m_searchHighlight.m_isValid) {
      map[QStringLiteral("searchHighlight")] = m_searchHighlight.toVariantMap();
    }
    return map;
  }

  // Deserialize from a QVariantMap (e.g. from hook arguments).
  // Missing keys fall back to default values.
  static FileOpenSettings fromVariantMap(const QVariantMap &p_map) {
    FileOpenSettings s;
    if (p_map.contains(QStringLiteral("mode"))) {
      s.m_mode = static_cast<ViewWindowMode>(p_map.value(QStringLiteral("mode")).toInt());
    }
    if (p_map.contains(QStringLiteral("forceMode"))) {
      s.m_forceMode = p_map.value(QStringLiteral("forceMode")).toBool();
    }
    if (p_map.contains(QStringLiteral("focus"))) {
      s.m_focus = p_map.value(QStringLiteral("focus")).toBool();
    }
    if (p_map.contains(QStringLiteral("newFile"))) {
      s.m_newFile = p_map.value(QStringLiteral("newFile")).toBool();
    }
    if (p_map.contains(QStringLiteral("readOnly"))) {
      s.m_readOnly = p_map.value(QStringLiteral("readOnly")).toBool();
    }
    if (p_map.contains(QStringLiteral("lineNumber"))) {
      s.m_lineNumber = p_map.value(QStringLiteral("lineNumber")).toInt();
    }
    if (p_map.contains(QStringLiteral("alwaysNewWindow"))) {
      s.m_alwaysNewWindow = p_map.value(QStringLiteral("alwaysNewWindow")).toBool();
    }
    if (p_map.contains(QStringLiteral("searchHighlight"))) {
      s.m_searchHighlight = SearchHighlightContext::fromVariantMap(
          p_map.value(QStringLiteral("searchHighlight")).toMap());
    }
    return s;
  }
};

} // namespace vnotex

#endif // FILEOPENSETTINGS_H
