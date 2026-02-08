#ifndef TEMP_DIR_FIXTURE_H
#define TEMP_DIR_FIXTURE_H

#include <QDir>
#include <QFile>
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

private:
  QTemporaryDir m_dir;
};

} // namespace tests

#endif // TEMP_DIR_FIXTURE_H
