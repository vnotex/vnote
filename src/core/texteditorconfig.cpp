#include "texteditorconfig.h"

using namespace vnotex;

#define READSTR(key) readString(appObj, userObj, (key))
#define READBOOL(key) readBool(appObj, userObj, (key))
#define READINT(key) readInt(appObj, userObj, (key))

TextEditorConfig::TextEditorConfig(ConfigMgr *p_mgr, IConfig *p_topConfig)
    : IConfig(p_mgr, p_topConfig)
{
    m_sessionName = QStringLiteral("text_editor");
}

void TextEditorConfig::init(const QJsonObject &p_app,
                            const QJsonObject &p_user)
{
    const auto appObj = p_app.value(m_sessionName).toObject();
    const auto userObj = p_user.value(m_sessionName).toObject();

    {
        auto lineNumber = READSTR(QStringLiteral("line_number"));
        m_lineNumberType = stringToLineNumberType(lineNumber);
    }

    m_textFoldingEnabled = READBOOL(QStringLiteral("text_folding"));

    {
        auto inputMode = READSTR(QStringLiteral("input_mode"));
        m_inputMode = stringToInputMode(inputMode);
    }

    {
        auto centerCursor = READSTR(QStringLiteral("center_cursor"));
        m_centerCursor = stringToCenterCursor(centerCursor);
    }

    {
        auto wrapMode = READSTR(QStringLiteral("wrap_mode"));
        m_wrapMode = stringToWrapMode(wrapMode);
    }

    m_expandTab = READBOOL(QStringLiteral("expand_tab"));

    m_tabStopWidth = READINT(QStringLiteral("tab_stop_width"));

    m_zoomDelta = READINT(QStringLiteral("zoom_delta"));
}

QJsonObject TextEditorConfig::toJson() const
{
    QJsonObject obj;
    obj[QStringLiteral("line_number")] = lineNumberTypeToString(m_lineNumberType);
    obj[QStringLiteral("text_folding")] = m_textFoldingEnabled;
    obj[QStringLiteral("input_mode")] = inputModeToString(m_inputMode);
    obj[QStringLiteral("center_cursor")] = centerCursorToString(m_centerCursor);
    obj[QStringLiteral("wrap_mode")] = wrapModeToString(m_wrapMode);
    obj[QStringLiteral("expand_tab")] = m_expandTab;
    obj[QStringLiteral("tab_stop_width")] = m_tabStopWidth;
    obj[QStringLiteral("zoom_delta")] = m_zoomDelta;
    return obj;
}

QString TextEditorConfig::lineNumberTypeToString(LineNumberType p_type) const
{
    switch (p_type) {
    case LineNumberType::None:
        return QStringLiteral("none");

    case LineNumberType::Relative:
        return QStringLiteral("relative");

    default:
        return QStringLiteral("absolute");
    }
}

TextEditorConfig::LineNumberType TextEditorConfig::stringToLineNumberType(const QString &p_str) const
{
    auto lineNumber = p_str.toLower();
    if (lineNumber == QStringLiteral("none")) {
        return LineNumberType::None;
    } else if (lineNumber == QStringLiteral("relative")) {
        return LineNumberType::Relative;
    } else {
        return LineNumberType::Absolute;
    }
}

QString TextEditorConfig::inputModeToString(InputMode p_mode) const
{
    switch (p_mode) {
    case InputMode::ViMode:
        return QStringLiteral("vi");

    default:
        return QStringLiteral("normal");
    }
}

TextEditorConfig::InputMode TextEditorConfig::stringToInputMode(const QString &p_str) const
{
    auto inputMode = p_str.toLower();
    if (inputMode == QStringLiteral("vi")) {
        return InputMode::ViMode;
    } else {
        return InputMode::NormalMode;
    }
}

QString TextEditorConfig::centerCursorToString(CenterCursor p_cursor) const
{
    switch (p_cursor) {
    case CenterCursor::AlwaysCenter:
        return QStringLiteral("always");

    case CenterCursor::CenterOnBottom:
        return QStringLiteral("bottom");

    default:
        return QStringLiteral("never");
    }
}

TextEditorConfig::CenterCursor TextEditorConfig::stringToCenterCursor(const QString &p_str) const
{
    auto centerCursor = p_str.toLower();
    if (centerCursor == QStringLiteral("always")) {
        return CenterCursor::AlwaysCenter;
    } else if (centerCursor == QStringLiteral("bottom")) {
        return CenterCursor::CenterOnBottom;
    } else {
        return CenterCursor::NeverCenter;
    }
}

QString TextEditorConfig::wrapModeToString(WrapMode p_mode) const
{
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

TextEditorConfig::WrapMode TextEditorConfig::stringToWrapMode(const QString &p_str) const
{
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

TextEditorConfig::LineNumberType TextEditorConfig::getLineNumberType() const
{
    return m_lineNumberType;
}

void TextEditorConfig::setLineNumberType(TextEditorConfig::LineNumberType p_type)
{
    updateConfig(m_lineNumberType, p_type, this);
}

bool TextEditorConfig::getTextFoldingEnabled() const
{
    return m_textFoldingEnabled;
}

void TextEditorConfig::setTextFoldingEnabled(bool p_enable)
{
    updateConfig(m_textFoldingEnabled, p_enable, this);
}

TextEditorConfig::InputMode TextEditorConfig::getInputMode() const
{
    return m_inputMode;
}

void TextEditorConfig::setInputMode(TextEditorConfig::InputMode p_mode)
{
    updateConfig(m_inputMode, p_mode, this);
}

TextEditorConfig::CenterCursor TextEditorConfig::getCenterCursor() const
{
    return m_centerCursor;
}

void TextEditorConfig::setCenterCursor(TextEditorConfig::CenterCursor p_centerCursor)
{
    updateConfig(m_centerCursor, p_centerCursor, this);
}

TextEditorConfig::WrapMode TextEditorConfig::getWrapMode() const
{
    return m_wrapMode;
}

void TextEditorConfig::setWrapMode(TextEditorConfig::WrapMode p_mode)
{
    updateConfig(m_wrapMode, p_mode, this);
}

bool TextEditorConfig::getExpandTabEnabled() const
{
    return m_expandTab;
}

void TextEditorConfig::setExpandTabEnabled(bool p_enable)
{
    updateConfig(m_expandTab, p_enable, this);
}

int TextEditorConfig::getTabStopWidth() const
{
    return m_tabStopWidth;
}

void TextEditorConfig::setTabStopWidth(int p_width)
{
    updateConfig(m_tabStopWidth, p_width, this);
}

int TextEditorConfig::getZoomDelta() const
{
    return m_zoomDelta;
}

void TextEditorConfig::setZoomDelta(int p_delta)
{
    updateConfig(m_zoomDelta, p_delta, this);
}
