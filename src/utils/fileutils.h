#ifndef FILEUTILS_H
#define FILEUTILS_H

#include <QByteArray>
#include <QString>
#include <QImage>
#include <QPixmap>

class QTemporaryFile;

namespace vnotex
{
    class FileUtils
    {
    public:
        FileUtils() = delete;

        static QByteArray readFile(const QString &p_filePath);

        static QString readTextFile(const QString &p_filePath);

        static void writeFile(const QString &p_filePath, const QByteArray &p_data);

        static void writeFile(const QString &p_filePath, const QString &p_text);

        // Rename file or dir.
        static void renameFile(const QString &p_path, const QString &p_name);

        static bool childExistsCaseInsensitive(const QString &p_dirPath, const QString &p_name);

        static bool existsCaseInsensitive(const QString &p_path);

        static void copyFile(const QString &p_filePath,
                             const QString &p_destPath,
                             bool p_move = false);

        static void copyDir(const QString &p_dirPath,
                            const QString &p_destPath,
                            bool p_move = false);

        static void removeFile(const QString &p_filePath);

        // Return false if it is not deleted due to non-empty.
        static bool removeDirIfEmpty(const QString &p_dirPath);

        static void removeDir(const QString &p_dirPath);

        static QString renameIfExistsCaseInsensitive(const QString &p_path);

        static bool isPlatformNameCaseSensitive();

        static bool isText(const QString &p_filePath);

        static QImage imageFromFile(const QString &p_filePath);

        static QPixmap pixmapFromFile(const QString &p_filePath);

        static QString generateUniqueFileName(const QString &p_folderPath,
                                              const QString &p_hints,
                                              const QString &p_suffix);

        static QString generateRandomFileName(const QString &p_hints, const QString &p_suffix);

        static QString generateFileNameWithSequence(const QString &p_folderPath,
                                                    const QString &p_baseName,
                                                    const QString &p_suffix);

        static QTemporaryFile *createTemporaryFile(const QString &p_suffix);

        // Go through @p_dirPath recursively and delete all empty dirs.
        // @p_dirPath itself is not deleted.
        static void removeEmptyDir(const QString &p_dirPath);
    };
} // ns vnotex

#endif // FILEUTILS_H
