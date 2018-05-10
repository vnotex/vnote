#ifndef MARKDOWNITOPTION_H
#define MARKDOWNITOPTION_H

#include <QStringList>

struct MarkdownitOption
{
    MarkdownitOption()
        : MarkdownitOption(true,
                           false,
                           true,
                           false,
                           false,
                           false,
                           false)
    {
    }

    MarkdownitOption(bool p_html,
                     bool p_breaks,
                     bool p_linkify,
                     bool p_sub,
                     bool p_sup,
                     bool p_metadata,
                     bool p_emoji)
        : m_html(p_html),
          m_breaks(p_breaks),
          m_linkify(p_linkify),
          m_sub(p_sub),
          m_sup(p_sup),
          m_metadata(p_metadata),
          m_emoji(p_emoji)
    {
    }

    QStringList toConfig() const
    {
        QStringList conf;
        if (m_html) {
            conf << "html";
        }

        if (m_breaks) {
            conf << "break";
        }

        if (m_linkify) {
            conf << "linkify";
        }

        if (m_sub) {
            conf << "sub";
        }

        if (m_sup) {
            conf << "sup";
        }

        if (m_metadata) {
            conf << "metadata";
        }

        if (m_emoji) {
            conf << "emoji";
        }

        return conf;
    }

    static MarkdownitOption fromConfig(const QStringList &p_conf)
    {
        return MarkdownitOption(testOption(p_conf, "html"),
                                testOption(p_conf, "break"),
                                testOption(p_conf, "linkify"),
                                testOption(p_conf, "sub"),
                                testOption(p_conf, "sup"),
                                testOption(p_conf, "metadata"),
                                testOption(p_conf, "emoji"));
    }

    bool operator==(const MarkdownitOption &p_opt) const
    {
        return m_html == p_opt.m_html
               && m_breaks == p_opt.m_breaks
               && m_linkify == p_opt.m_linkify
               && m_sub == p_opt.m_sub
               && m_sup == p_opt.m_sup
               && m_metadata == p_opt.m_metadata
               && m_emoji == p_opt.m_emoji;
    }

    // Eanble HTML tags in source.
    bool m_html;

    // Convert '\n' in paragraphs into <br>.
    bool m_breaks;

    // Auto-convert URL-like text to links.
    bool m_linkify;

    // Enable subscript.
    bool m_sub;

    // Enable superscript.
    bool m_sup;

    // Enable metadata in YML format.
    bool m_metadata;

    // Enable emoji and emoticon.
    bool m_emoji;

private:
    static bool testOption(const QStringList &p_conf, const QString &p_opt)
    {
        return p_conf.contains(p_opt);
    }
};

#endif // MARKDOWNITOPTION_H
