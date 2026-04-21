#ifndef TEMP_DIR_FIXTURE_H
#define TEMP_DIR_FIXTURE_H

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

namespace tests {

// RAII wrapper for temporary directory used in tests
class TempDirFixture {
public:
  TempDirFixture() : m_dir() { m_dir.setAutoRemove(true); }

  // Returns the path to the temporary directory
  QString path() const { return m_dir.path(); }

  // Returns path to a file/subdir within the temp directory
  QString filePath(const QString &p_name) const { return m_dir.path() + "/" + p_name; }

  // Creates a file with the given content, returns full path
  QString createFile(const QString &p_name, const QByteArray &p_content = QByteArray()) {
    QString fullPath = filePath(p_name);
    QFile file(fullPath);
    if (file.open(QIODevice::WriteOnly)) {
      file.write(p_content);
      file.close();
    }
    return fullPath;
  }

  // Creates a text file with the given content, returns full path
  QString createTextFile(const QString &p_name, const QString &p_content) {
    return createFile(p_name, p_content.toUtf8());
  }

  // Creates a subdirectory, returns full path
  QString createDir(const QString &p_name) {
    QString fullPath = filePath(p_name);
    QDir(m_dir.path()).mkpath(p_name);
    return fullPath;
  }

  // Check if the fixture is valid
  bool isValid() const { return m_dir.isValid(); }

  // Copies a source directory tree into the temp directory under p_destName.
  // If p_destName is empty, uses the source directory's basename.
  // Returns the full path to the copy inside the temp directory.
  QString copyFrom(const QString &p_sourceDir, const QString &p_destName = QString()) {
    QString destName = p_destName.isEmpty() ? QFileInfo(p_sourceDir).fileName() : p_destName;
    QString destPath = filePath(destName);
    copyDirRecursive(p_sourceDir, destPath);
    return destPath;
  }

private:
  static void copyDirRecursive(const QString &p_src, const QString &p_dest) {
    QDir srcDir(p_src);
    if (!srcDir.exists()) {
      return;
    }
    QDir().mkpath(p_dest);
    const auto entries =
        srcDir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : entries) {
      const QString destPath = p_dest + QStringLiteral("/") + fi.fileName();
      if (fi.isDir()) {
        copyDirRecursive(fi.absoluteFilePath(), destPath);
      } else {
        QFile::copy(fi.absoluteFilePath(), destPath);
      }
    }
  }
  QTemporaryDir m_dir;
};

} // namespace tests

#endif // TEMP_DIR_FIXTURE_H
