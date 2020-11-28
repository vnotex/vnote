#ifndef VNOTEX_TEXTEDITORCONFIG_H
#define VNOTEX_TEXTEDITORCONFIG_H

#include "iconfig.h"

namespace vnotex
{
    class TextEditorConfig : public IConfig
    {
    public:
        enum class LineNumberType
        {
            None,
            Absolute,
            Relative
        };

        enum class InputMode
        {
            NormalMode,
            ViMode
        };

        enum class CenterCursor
        {
            NeverCenter,
            AlwaysCenter,
            CenterOnBottom
        };

        enum class WrapMode
        {
            NoWrap,
            WordWrap,
            WrapAnywhere,
            WordWrapOrAnywhere
        };

        TextEditorConfig(ConfigMgr *p_mgr, IConfig *p_topConfig);

        void init(const QJsonObject &p_app, const QJsonObject &p_user) Q_DECL_OVERRIDE;

        QJsonObject toJson() const Q_DECL_OVERRIDE;

        TextEditorConfig::LineNumberType getLineNumberType() const;
        void setLineNumberType(TextEditorConfig::LineNumberType p_type);

        bool getTextFoldingEnabled() const;
        void setTextFoldingEnabled(bool p_enable);

        TextEditorConfig::InputMode getInputMode() const;
        void setInputMode(TextEditorConfig::InputMode p_mode);

        TextEditorConfig::CenterCursor getCenterCursor() const;
        void setCenterCursor(TextEditorConfig::CenterCursor p_centerCursor);

        TextEditorConfig::WrapMode getWrapMode() const;
        void setWrapMode(TextEditorConfig::WrapMode p_mode);

        bool getExpandTabEnabled() const;
        void setExpandTabEnabled(bool p_enable);

        int getTabStopWidth() const;
        void setTabStopWidth(int p_width);

        int getZoomDelta() const;
        void setZoomDelta(int p_delta);

    private:
        QString lineNumberTypeToString(LineNumberType p_type) const;
        LineNumberType stringToLineNumberType(const QString &p_str) const;

        QString inputModeToString(InputMode p_mode) const;
        InputMode stringToInputMode(const QString &p_str) const;

        QString centerCursorToString(CenterCursor p_cursor) const;
        CenterCursor stringToCenterCursor(const QString &p_str) const;

        QString wrapModeToString(WrapMode p_mode) const;
        WrapMode stringToWrapMode(const QString &p_str) const;

        LineNumberType m_lineNumberType = LineNumberType::Absolute;

        bool m_textFoldingEnabled = true;

        InputMode m_inputMode = InputMode::NormalMode;

        CenterCursor m_centerCursor = CenterCursor::NeverCenter;

        WrapMode m_wrapMode = WrapMode::WordWrapOrAnywhere;

        bool m_expandTab = true;

        int m_tabStopWidth = 4;

        int m_zoomDelta = 0;
    };
}

#endif // TEXTEDITORCONFIG_H
