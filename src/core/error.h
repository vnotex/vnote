#ifndef ERROR_H
#define ERROR_H

#include <QString>

namespace vnotex {

enum class ErrorCode {
  Ok = 0,
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
  FileExistsOnCreate,
  DirExistsOnCreate,
  InvalidArgument,
  Cancelled
};

class Error {
public:
  Error();
  Error(ErrorCode p_code, const QString &p_message = QString());

  bool isOk() const;

  // Returns true if there is an error (for convenient error checking).
  // Usage: if (error) { handle error }
  explicit operator bool() const;

  ErrorCode code() const;
  QString message() const;

  QString what() const;

  static Error ok();
  static Error error(ErrorCode p_code, const QString &p_message = QString());

  static QString codeToString(ErrorCode p_code);

private:
  ErrorCode m_code = ErrorCode::Ok;
  QString m_message;
};

} // namespace vnotex

#endif // ERROR_H
