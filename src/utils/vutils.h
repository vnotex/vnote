#ifndef VUTILS_H
#define VUTILS_H

#include <QString>
#include <QColor>
#include <QVector>
#include <QPair>
#include <QMessageBox>
#include <QUrl>
#include "vconfigmanager.h"
#include "vconstants.h"

class QKeyEvent;
class VFile;

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
    VUtils();

    static QString readFileFromDisk(const QString &filePath);
    static bool writeFileToDisk(const QString &filePath, const QString &text);
    // Transform FFFFFF string to QRgb
    static QRgb QRgbFromString(const QString &str);
    static QString generateImageFileName(const QString &path, const QString &title,
                                         const QString &format = "png");
    static QString generateCopiedFileName(const QString &p_dirPath, const QString &p_fileName);
    static QString generateCopiedDirName(const QString &p_parentDirPath, const QString &p_dirName);
    static void processStyle(QString &style, const QVector<QPair<QString, QString> > &varMap);

    // Return the last directory name of @p_path.
    static inline QString directoryNameFromPath(const QString& p_path);

    // Return the file name of @p_path.
    // /home/tamlok/abc, /home/tamlok/abc/ will both return abc.
    static QString fileNameFromPath(const QString &p_path);

    // Return the base path of @p_path.
    // /home/tamlok/abc, /home/tamlok/abc/ will both return /home/tamlok.
    static QString basePathFromPath(const QString &p_path);

    // Fetch all the image links (including those in code blocks) in markdown file p_file.
    // @p_type to filter the links returned.
    // Need to open p_file and will close it if it is originally closed.
    static QVector<ImageLink> fetchImagesFromMarkdownFile(VFile *p_file,
                                                          ImageLink::ImageLinkType p_type = ImageLink::All);

    // Create directories along the @p_path.
    // @p_path could be /home/tamlok/abc, /home/tamlok/abc/.
    static bool makePath(const QString &p_path);

    static ClipboardOpType opTypeInClipboard();
    static bool copyFile(const QString &p_srcFilePath, const QString &p_destFilePath, bool p_isCut);
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

private:
    // <value, name>
    static const QVector<QPair<QString, QString>> c_availableLanguages;
};

inline QString VUtils::directoryNameFromPath(const QString &p_path)
{
    return fileNameFromPath(p_path);
}

#endif // VUTILS_H
