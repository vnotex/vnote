#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <QDebug>
#include <QString>
#include <stdexcept>

namespace vnotex {
class Exception : virtual public std::runtime_error {
public:
  enum class Type {
    InvalidPath,
    FailToCreateDir,
    FailToWriteFile,
    FailToReadFile,
    FailToRenameFile,
    FailToCopyFile,
    FailToCopyDir,
    FailToRemoveFile,
    FailToRemoveDir,
    FileMissingOnDisk,
    EssentialFileMissing,
    FileExistsOnCreate,
    DirExistsOnCreate,
    InvalidArgument
  };

  Exception(Type p_type, const QString &p_what)
      : std::runtime_error(p_what.toStdString()), m_type(p_type) {}

  Type m_type;

  [[noreturn]] static void throwOne(Exception::Type p_type, const QString &p_what) {
    qCritical() << typeToString(p_type) << p_what;
    throw Exception(p_type, p_what);
  }

private:
  static QString typeToString(Exception::Type p_type) {
    switch (p_type) {
    case Type::InvalidPath:
      return QStringLiteral("InvalidPath");

    case Type::FailToCreateDir:
      return QStringLiteral("FailToCreateDir");

    case Type::FailToWriteFile:
      return QStringLiteral("FailToWriteFile");

    case Type::FailToReadFile:
      return QStringLiteral("FailToReadFile");

    case Type::FailToRenameFile:
      return QStringLiteral("FailToRenameFile");

    case Type::FailToCopyFile:
      return QStringLiteral("FailToCopyFile");

    case Type::FailToCopyDir:
      return QStringLiteral("FailToCopyDir");

    case Type::FailToRemoveFile:
      return QStringLiteral("FailToRemoveFile");

    case Type::FailToRemoveDir:
      return QStringLiteral("FailToRemoveDir");

    case Type::FileMissingOnDisk:
      return QStringLiteral("FileMissingOnDisk");

    case Type::EssentialFileMissing:
      return QStringLiteral("EssentialFileMissing");

    case Type::FileExistsOnCreate:
      return QStringLiteral("FileExistsOnCreate");

    case Type::DirExistsOnCreate:
      return QStringLiteral("DirExistsOnCreate");

    case Type::InvalidArgument:
      return QStringLiteral("InvalidArgument");
    }

    return QString::number(static_cast<int>(p_type));
  }
};
} // namespace vnotex

#endif // EXCEPTION_H
