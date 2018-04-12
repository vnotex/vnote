#ifndef VCONSTANTS_H
#define VCONSTANTS_H

#include <QString>
#include <QStringList>

typedef unsigned long long TimeStamp;

// Html: rich text file;
// Markdown: Markdown text file;
// List: Infinite list file like WorkFlowy;
// Container: a composite file containing multiple files;
enum class DocType { Html = 0, Markdown, List, Container, Unknown };

// Note: note file managed by VNote;
// Orphan: external file;
enum class FileType { Note, Orphan };

enum class ClipboardOpType { CopyFile, CopyDir, Invalid };

namespace ClipboardConfig
{
    static const QString c_type = "type";
    static const QString c_magic = "magic";
    static const QString c_isCut = "is_cut";
    static const QString c_files = "files";
    static const QString c_dirs = "dirs";
}

enum class OpenFileMode {Read = 0, Edit, Invalid };

static const qreal c_webZoomFactorMax = 5;
static const qreal c_webZoomFactorMin = 0.25;

static const int c_tabSequenceBase = 1;

// HTML and JS.
namespace HtmlHolder
{
    static const QString c_JSHolder = "JS_PLACE_HOLDER";
    static const QString c_cssHolder = "CSS_PLACE_HOLDER";
    static const QString c_codeBlockCssHolder = "HIGHLIGHTJS_CSS_PLACE_HOLDER";
    static const QString c_globalStyleHolder = "/* STYLE_GLOBAL_PLACE_HOLDER */";
    static const QString c_extraHolder = "<!-- EXTRA_PLACE_HOLDER -->";
    static const QString c_bodyHolder = "<!-- BODY_PLACE_HOLDER -->";
    static const QString c_headHolder = "<!-- HEAD_PLACE_HOLDER -->";
    static const QString c_styleHolder = "/* STYLE_PLACE_HOLDER */";
}

// Directory Config file items.
namespace DirConfig
{
    static const QString c_version = "version";
    static const QString c_subDirectories = "sub_directories";
    static const QString c_files = "files";
    static const QString c_attachments = "attachments";
    static const QString c_imageFolder = "image_folder";
    static const QString c_attachmentFolder = "attachment_folder";
    static const QString c_recycleBinFolder = "recycle_bin_folder";
    static const QString c_name = "name";
    static const QString c_createdTime = "created_time";
    static const QString c_modifiedTime = "modified_time";
}

// Snippet Cofnig file items.
namespace SnippetConfig
{
    static const QString c_version = "version";
    static const QString c_snippets = "snippets";
    static const QString c_name = "name";
    static const QString c_type = "type";
    static const QString c_cursorMark = "cursor_mark";
    static const QString c_selectionMark = "selection_mark";
    static const QString c_shortcut = "shortcut";
    static const QString c_autoIndent = "auto_indent";
}

static const QString c_emptyHeaderName = "[EMPTY]";

enum class TextDecoration
{
    None,
    Bold,
    Italic,
    Underline,
    Strikethrough,
    InlineCode,
    CodeBlock,
    Heading
};

enum FindOption
{
    CaseSensitive = 0x1U,
    WholeWordOnly = 0x2U,
    RegularExpression = 0x4U,
    IncrementalSearch = 0x8U
};

enum class ImageProperty {/* ID of the image preview (long long). Unique for each source. */
                          ImageID = 1,
                          /* Source type of the preview, such as image, codeblock. */
                          ImageSource,
                          /* Type of the preview, block or inline. */
                          ImageType };

enum class PreviewImageType { Block, Inline, Invalid };

enum class PreviewImageSource { Image, CodeBlock, Invalid };

enum HighlightBlockState
{
    Normal = 0,

    // A fenced code block.
    CodeBlockStart,
    CodeBlock,
    CodeBlockEnd,

    // This block is inside a HTML comment region.
    Comment,

    // Verbatim code block.
    Verbatim,

    // Mathjax. It means the pending state of the block.
    MathjaxBlock,
    MathjaxInline
};

// Pages to open on start up.
enum class StartupPageType
{
    None = 0,
    ContinueLeftOff = 1,
    SpecificPages = 2,
    Invalid
};

// Cursor block mode.
enum class CursorBlock
{
    None = 0,
    // Display a cursor block on the character on the right side of the cursor.
    RightSide,
    // Display a cursor block on the character on the left side of the cursor.
    LeftSide
};

enum class UpdateAction
{
    // The info of a file/directory has been changed.
    InfoChanged = 0,

    // The file/directory has been moved.
    Moved
};

enum MarkdownConverterType
{
    Hoedown = 0,
    Marked,
    MarkdownIt,
    Showdown
};

enum PlantUMLMode
{
    DisablePlantUML = 0,
    OnlinePlantUML = 1,
    LocalPlantUML = 2
};


struct MarkdownitOption
{
    MarkdownitOption()
        : MarkdownitOption(true,
                           false,
                           true,
                           false,
                           false)
    {
    }

    MarkdownitOption(bool p_html,
                     bool p_breaks,
                     bool p_linkify,
                     bool p_sub,
                     bool p_sup)
        : m_html(p_html),
          m_breaks(p_breaks),
          m_linkify(p_linkify),
          m_sub(p_sub),
          m_sup(p_sup)
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

        return conf;
    }

    static MarkdownitOption fromConfig(const QStringList &p_conf)
    {
        return MarkdownitOption(testOption(p_conf, "html"),
                                testOption(p_conf, "break"),
                                testOption(p_conf, "linkify"),
                                testOption(p_conf, "sub"),
                                testOption(p_conf, "sup"));
    }

    bool operator==(const MarkdownitOption &p_opt) const
    {
        return m_html == p_opt.m_html
               && m_breaks == p_opt.m_breaks
               && m_linkify == p_opt.m_linkify
               && m_sub == p_opt.m_sub
               && m_sup == p_opt.m_sup;
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

private:
    static bool testOption(const QStringList &p_conf, const QString &p_opt)
    {
        return p_conf.contains(p_opt);
    }
};
#endif
