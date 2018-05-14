#ifndef VWEBUTILS_H
#define VWEBUTILS_H

#include <QUrl>
#include <QString>
#include <QVector>
#include <QStringList>
#include <QRegExp>


class VWebUtils
{
public:
    VWebUtils();

    void init();

    QStringList getCopyTargetsName() const;

    // Alter @p_html using @p_target.
    // Returns true if @p_html is modified.
    bool alterHtmlAsTarget(const QUrl &p_baseUrl, QString &p_html, const QString &p_target) const;

    // Download or copy @p_url to @p_folder.
    // Return the target file path on success or empty string on failure.
    QString copyResource(const QUrl &p_url, const QString &p_folder) const;

    // Return a dataURI of @p_url if it is an image.
    QString dataURI(const QUrl &p_url, bool p_keepTitle = true) const;

private:
    struct CopyTargetAction
    {
        QChar m_act;

        QStringList m_args;
    };

    struct CopyTarget
    {
        QString m_name;

        QVector<CopyTargetAction> m_actions;
    };

    struct HtmlTag
    {
        HtmlTag()
            : m_start(-1), m_end(-1)
        {

        }

        bool isNull()
        {
            return m_name.isEmpty();
        }

        QString m_name;
        QString m_style;

        int m_start;
        int m_end;
    };

    void initCopyTargets(const QStringList &p_str);

    // Return the index in m_copyTargets of @p_target.
    int targetIndex(const QString &p_target) const;

    bool alterHtmlByTargetAction(const QUrl &p_baseUrl, QString &p_html, const CopyTargetAction &p_action);

    // Remove background color style in @p_html of all tags except @p_skipTags.
    bool removeBackgroundColor(QString &p_html, const QStringList &p_skipTags);

    // Translate color styles in @p_html using mappings from VPalette.
    bool translateColors(QString &p_html, const QStringList &p_skipTags);

    // Fix <img src> in @p_html.
    bool fixImageSrc(const QUrl &p_baseUrl, QString &p_html);

    // Remove margin/padding/margin-left/right/padding-left/right.
    bool removeMarginPadding(QString &p_html, const QStringList &p_skipTags);

    bool removeStyles(QString &p_html, const QStringList &p_skipTags, const QStringList &p_styles);

    // Remove styles specified in [web]/styles_to_remove_when_copied.
    bool removeStylesToRemoveWhenCopied(QString &p_html, const QStringList &p_skipTags);

    // Remove all styles.
    bool removeAllStyles(QString &p_html, const QStringList &p_skipTags);

    // Transform <mark> to <span>.
    bool transformMarkToSpan(QString &p_html);

    // Replace the background color of <pre> with that of its child <code>.
    bool replacePreBackgroundColorWithCode(QString &p_html);

    VWebUtils::HtmlTag readNextTag(const QString &p_html, int p_pos);

    // Replace \n with <br> in <pre>.
    bool replaceNewLineWithBR(QString &p_html);

    // Replace local absolute/relative <img> tag with a warning label.
    bool replaceLocalImgWithWarningLabel(QString &p_html);

    // Add <span> inside <code> not in <pre>.
    bool addSpanInsideCode(QString &p_html);

    // Replace &quot; in font-family with '.
    bool replaceQuoteInFontFamily(QString &p_html);

    // Replace headings with span.
    bool replaceHeadingWithSpan(QString &p_html);

    // Fix tags as XHTML like <img> and <br>.
    bool fixXHtmlTags(QString &p_html);

    QVector<CopyTarget> m_copyTargets;

    // Custom styles to remove when copied.
    QStringList m_stylesToRemoveWhenCopied;

    // Style of <span> which is transformed from <mark>.
    QString m_styleOfSpanForMark;

    // Html start tag.
    // Captured texts:
    // 1. The tag name like 'code';
    // 2. Text after tag name and before the end '>';
    QRegExp m_tagReg;

    // Html start tag with "style" defined.
    // Captured texts:
    // 1. The tag name like 'code';
    // 2. Text before 'style=""';
    // 3. Text inside 'style=""';
    // 4. Text after 'style=""' and before '>';
    QRegExp m_styleTagReg;

    // <Img> tag.
    // Captured texts:
    // 1. Src string without ";
    QRegExp m_imgTagReg;
};
#endif // VWEBUTILS_H
