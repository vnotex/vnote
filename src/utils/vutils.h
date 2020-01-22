#ifndef VUTILS_H
#define VUTILS_H

#include <QString>
#include <QColor>
#include <QVector>
#include <QPair>
#include <QMessageBox>
#include <QUrl>
#include <QDir>
#include <functional>

#include "vconfigmanager.h"
#include "vconstants.h"

class QKeyEvent;
class VFile;
class VOrphanFile;
class VNotebook;
class QWidget;
class QComboBox;
class QWebEngineView;
class QAction;
class QTreeWidgetItem;
class QFormLayout;
class QTemporaryFile;

#if !defined(V_ASSERT)
    #define V_ASSERT(cond) ((!(cond)) ? qt_assert(#cond, __FILE__, __LINE__) : qt_noop())
#endif

// Thanks to CGAL/cgal.
#ifndef __has_attribute
    #define __has_attribute(x) 0  // Compatibility with non-clang compilers.
#endif

#ifndef __has_cpp_attribute
  #define __has_cpp_attribute(x) 0  // Compatibility with non-supporting compilers.
#endif

// The fallthrough attribute.
// See for clang:
//   http://clang.llvm.org/docs/AttributeReference.html#statement-attributes
// See for gcc:
//   https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
#if __has_cpp_attribute(fallthrough)
#  define V_FALLTHROUGH [[fallthrough]]
#elif __has_cpp_attribute(gnu::fallthrough)
#  define V_FALLTHROUGH [[gnu::fallthrough]]
#elif __has_cpp_attribute(clang::fallthrough)
#  define V_FALLTHROUGH [[clang::fallthrough]]
#elif __has_attribute(fallthrough) && ! __clang__
#  define V_FALLTHROUGH __attribute__ ((fallthrough))
#else
#  define V_FALLTHROUGH while(false){}
#endif

#define DETIME() qDebug() << "ELAPSED_TIME" << __func__ << __LINE__ << VUtils::elapsedTime()

#define RESET_TIME() VUtils::elapsedTime(true)

enum class MessageBoxType
{
    Normal = 0,
    Danger = 1
};

struct ImageLink
{
    enum ImageLinkType
    {
        LocalRelativeInternal = 0x1,
        LocalRelativeExternal = 0x2,
        LocalAbsolute = 0x4,
        Resource = 0x8,
        Remote = 0x10,
        All = 0xffff
    };

    QString m_path;

    // The url text in the link.
    QString m_url;

    ImageLinkType m_type;
};

class VUtils
{
public:
    static QString readFileFromDisk(const QString &filePath);

    static bool writeFileToDisk(const QString &p_filePath, const QString &p_text);

    static bool writeFileToDisk(const QString &p_filePath, const QByteArray &p_data);

    static bool writeJsonToDisk(const QString &p_filePath, const QJsonObject &p_json);

    static QJsonObject readJsonFromDisk(const QString &p_filePath);

    static QString generateImageFileName(const QString &path,
                                         const QString &title,
                                         const QString &format = "png");

    // Given the file name @p_fileName and directory path @p_dirPath, generate
    // a file name based on @p_fileName which does not exist in @p_dirPath.
    // @p_completeBaseName: use complete base name or complete suffix. For example,
    // "abc.tar.gz", if @p_completeBaseName is true, the base name is "abc.tar",
    // otherwise, it is "abc".
    static QString generateCopiedFileName(const QString &p_dirPath,
                                          const QString &p_fileName,
                                          bool p_completeBaseName = true);

    // Given the directory name @p_dirName and directory path @p_parentDirPath,
    // generate a directory name based on @p_dirName which does not exist in
    // @p_parentDirPath.
    static QString generateCopiedDirName(const QString &p_parentDirPath,
                                         const QString &p_dirName);

    // Return the last directory name of @p_path.
    static QString directoryNameFromPath(const QString& p_path);

    // Return the file name of @p_path.
    // /home/tamlok/abc, /home/tamlok/abc/ will both return abc.
    static QString fileNameFromPath(const QString &p_path);

    // Return the base path of @p_path.
    // /home/tamlok/abc, /home/tamlok/abc/ will both return /home/tamlok.
    static QString basePathFromPath(const QString &p_path);

    // Fetch all the image links in markdown file p_file.
    // @p_type to filter the links returned.
    // Need to open p_file and will close it if it is originally closed.
    static QVector<ImageLink> fetchImagesFromMarkdownFile(VFile *p_file,
                                                          ImageLink::ImageLinkType p_type = ImageLink::All);

    // Use PegParser to parse @p_content to get all image link regions.
    static QVector<VElementRegion> fetchImageRegionsUsingParser(const QString &p_content);

    // Return the absolute path of @p_url according to @p_basePath.
    static QString linkUrlToPath(const QString &p_basePath, const QString &p_url);

    // Create directories along the @p_path.
    // @p_path could be /home/tamlok/abc, /home/tamlok/abc/.
    static bool makePath(const QString &p_path);

    // Return QJsonObject if there is valid Json string in clipboard.
    // Return empty object if there is no valid Json string.
    static QJsonObject clipboardToJson();

    // Get the operation type in system's clipboard.
    static ClipboardOpType operationInClipboard();

    static ClipboardOpType opTypeInClipboard() { return ClipboardOpType::Invalid; }

    // Copy file @p_srcFilePath to @p_destFilePath.
    // Will make necessary parent directory along the destination path.
    static bool copyFile(const QString &p_srcFilePath, const QString &p_destFilePath, bool p_isCut);

    // Copy @p_srcDirPath to be @p_destDirPath.
    // @p_destDirPath should not exist.
    // Will make necessary parent directory along the destination path.
    static bool copyDirectory(const QString &p_srcDirPath, const QString &p_destDirPath, bool p_isCut);

    static int showMessage(QMessageBox::Icon p_icon, const QString &p_title, const QString &p_text,
                           const QString &p_infoText, QMessageBox::StandardButtons p_buttons,
                           QMessageBox::StandardButton p_defaultBtn, QWidget *p_parent,
                           MessageBoxType p_type = MessageBoxType::Normal);

    static void promptForReopen(QWidget *p_parent);

    static const QVector<QPair<QString, QString> > &getAvailableLanguages();
    static bool isValidLanguage(const QString &p_lang);
    static bool isImageURL(const QUrl &p_url);
    static bool isImageURLText(const QString &p_url);
    static qreal calculateScaleFactor();
    static bool realEqual(qreal p_a, qreal p_b);

    static QChar keyToChar(int p_key, bool p_smallCase = true);

    static QString getLocale();

    static void sleepWait(int p_milliseconds);

    // Return the DocType according to suffix.
    static DocType docTypeFromName(const QString &p_name);

    // Generate HTML template.
    static QString generateHtmlTemplate(MarkdownConverterType p_conType);

    // @p_renderBg is the background name.
    // @p_wkhtmltopdf: whether this template is used for wkhtmltopdf.
    static QString generateHtmlTemplate(MarkdownConverterType p_conType,
                                        const QString &p_renderBg,
                                        const QString &p_renderStyle,
                                        const QString &p_renderCodeBlockStyle,
                                        bool p_isPDF,
                                        bool p_wkhtmltopdf = false,
                                        bool p_addToc = false);

    // @p_renderBg is the background name.
    static QString generateExportHtmlTemplate(const QString &p_renderBg,
                                              bool p_includeMathJax,
                                              bool p_outlinePanel);

    static QString generateSimpleHtmlTemplate(const QString &p_body);

    // Generate template for MathJax preview.
    static QString generateMathJaxPreviewTemplate();

    // Get an available file name in @p_directory with base @p_baseFileName.
    // If there already exists a file named @p_baseFileName, try to add sequence
    // suffix to the name, such as _001.
    // @p_completeBaseName: use complete base name or complete suffix. For example,
    // "abc.tar.gz", if @p_completeBaseName is true, the base name is "abc.tar",
    // otherwise, it is "abc".
    static QString getFileNameWithSequence(const QString &p_directory,
                                           const QString &p_baseFileName,
                                           bool p_completeBaseName = true);

    // Get an available directory name in @p_directory with base @p_baseDirName.
    // If there already exists a file named @p_baseFileName, try to add sequence
    // suffix to the name, such as _001.
    static QString getDirNameWithSequence(const QString &p_directory,
                                          const QString &p_baseDirName);

    // Get an available random file name in @p_directory.
    static QString getRandomFileName(const QString &p_directory);

    // Try to check if @p_path is legal.
    static bool checkPathLegal(const QString &p_path);

    // Check if file/folder name is legal.
    static bool checkFileNameLegal(const QString &p_name);

    // Returns true if @p_patha and @p_pathb points to the same file/directory.
    static bool equalPath(const QString &p_patha, const QString &p_pathb);

    // Try to split @p_path into multiple parts based on @p_base.
    // Returns false if @p_path is not under @p_base directory.
    // @p_parts will be empty if @p_path is right @p_base.
    // Example: "/home/tamlok/study", "/home/tamlok/study/a/b/c/vnote.md"
    // returns true and @p_parts is {a, b, c, vnote.md}.
    static bool splitPathInBasePath(const QString &p_base,
                                    const QString &p_path,
                                    QStringList &p_parts);

    // Decode URL by simply replacing meta-characters.
    static void decodeUrl(QString &p_url);

    // Returns the shortcut text.
    static QString getShortcutText(const QString &p_keySeq);

    // Delete directory recursively specified by @p_path.
    // Will just move the directory to the recycle bin of @p_notebook if
    // @p_skipRecycleBin is false.
    static bool deleteDirectory(const VNotebook *p_notebook,
                                const QString &p_path,
                                bool p_skipRecycleBin = false);

    static bool deleteDirectory(const QString &p_path);

    // Empty all files in directory recursively specified by @p_path.
    // Will just move files to the recycle bin of @p_notebook if
    // @p_skipRecycleBin is false.
    static bool emptyDirectory(const VNotebook *p_notebook,
                               const QString &p_path,
                               bool p_skipRecycleBin = false);

    // Delete file specified by @p_path.
    // Will just move the file to the recycle bin of @p_notebook if
    // @p_skipRecycleBin is false.
    static bool deleteFile(const VNotebook *p_notebook,
                           const QString &p_path,
                           bool p_skipRecycleBin = false);

    // Delete file specified by @p_path.
    // Will just move the file to the recycle bin of VOrphanFile if
    // @p_skipRecycleBin is false.
    static bool deleteFile(const VOrphanFile *p_file,
                           const QString &p_path,
                           bool p_skipRecycleBin = false);

    // Delete file specified by @p_path.
    static bool deleteFile(const QString &p_path);

    // @p_uniformNum: if true, we use YYYY/MM/DD HH:mm:ss form, which is good for
    // sorting.
    static QString displayDateTime(const QDateTime &p_dateTime, bool p_uniformNum = false);

    // Check if file @p_name exists in @p_dir.
    // @p_forceCaseInsensitive: if true, will check the name ignoring the case.
    static bool fileExists(const QDir &p_dir, const QString &p_name, bool p_forceCaseInsensitive = false);

    // Assign @p_str to @p_msg if it is not NULL.
    static void addErrMsg(QString *p_msg, const QString &p_str);

    // Check each file of @p_files and return valid ones for VNote to open.
    static QStringList filterFilePathsToOpen(const QStringList &p_files);

    // Return the normalized file path of @p_file if it is valid to open.
    // Return empty if it is not valid.
    static QString validFilePathToOpen(const QString &p_file);

    // See if @p_modifiers is Control which is different on macOs and Windows.
    static bool isControlModifierForVim(int p_modifiers);

    // If @p_file does not exists, create an empty file.
    static void touchFile(const QString &p_file);

    // Ctrl, Meta, Shift, Alt.
    static bool isMetaKey(int p_key);

    // Create and return a QComboBox.
    static QComboBox *getComboBox(QWidget *p_parent = nullptr);

    static QWebEngineView *getWebEngineView(const QColor &p_background, QWidget *p_parent = nullptr);

    static void setDynamicProperty(QWidget *p_widget, const char *p_prop, bool p_val = true);

    // Return a doc file path.
    static QString getDocFile(const QString &p_name);

    static QString getCaptainShortcutSequenceText(const QString &p_operation);

    static QString getAvailableFontFamily(const QStringList &p_families);

    static bool fixTextWithShortcut(QAction *p_act, const QString &p_shortcut);

    static bool fixTextWithCaptainShortcut(QAction *p_act, const QString &p_shortcut);

    // From QProcess code.
    static QStringList parseCombinedArgString(const QString &p_program);

    // Read QImage from local file @p_filePath.
    // Directly calling QImage(p_filePath) will judge the image format from the suffix,
    // resulting a null image in wrong suffix case.
    static QImage imageFromFile(const QString &p_filePath);

    static QPixmap pixmapFromFile(const QString &p_filePath);

    // Return QFormLayout.
    static QFormLayout *getFormLayout();

    static bool inSameDrive(const QString &p_a, const QString &p_b);

    static QString promptForFileName(const QString &p_title,
                                     const QString &p_label,
                                     const QString &p_default,
                                     const QString &p_dir,
                                     QWidget *p_parent = nullptr);

    static QString promptForFileName(const QString &p_title,
                                     const QString &p_label,
                                     const QString &p_default,
                                     std::function<bool(const QString &p_name)> p_checkExistsFunc,
                                     QWidget *p_parent = nullptr);

    // Whether @p_html has only <img> content.
    static bool onlyHasImgInHtml(const QString &p_html);

    static int elapsedTime(bool p_reset = false);

    // Render SVG to Pixmap.
    // @p_factor: < 0 indicates no scaling.
    static QPixmap svgToPixmap(const QByteArray &p_content,
                               const QString &p_background,
                               qreal p_factor);

    // Fetch the image link's URL if there is only one link.
    static QString fetchImageLinkUrlToPreview(const QString &p_text, int &p_width, int &p_height);

    static QString fetchImageLinkUrl(const QString &p_link);

    static QString fetchLinkUrl(const QString &p_link);

    static QUrl pathToUrl(const QString &p_path);

    // Return the name of the parent directory of @p_path.
    // @p_path: file path of file or dir.
    static QString parentDirName(const QString &p_path);

    // Remove query in the url (?xxx).
    static QString purifyUrl(const QString &p_url);

    static QTemporaryFile *createTemporaryFile(QString p_suffix);

    static QString purifyImageTitle(QString p_title);

    static QString escapeHtml(QString p_text);

    static QString encodeSpacesInPath(const QString &p_path);

    static void prependDotIfRelative(QString &p_path);

    // Regular expression for image link.
    // ![image title]( http://github.com/tamlok/vnote.jpg "alt text" =200x100)
    // Captured texts (need to be trimmed):
    // 1. Image Alt Text (Title);
    // 2. Image URL;
    // 3. Image Optional Title with double quotes or quotes;
    // 4. Unused;
    // 5. Unused;
    // 6. Width and height text;
    // 7. Width;
    // 8. Height;
    static const QString c_imageLinkRegExp;

    // Regular expression for image title.
    static const QString c_imageTitleRegExp;

    // Regular expression for link.
    // [link text]( http://github.com/tamlok "alt text")
    // Captured texts (need to be trimmed):
    // 1. Link Alt Text (Title);
    // 2. Link URL;
    // 3. Link Optional Title with double quotes or quotes;
    // 4. Unused;
    // 5. Unused;
    static const QString c_linkRegExp;

    // Regular expression for file/directory name.
    // Forbidden char: \/:*?"<>| and whitespaces except space.
    static const QString c_fileNameRegExp;

    // Regular expression for fenced code block.
    static const QString c_fencedCodeBlockStartRegExp;
    static const QString c_fencedCodeBlockEndRegExp;

    // Regular expression for preview image block.
    static const QString c_previewImageBlockRegExp;

    // Regular expression for header block.
    // Captured texts:
    // 1. Header marker (##);
    // 2. Header Title (need to be trimmed);
    // 3. Header Sequence (1.1., 1.2., optional);
    // 4. Unused;
    static const QString c_headerRegExp;

    // Regular expression for header block.
    // Captured texts:
    // 1. prefix till the real header title content;
    static const QString c_headerPrefixRegExp;

    static const QString c_listRegExp;

    static const QString c_blockQuoteRegExp;

private:
    VUtils() {}

    static void initAvailableLanguage();

    // Delete file/directory specified by @p_path by moving it to the recycle bin
    // folder @p_recycleBinFolderPath.
    static bool deleteFile(const QString &p_recycleBinFolderPath,
                           const QString &p_path);

    static QString generateHtmlTemplate(const QString &p_template,
                                        MarkdownConverterType p_conType,
                                        bool p_isPDF = false,
                                        bool p_wkhtmltopdf = false,
                                        bool p_addToc = false);

    // <value, name>
    static QVector<QPair<QString, QString>> s_availableLanguages;
};

inline QString VUtils::directoryNameFromPath(const QString &p_path)
{
    return fileNameFromPath(p_path);
}

#endif // VUTILS_H
