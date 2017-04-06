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

#if !defined(V_ASSERT)
    #define V_ASSERT(cond) ((!(cond)) ? qt_assert(#cond, __FILE__, __LINE__) : qt_noop())
#endif

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
    static bool isMarkdown(const QString &fileName);
    static inline QString directoryNameFromPath(const QString& path);
    static QString fileNameFromPath(const QString &path);
    static QString basePathFromPath(const QString &path);
    static QVector<QString> imagesFromMarkdownFile(const QString &filePath);
    static void makeDirectory(const QString &path);
    static ClipboardOpType opTypeInClipboard();
    static bool copyFile(const QString &p_srcFilePath, const QString &p_destFilePath, bool p_isCut);
    static bool copyDirectory(const QString &p_srcDirPath, const QString &p_destDirPath, bool p_isCut);
    static int showMessage(QMessageBox::Icon p_icon, const QString &p_title, const QString &p_text,
                           const QString &p_infoText, QMessageBox::StandardButtons p_buttons,
                           QMessageBox::StandardButton p_defaultBtn, QWidget *p_parent);
    static const QVector<QPair<QString, QString> > &getAvailableLanguages();
    static bool isValidLanguage(const QString &p_lang);
    static bool isImageURL(const QUrl &p_url);
    static bool isImageURLText(const QString &p_url);
    static qreal calculateScaleFactor();
    static bool realEqual(qreal p_a, qreal p_b);
    static QChar keyToChar(int p_key);
    static QString getLocale();

private:
    // <value, name>
    static const QVector<QPair<QString, QString>> c_availableLanguages;
};

inline QString VUtils::directoryNameFromPath(const QString &path)
{
    return fileNameFromPath(path);
}

#endif // VUTILS_H
