#ifndef VUTILS_H
#define VUTILS_H

#include <QString>
#include <QColor>
#include <QVector>
#include <QPair>
#include <QMessageBox>
#include <QUrl>
#include <QDir>
#include "vconfigmanager.h"
#include "vconstants.h"

class QKeyEvent;
class VFile;
class VOrphanFile;
class VNotebook;

#if !defined(V_ASSERT)
    #define V_ASSERT(cond) ((!(cond)) ? qt_assert(#cond, __FILE__, __LINE__) : qt_noop())
#endif

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
    ImageLinkType m_type;
};

class VUtils
{
public:
    static QString readFileFromDisk(const QString &filePath);
    static bool writeFileToDisk(const QString &filePath, const QString &text);
    // Transform FFFFFF string to QRgb
    static QRgb QRgbFromString(const QString &str);
    static QString generateImageFileName(const QString &path, const QString &title,
                                         const QString &format = "png");

    // Given the file name @p_fileName and directory path @p_dirPath, generate
    // a file name based on @p_fileName which does not exist in @p_dirPath.
    static QString generateCopiedFileName(const QString &p_dirPath, const QString &p_fileName);

    static QString generateCopiedDirName(const QString &p_parentDirPath, const QString &p_dirName);
    static void processStyle(QString &style, const QVector<QPair<QString, QString> > &varMap);

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
    static const QVector<QPair<QString, QString> > &getAvailableLanguages();
    static bool isValidLanguage(const QString &p_lang);
    static bool isImageURL(const QUrl &p_url);
    static bool isImageURLText(const QString &p_url);
    static qreal calculateScaleFactor();
    static bool realEqual(qreal p_a, qreal p_b);
    static QChar keyToChar(int p_key);
    static QString getLocale();

    static void sleepWait(int p_milliseconds);

    // Return the DocType according to suffix.
    static DocType docTypeFromName(const QString &p_name);

    // Generate HTML template.
    static QString generateHtmlTemplate(MarkdownConverterType p_conType, bool p_exportPdf);

    // Get an available file name in @p_directory with base @p_baseFileName.
    // If there already exists a file named @p_baseFileName, try to add sequence
    // suffix to the name, such as _001.
    static QString getFileNameWithSequence(const QString &p_directory,
                                           const QString &p_baseFileName);

    // Get an available random file name in @p_directory.
    static QString getRandomFileName(const QString &p_directory);

    // Try to check if @p_path is legal.
    static bool checkPathLegal(const QString &p_path);

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

    static QString displayDateTime(const QDateTime &p_dateTime);

    // Check if file @p_name exists in @p_dir.
    // @p_forceCaseInsensitive: if true, will check the name ignoring the case.
    static bool fileExists(const QDir &p_dir, const QString &p_name, bool p_forceCaseInsensitive = false);

    // Assign @p_str to @p_msg if it is not NULL.
    static void addErrMsg(QString *p_msg, const QString &p_str);

    // Regular expression for image link.
    // ![image title]( http://github.com/tamlok/vnote.jpg "alt \" text" )
    // Captured texts (need to be trimmed):
    // 1. Image Alt Text (Title);
    // 2. Image URL;
    // 3. Image Optional Title with double quotes;
    // 4. Unused;
    static const QString c_imageLinkRegExp;

    // Regular expression for file/directory name.
    // Forbidden char: \/:*?"<>|
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

private:
    VUtils() {}

    static void initAvailableLanguage();

    // Use HGMarkdownParser to parse @p_content to get all image link regions.
    static QVector<VElementRegion> fetchImageRegionsUsingParser(const QString &p_content);

    // Delete file/directory specified by @p_path by moving it to the recycle bin
    // folder @p_recycleBinFolderPath.
    static bool deleteFile(const QString &p_recycleBinFolderPath,
                           const QString &p_path);

    // <value, name>
    static QVector<QPair<QString, QString>> s_availableLanguages;
};

inline QString VUtils::directoryNameFromPath(const QString &p_path)
{
    return fileNameFromPath(p_path);
}

#endif // VUTILS_H
