#ifndef VCONSTANTS_H
#define VCONSTANTS_H

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
}

enum class OpenFileMode {Read = 0, Edit};

static const qreal c_webZoomFactorMax = 5;
static const qreal c_webZoomFactorMin = 0.25;

static const int c_tabSequenceBase = 1;

// HTML and JS.
static const QString c_htmlJSHolder = "JS_PLACE_HOLDER";
static const QString c_htmlExtraHolder = "<!-- EXTRA_PLACE_HOLDER -->";

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

static const QString c_emptyHeaderName = "[EMPTY]";

enum class TextDecoration { None,
                            Bold,
                            Italic,
                            Underline,
                            Strikethrough,
                            InlineCode };

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
    Comment
};

enum class LineNumberType
{
    None = 0,
    Absolute,
    Relative,
    CodeBlock
};

#endif
