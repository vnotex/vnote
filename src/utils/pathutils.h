#ifndef PATHUTILS_H
#define PATHUTILS_H

#include <QString>
#include <QDir>
#include <QUrl>

namespace vnotex
{
    class PathUtils
    {
    public:
        PathUtils() = delete;

        static QString cleanPath(const QString &p_path)
        {
            return QDir::cleanPath(p_path);
        }

        // See testParentDirPath().
        static QString parentDirPath(const QString &p_path);

        // Return @p_path if it is a dir. Otherwise, return its parent dir.
        static QString dirOrParentDirPath(const QString &p_path);

        // Whether @p_path is a dir.
        static bool isDir(const QString &p_path);

        // Whether @p_folderPath is an empty directory.
        static bool isEmptyDir(const QString &p_path);

        // Concatenate @p_dirPath and @p_name.
        static QString concatenateFilePath(const QString &p_dirPath, const QString &p_name);

        // Get dir name of @p_path directory.
        static QString dirName(const QString &p_path);

        // Get file name of @p_path file/directory.
        static QString fileName(const QString &p_path);

        static QString absolutePath(const QString &p_path)
        {
            return QDir(p_path).absolutePath();
        }

        // Normalize @p_path for comparision.
        static QString normalizePath(const QString &p_path);

        // Whether two paths point to the same file/directory.
        static bool areSamePaths(const QString &p_a, const QString &p_b);

        // Whether @p_dir contains @p_path.
        static bool pathContains(const QString &p_dir, const QString &p_path);

        static bool isLegalFileName(const QString &p_name);

        static bool isLegalPath(const QString &p_path);

        // Return relative path of @p_path to @p_dir.
        static QString relativePath(const QString &p_dir, const QString &p_path);

        static QUrl pathToUrl(const QString &p_path);

        static QString urlToPath(const QUrl &p_url);

        static QString encodeSpacesInPath(const QString &p_path);

        static void prependDotIfRelative(QString &p_path);

        static QString removeUrlParameters(const QString &p_url);

        static bool isImageUrl(const QString &p_url);

        // Regular expression string for file/folder name.
        // Forbidden chars: \/:*?"<>| and whitespaces except spaces.
        static const QString c_fileNameRegularExpression;
    };
} // ns vnotex

#endif // PATHUTILS_H
