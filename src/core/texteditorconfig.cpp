#include "texteditorconfig.h"

using namespace vnotex;

#define READSTR(key) readString(p_jobj, (key))
#define READBOOL(key) readBool(p_jobj, (key))
#define READINT(key) readInt(p_jobj, (key))

TextEditorConfig::TextEditorConfig(IConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig) {
  m_sectionName = QStringLiteral("text_editor");
}

void TextEditorConfig::fromJson(const QJsonObject &p_jobj) {

  {
    auto lineNumber = READSTR(QStringLiteral("lineNumber"));
    m_lineNumberType = stringToLineNumberType(lineNumber);
  }

  m_textFoldingEnabled = READBOOL(QStringLiteral("textFolding"));

  {
    auto inputMode = READSTR(QStringLiteral("inputMode"));
    m_inputMode = stringToInputMode(inputMode);
  }

  {
    auto centerCursor = READSTR(QStringLiteral("centerCursor"));
    m_centerCursor = stringToCenterCursor(centerCursor);
  }

  {
    auto wrapMode = READSTR(QStringLiteral("wrapMode"));
    m_wrapMode = stringToWrapMode(wrapMode);
  }

  m_expandTab = READBOOL(QStringLiteral("expandTab"));

  m_tabStopWidth = READINT(QStringLiteral("tabStopWidth"));

  m_highlightWhitespace = READBOOL(QStringLiteral("highlightWhitespace"));

  m_zoomDelta = READINT(QStringLiteral("zoomDelta"));

  m_spellCheckEnabled = READBOOL(QStringLiteral("spellCheck"));

  m_lineSpacing = p_jobj.value(QStringLiteral("lineSpacing")).toDouble(1.0);
}

QJsonObject TextEditorConfig::toJson() const {
  QJsonObject obj;
  obj[QStringLiteral("lineNumber")] = lineNumberTypeToString(m_lineNumberType);
  obj[QStringLiteral("textFolding")] = m_textFoldingEnabled;
  obj[QStringLiteral("inputMode")] = inputModeToString(m_inputMode);
  obj[QStringLiteral("centerCursor")] = centerCursorToString(m_centerCursor);
  obj[QStringLiteral("wrapMode")] = wrapModeToString(m_wrapMode);
  obj[QStringLiteral("expandTab")] = m_expandTab;
  obj[QStringLiteral("tabStopWidth")] = m_tabStopWidth;
  obj[QStringLiteral("highlightWhitespace")] = m_highlightWhitespace;
  obj[QStringLiteral("zoomDelta")] = m_zoomDelta;
  obj[QStringLiteral("spellCheck")] = m_spellCheckEnabled;
  obj[QStringLiteral("lineSpacing")] = m_lineSpacing;
  return obj;
}

QString TextEditorConfig::lineNumberTypeToString(LineNumberType p_type) const {
  switch (p_type) {
  case LineNumberType::None:
    return QStringLiteral("none");

  case LineNumberType::Relative:
    return QStringLiteral("relative");

  default:
    return QStringLiteral("absolute");
  }
}

TextEditorConfig::LineNumberType
TextEditorConfig::stringToLineNumberType(const QString &p_str) const {
  auto lineNumber = p_str.toLower();
  if (lineNumber == QStringLiteral("none")) {
    return LineNumberType::None;
  } else if (lineNumber == QStringLiteral("relative")) {
    return LineNumberType::Relative;
  } else {
    return LineNumberType::Absolute;
  }
}

QString TextEditorConfig::inputModeToString(InputMode p_mode) const {
  switch (p_mode) {
  case InputMode::ViMode:
    return QStringLiteral("vi");

  case InputMode::VscodeMode:
    return QStringLiteral("vscode");

  default:
    return QStringLiteral("normal");
  }
}

TextEditorConfig::InputMode TextEditorConfig::stringToInputMode(const QString &p_str) const {
  auto inputMode = p_str.toLower();
  if (inputMode == QStringLiteral("vi")) {
    return InputMode::ViMode;
  } else if (inputMode == QStringLiteral("vscode")) {
    return InputMode::VscodeMode;
  } else {
    return InputMode::NormalMode;
  }
}

QString TextEditorConfig::centerCursorToString(CenterCursor p_cursor) const {
  switch (p_cursor) {
  case CenterCursor::AlwaysCenter:
    return QStringLiteral("always");

  case CenterCursor::CenterOnBottom:
    return QStringLiteral("bottom");

  default:
    return QStringLiteral("never");
  }
}

TextEditorConfig::CenterCursor TextEditorConfig::stringToCenterCursor(const QString &p_str) const {
  auto centerCursor = p_str.toLower();
  if (centerCursor == QStringLiteral("always")) {
    return CenterCursor::AlwaysCenter;
  } else if (centerCursor == QStringLiteral("bottom")) {
    return CenterCursor::CenterOnBottom;
  } else {
    return CenterCursor::NeverCenter;
  }
}

QString TextEditorConfig::wrapModeToString(WrapMode p_mode) const {
  switch (p_mode) {
  case WrapMode::NoWrap:
    return QStringLiteral("none");

  case WrapMode::WrapAnywhere:
    return QStringLiteral("anywhere");

  case WrapMode::WordWrapOrAnywhere:
    return QStringLiteral("word_anywhere");

  default:
    return QStringLiteral("word");
  }
}

TextEditorConfig::WrapMode TextEditorConfig::stringToWrapMode(const QString &p_str) const {
  auto centerCursor = p_str.toLower();
  if (centerCursor == QStringLiteral("none")) {
    return WrapMode::NoWrap;
  } else if (centerCursor == QStringLiteral("anywhere")) {
    return WrapMode::WrapAnywhere;
  } else if (centerCursor == QStringLiteral("word_anywhere")) {
    return WrapMode::WordWrapOrAnywhere;
  } else {
    return WrapMode::WordWrap;
  }
}

TextEditorConfig::LineNumberType TextEditorConfig::getLineNumberType() const {
  return m_lineNumberType;
}

void TextEditorConfig::setLineNumberType(TextEditorConfig::LineNumberType p_type) {
  updateConfig(m_lineNumberType, p_type, this);
}

bool TextEditorConfig::getTextFoldingEnabled() const { return m_textFoldingEnabled; }

void TextEditorConfig::setTextFoldingEnabled(bool p_enabled) {
  updateConfig(m_textFoldingEnabled, p_enabled, this);
}

TextEditorConfig::InputMode TextEditorConfig::getInputMode() const { return m_inputMode; }

void TextEditorConfig::setInputMode(TextEditorConfig::InputMode p_mode) {
  updateConfig(m_inputMode, p_mode, this);
}

TextEditorConfig::CenterCursor TextEditorConfig::getCenterCursor() const { return m_centerCursor; }

void TextEditorConfig::setCenterCursor(TextEditorConfig::CenterCursor p_centerCursor) {
  updateConfig(m_centerCursor, p_centerCursor, this);
}

TextEditorConfig::WrapMode TextEditorConfig::getWrapMode() const { return m_wrapMode; }

void TextEditorConfig::setWrapMode(TextEditorConfig::WrapMode p_mode) {
  updateConfig(m_wrapMode, p_mode, this);
}

bool TextEditorConfig::getExpandTabEnabled() const { return m_expandTab; }

void TextEditorConfig::setExpandTabEnabled(bool p_enabled) {
  updateConfig(m_expandTab, p_enabled, this);
}

bool TextEditorConfig::getHighlightWhitespaceEnabled() const { return m_highlightWhitespace; }

void TextEditorConfig::setHighlightWhitespaceEnabled(bool p_enabled) {
  updateConfig(m_highlightWhitespace, p_enabled, this);
}

int TextEditorConfig::getTabStopWidth() const { return m_tabStopWidth; }

void TextEditorConfig::setTabStopWidth(int p_width) { updateConfig(m_tabStopWidth, p_width, this); }

int TextEditorConfig::getZoomDelta() const { return m_zoomDelta; }

void TextEditorConfig::setZoomDelta(int p_delta) { updateConfig(m_zoomDelta, p_delta, this); }

bool TextEditorConfig::isSpellCheckEnabled() const { return m_spellCheckEnabled; }

void TextEditorConfig::setSpellCheckEnabled(bool p_enabled) {
  updateConfig(m_spellCheckEnabled, p_enabled, this);
}

qreal TextEditorConfig::getLineSpacing() const { return m_lineSpacing; }

void TextEditorConfig::setLineSpacing(qreal p_spacing) {
  updateConfig(m_lineSpacing, p_spacing, this);
}
