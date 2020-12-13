#include "pathutils.h"

#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <QImageReader>
#include <QValidator>
#include <QRegularExpressionValidator>

using namespace vnotex;

const QString PathUtils::c_fileNameRegularExpression = QString("\\A(?:[^\\\\/:\\*\\?\"<>\\|\\s]| )+\\z");

QString PathUtils::parentDirPath(const QString &p_path)
{
    if (p_path.isEmpty()) {
        return p_path;
    }

    QFileInfo info(p_path);
    Q_ASSERT(info.isAbsolute());
    return cleanPath(info.absolutePath());
}

QString PathUtils::dirOrParentDirPath(const QString &p_path)
{
    if (p_path.isEmpty()) {
        return p_path;
    }

    QFileInfo info(p_path);
    if (info.isDir()) {
        return p_path;
    } else {
        return info.absolutePath();
    }
}

bool PathUtils::isEmptyDir(const QString &p_path)
{
    QFileInfo fi(p_path);
    if (!fi.exists()) {
        return true;
    }

    return fi.isDir() && QDir(p_path).isEmpty();
}

QString PathUtils::concatenateFilePath(const QString &p_dirPath, const QString &p_name)
{
    auto dirPath = cleanPath(p_dirPath);
    if (p_name.isEmpty()) {
        return dirPath;
    }

    if (dirPath.isEmpty()) {
        return p_name;
    }

    return dirPath + '/' + p_name;
}

QString PathUtils::dirName(const QString &p_path)
{
    Q_ASSERT(!QFileInfo::exists(p_path) || QFileInfo(p_path).isDir());
    return QDir(p_path).dirName();
}

QString PathUtils::fileName(const QString &p_path)
{
    QFileInfo fi(p_path);
    return fi.fileName();
}

QString PathUtils::normalizePath(const QString &p_path)
{
    auto absPath = QDir::cleanPath(QDir(p_path).absolutePath());
#if defined(Q_OS_WIN)
    return absPath.toLower();
#else
    return absPath;
#endif
}

bool PathUtils::areSamePaths(const QString &p_a, const QString &p_b)
{
    return normalizePath(p_a) == normalizePath(p_b);
}

bool PathUtils::pathContains(const QString &p_dir, const QString &p_path)
{
    auto rel = relativePath(p_dir, p_path);
    if (rel.startsWith(QStringLiteral("../")) || rel == QStringLiteral("..")) {
        return false;
    }

    if (QFileInfo(rel).isAbsolute()) {
        return false;
    }

    return true;
}

bool PathUtils::isLegalFileName(const QString &p_name)
{
    QRegularExpression nameRe(c_fileNameRegularExpression);
    auto match = nameRe.match(p_name);
    return match.hasMatch();
}

bool PathUtils::isLegalPath(const QString &p_path)
{
    // Ensure each part of the @p_path is a valid file name until we come to
    // an existing parent directory.
    if (p_path.isEmpty()) {
        return false;
    }

    if (QFileInfo::exists(p_path)) {
#if defined(Q_OS_WIN)
            // On Windows, "/" and ":" will also make exists() return true.
            if (p_path.startsWith('/') || p_path == ":") {
                return false;
            }
#endif
        return true;
    }

    bool ret = false;
    int pos = -1;
    QString basePath = parentDirPath(p_path);
    QString name = dirName(p_path);
    QScopedPointer<QValidator> validator(new QRegularExpressionValidator(QRegularExpression(c_fileNameRegularExpression)));
    while (!name.isEmpty()) {
        QValidator::State validFile = validator->validate(name, pos);
        if (validFile != QValidator::Acceptable) {
            break;
        }

        if (QFileInfo::exists(basePath)) {
            ret = true;
#if defined(Q_OS_WIN)
            // On Windows, "/" and ":" will also make exists() return true.
            if (basePath.startsWith('/') || basePath == ":") {
                ret = false;
            }
#endif
            break;
        }

        basePath = parentDirPath(basePath);
        name = dirName(basePath);
    }

    return ret;
}

QString PathUtils::relativePath(const QString &p_dir, const QString &p_path)
{
    QDir dir(p_dir);
    return cleanPath(dir.relativeFilePath(p_path));
}

QUrl PathUtils::pathToUrl(const QString &p_path)
{
    // Need to judge the path: Url, local file, resource file.
    QUrl url;
    QFileInfo pathInfo(p_path);
    if (pathInfo.exists()) {
        if (pathInfo.isNativePath()) {
            // Local file.
            url = QUrl::fromLocalFile(p_path);
        } else {
            // Resource file.
            url = QUrl(QStringLiteral("qrc") + p_path);
        }
    } else {
        // Url.
        url = QUrl(p_path);
    }

    return url;
}

QString PathUtils::urlToPath(const QUrl &p_url)
{
    if (p_url.isLocalFile()) {
        return p_url.toLocalFile();
    } else {
        return p_url.toString();
    }
}

QString PathUtils::encodeSpacesInPath(const QString &p_path)
{
    QString tmp(p_path);
    tmp.replace(QLatin1Char(' '), QStringLiteral("%20"));
    return tmp;
}

void PathUtils::prependDotIfRelative(QString &p_path)
{
    if (QDir::isRelativePath(p_path)
        && !p_path.startsWith(QStringLiteral("."))) {
        p_path.prepend(QStringLiteral("./"));
    }
}

QString PathUtils::removeUrlParameters(const QString &p_url)
{
    int idx = p_url.indexOf(QLatin1Char('?'));
    if (idx > -1) {
        return p_url.left(idx);
    }
    return p_url;
}

bool PathUtils::isImageUrl(const QString &p_url)
{
    QFileInfo info(removeUrlParameters(p_url));
    return QImageReader::supportedImageFormats().contains(info.suffix().toLower().toLatin1());
}

bool PathUtils::isDir(const QString &p_path)
{
    return QFileInfo(p_path).isDir();
}
