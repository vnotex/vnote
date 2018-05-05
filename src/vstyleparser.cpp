#include "vstyleparser.h"

#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QTextEdit>
#include <QColor>
#include <QBrush>
#include <QVector>
#include <QtDebug>
#include <QStringList>

#include "utils/vutils.h"

VStyleParser::VStyleParser()
{
    markdownStyles = NULL;
}

VStyleParser::~VStyleParser()
{
    if (markdownStyles) {
        pmh_free_style_collection(markdownStyles);
    }
}

QColor VStyleParser::QColorFromPmhAttr(pmh_attr_argb_color *attr) const
{
    return QColor(attr->red, attr->green, attr->blue, attr->alpha);
}

QBrush VStyleParser::QBrushFromPmhAttr(pmh_attr_argb_color *attr) const
{
    return QBrush(QColorFromPmhAttr(attr));
}

void markdownStyleErrorCB(char *errMsg, int lineNr, void *context)
{
    (void)context;
    qWarning() << "parser error:" << errMsg << lineNr;
}

QTextCharFormat VStyleParser::QTextCharFormatFromAttrs(pmh_style_attribute *attrs,
                                                       const QFont &baseFont) const
{
    QTextCharFormat format;
    while (attrs) {
        switch (attrs->type) {
        case pmh_attr_type_foreground_color:
            format.setForeground(QBrushFromPmhAttr(attrs->value->argb_color));
            break;

        case pmh_attr_type_background_color:
            format.setBackground(QBrushFromPmhAttr(attrs->value->argb_color));
            break;

        case pmh_attr_type_font_size_pt:
        {
            pmh_attr_font_size *fontSize = attrs->value->font_size;
            int ptSize = fontSize->size_pt;
            if (fontSize->is_relative) {
                int basePtSize = baseFont.pointSize();
                if (basePtSize == -1) {
                    // In pixel. Use default font configuration.
                    basePtSize = 11;
                }
                ptSize += basePtSize;
            }
            if (ptSize > 0) {
                format.setFontPointSize(ptSize);
            }
            break;
        }

        case pmh_attr_type_font_family:
        {
            QString familyList(attrs->value->font_family);
            QString finalFamily = VUtils::getAvailableFontFamily(familyList.split(','));
            if (!finalFamily.isEmpty()) {
                format.setFontFamily(finalFamily);
            }
            break;
        }

        case pmh_attr_type_font_style:
        {
            pmh_attr_font_styles *fontStyle = attrs->value->font_styles;
            if (fontStyle->italic) {
                format.setFontItalic(true);
            }

            if (fontStyle->bold) {
                format.setFontWeight(QFont::Bold);
            }

            if (fontStyle->underlined) {
                format.setFontUnderline(true);
            }

            if (fontStyle->strikeout) {
                format.setFontStrikeOut(true);
            }

            break;
        }

        default:
            qWarning() << "unimplemented format attr type:" << attrs->type;
            break;
        }
        attrs = attrs->next;
    }
    return format;
}

void VStyleParser::parseMarkdownStyle(const QString &styleStr)
{
    if (markdownStyles) {
        pmh_free_style_collection(markdownStyles);
    }
    markdownStyles = pmh_parse_styles(styleStr.toLocal8Bit().data(),
                                      &markdownStyleErrorCB, this);
}

QVector<HighlightingStyle> VStyleParser::fetchMarkdownStyles(const QFont &baseFont) const
{
    QVector<HighlightingStyle> styles;

    for (int i = 0; i < pmh_NUM_LANG_TYPES; ++i) {
        pmh_style_attribute *attr = markdownStyles->element_styles[i];
        if (!attr) {
            continue;
        }
        HighlightingStyle style;
        style.type = attr->lang_element_type;
        style.format = QTextCharFormatFromAttrs(attr, baseFont);
        styles.append(style);
    }
    return styles;
}

QHash<QString, QTextCharFormat> VStyleParser::fetchCodeBlockStyles(const QFont & p_baseFont) const
{
    QHash<QString, QTextCharFormat> styles;

    pmh_style_attribute *attrs = markdownStyles->element_styles[pmh_VERBATIM];

    // First set up the base format.
    QTextCharFormat baseFormat = QTextCharFormatFromAttrs(attrs, p_baseFont);

    while (attrs) {
        switch (attrs->type) {
        case pmh_attr_type_other:
        {
            QString attrName(attrs->name);
            QString attrValue(attrs->value->string);
            QTextCharFormat format;
            format.setFontFamily(baseFormat.fontFamily());

            QStringList items = attrValue.split(',', QString::SkipEmptyParts);
            for (auto const &item : items) {
                QString val = item.trimmed().toLower();
                if (val == "bold") {
                    format.setFontWeight(QFont::Bold);
                } else if (val == "italic") {
                    format.setFontItalic(true);
                } else if (val == "underlined") {
                    format.setFontUnderline(true);
                } else if (val == "strikeout") {
                    format.setFontStrikeOut(true);
                } else {
                    // Treat it as the color RGB value string without '#'.
                    QColor color("#" + val);
                    if (color.isValid()) {
                        format.setForeground(QBrush(color));
                    }
                }
            }

            if (format.isValid()) {
                styles[attrName] = format;
            }
            break;
        }

        default:
            // We just only handle custom attribute here.
            break;
        }
        attrs = attrs->next;
    }

    return styles;
}

void VStyleParser::fetchMarkdownEditorStyles(QPalette &palette, QFont &font,
                                             QMap<QString, QMap<QString, QString>> &styles) const
{
    QString ruleKey;

    int basePointSize = font.pointSize();
    if (basePointSize == -1) {
        // The size is specified in pixel. Use 11 pt by default.
        basePointSize = 11;
    }

    // editor
    pmh_style_attribute *editorStyles = markdownStyles->editor_styles;
    ruleKey = "editor";
    while (editorStyles) {
        switch (editorStyles->type) {
        case pmh_attr_type_foreground_color:
            palette.setColor(QPalette::Text,
                             QColorFromPmhAttr(editorStyles->value->argb_color));
            break;

        case pmh_attr_type_background_color:
            palette.setColor(QPalette::Base,
                             QColorFromPmhAttr(editorStyles->value->argb_color));
            break;

        case pmh_attr_type_font_family:
        {
            QString familyList(editorStyles->value->font_family);
            QString finalFamily = VUtils::getAvailableFontFamily(familyList.split(','));
            if (!finalFamily.isEmpty()) {
                font.setFamily(finalFamily);
            }
            break;
        }

        case pmh_attr_type_font_size_pt:
        {
            pmh_attr_font_size *fontSize = editorStyles->value->font_size;
            int ptSize = fontSize->size_pt;
            if (fontSize->is_relative) {
                ptSize += basePointSize;
            }

            if (ptSize > 0) {
                font.setPointSize(ptSize);
            }

            break;
        }

        // Get custom styles:
        //     trailing-space, line-number-background, line-number-foreground,
        //     color-column-background, color-column-foreground
        case pmh_attr_type_other:
        {
            QString attrName(editorStyles->name);
            QString value(editorStyles->value->string);
            styles[ruleKey][attrName] = value;
            break;
        }

        default:
                qWarning() << "unimplemented editor attr type:" << editorStyles->type;
        }
        editorStyles = editorStyles->next;
    }

    // editor-current-line
    pmh_style_attribute *curLineStyles = markdownStyles->editor_current_line_styles;
    ruleKey = "editor-current-line";
    while (curLineStyles) {
        switch (curLineStyles->type) {
        case pmh_attr_type_background_color:
        {
            QString attrName(curLineStyles->name);
            QString value = QColorFromPmhAttr(curLineStyles->value->argb_color).name();
            styles[ruleKey][attrName] = value;
            break;
        }

        // Get custom styles:
        //     vim-background.
        case pmh_attr_type_other:
        {
            QString attrName(curLineStyles->name);
            QString value(curLineStyles->value->string);
            styles[ruleKey][attrName] = value;
            break;
        }

        default:
            qWarning() << "unimplemented current line attr type:" << curLineStyles->type;
        }
        curLineStyles = curLineStyles->next;
    }

    // editor-selection
    pmh_style_attribute *selStyles = markdownStyles->editor_selection_styles;
    while (selStyles) {
        switch (selStyles->type) {
        case pmh_attr_type_foreground_color:
            palette.setColor(QPalette::HighlightedText,
                             QColorFromPmhAttr(selStyles->value->argb_color));
            break;

        case pmh_attr_type_background_color:
            palette.setColor(QPalette::Highlight,
                             QColorFromPmhAttr(selStyles->value->argb_color));
            break;

        default:
            qWarning() << "unimplemented selection attr type:" << selStyles->type;
        }
        selStyles = selStyles->next;
    }
}
