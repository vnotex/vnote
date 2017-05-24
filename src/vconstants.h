#ifndef VCONSTANTS_H
#define VCONSTANTS_H

// Html: rich text file;
// Markdown: Markdown text file;
enum class DocType { Html, Markdown };

enum class FileType { Normal, Orphan };

enum class ClipboardOpType { Invalid, CopyFile, CopyDir };
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
    static const QString c_imageFolder = "image_folder";
}
#endif
