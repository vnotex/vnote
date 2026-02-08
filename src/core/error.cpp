#include "error.h"

using namespace vnotex;

Error::Error() : m_code(ErrorCode::Ok) {}

Error::Error(ErrorCode p_code, const QString &p_message)
    : m_code(p_code), m_message(p_message) {}

bool Error::isOk() const { return m_code == ErrorCode::Ok; }

Error::operator bool() const { return !isOk(); }

ErrorCode Error::code() const { return m_code; }

QString Error::message() const { return m_message; }

QString Error::what() const {
  return QStringLiteral("Error Code: %1, Message: %2")
      .arg(codeToString(m_code))
      .arg(m_message);
}

Error Error::ok() { return Error(); }

Error Error::error(ErrorCode p_code, const QString &p_message) {
  return Error(p_code, p_message);
}

QString Error::codeToString(ErrorCode p_code) {
  switch (p_code) {
  case ErrorCode::Ok:
    return QStringLiteral("Ok");
  case ErrorCode::InvalidPath:
    return QStringLiteral("InvalidPath");
  case ErrorCode::FailToCreateDir:
    return QStringLiteral("FailToCreateDir");
  case ErrorCode::FailToWriteFile:
    return QStringLiteral("FailToWriteFile");
  case ErrorCode::FailToReadFile:
    return QStringLiteral("FailToReadFile");
  case ErrorCode::FailToRenameFile:
    return QStringLiteral("FailToRenameFile");
  case ErrorCode::FailToCopyFile:
    return QStringLiteral("FailToCopyFile");
  case ErrorCode::FailToCopyDir:
    return QStringLiteral("FailToCopyDir");
  case ErrorCode::FailToRemoveFile:
    return QStringLiteral("FailToRemoveFile");
  case ErrorCode::FailToRemoveDir:
    return QStringLiteral("FailToRemoveDir");
  case ErrorCode::FileMissingOnDisk:
    return QStringLiteral("FileMissingOnDisk");
  case ErrorCode::FileExistsOnCreate:
    return QStringLiteral("FileExistsOnCreate");
  case ErrorCode::DirExistsOnCreate:
    return QStringLiteral("DirExistsOnCreate");
  case ErrorCode::InvalidArgument:
    return QStringLiteral("InvalidArgument");
  }
  return QString::number(static_cast<int>(p_code));
}
