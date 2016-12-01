#ifndef VUTILS_H
#define VUTILS_H

#include <QString>
#include <QColor>
#include <QVector>
#include <QPair>
#include <QMessageBox>
#include "vconfigmanager.h"
#include "vconstants.h"

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
};

inline QString VUtils::directoryNameFromPath(const QString &path)
{
    return fileNameFromPath(path);
}

#endif // VUTILS_H
