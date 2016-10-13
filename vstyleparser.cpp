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
    qDebug() << "parser error:" << errMsg << lineNr;
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
            QString finalFamily = filterAvailableFontFamily(familyList);
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
            break;
        }

        default:
            qWarning() << "warning: unimplemented format attr type:" << attrs->type;
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
    QVector<HighlightingStyle> styles(pmh_NUM_LANG_TYPES);

    for (int i = 0; i < pmh_NUM_LANG_TYPES; ++i) {
        pmh_style_attribute *attr = markdownStyles->element_styles[i];
        if (!attr) {
            continue;
        }
        styles[i].type = attr->lang_element_type;
        styles[i].format = QTextCharFormatFromAttrs(attr, baseFont);
    }
    return styles;
}

void VStyleParser::fetchMarkdownEditorStyles(QPalette &palette, QFont &font) const
{
    // editor
    pmh_style_attribute *editorStyles = markdownStyles->editor_styles;
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
            QString finalFamily = filterAvailableFontFamily(familyList);
            if (!finalFamily.isEmpty()) {
                font.setFamily(finalFamily);
            }
            break;
        }

        default:
                qWarning() << "warning: unimplemented editor attr type:" << editorStyles->type;
        }
        editorStyles = editorStyles->next;
    }

    // editor-current-line
    pmh_style_attribute *curLineStyles = markdownStyles->editor_current_line_styles;
    if (curLineStyles) {
        qDebug() << "editor-current-line style is not supported";
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
            qWarning() << "warning: unimplemented selection attr type:" << selStyles->type;
        }
        selStyles = selStyles->next;
    }
}

// @familyList is a comma separated string
QString VStyleParser::filterAvailableFontFamily(const QString &familyList) const
{
    QStringList families = familyList.split(',', QString::SkipEmptyParts);
    QStringList availFamilies = QFontDatabase().families();

    qDebug() << "family:" << familyList;
    for (int i = 0; i < families.size(); ++i) {
        QString family = families[i].trimmed().toLower();
        for (int j = 0; j < availFamilies.size(); ++j) {
            QString availFamily = availFamilies[j];
            availFamily.remove(QRegExp("\\[.*\\]"));
            if (family == availFamily.trimmed().toLower()) {
                qDebug() << "matched family:" << availFamilies[j];
                return availFamilies[j];
            }
        }
    }

    return QString();
}
