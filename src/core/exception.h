#ifndef EXCEPTION_H
#define EXCEPTION_H

#include <stdexcept>
#include <QString>
#include <QDebug>

namespace vnotex
{
    class Exception : virtual public std::runtime_error
    {
    public:
        enum class Type
        {
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
            InvalidArgument
        };

        Exception(Type p_type, const QString &p_what)
            : std::runtime_error(p_what.toStdString()),
              m_type(p_type)
        {
        }

        Type m_type;

        [[noreturn]] static void throwOne(Exception::Type p_type, const QString &p_what)
        {
            qCritical() << typeToString(p_type) << p_what;
            throw Exception(p_type, p_what);
        }

    private:
        static QString typeToString(Exception::Type p_type)
        {
            switch (p_type) {
            case Type::InvalidPath:
                return QString("InvalidPath");

            case Type::FailToCreateDir:
                return QString("FailToCreateDir");

            case Type::FailToWriteFile:
                return QString("FailToWriteFile");

            case Type::FailToReadFile:
                return QString("FailToReadFile");

            case Type::FailToRenameFile:
                return QString("FailToRenameFile");

            case Type::FailToCopyFile:
                return QString("FailToCopyFile");

            case Type::FailToCopyDir:
                return QString("FailToCopyDir");

            case Type::FailToRemoveFile:
                return QString("FailToRemoveFile");

            case Type::FailToRemoveDir:
                return QString("FailToRemoveDir");

            case Type::FileMissingOnDisk:
                return QString("FileMissingOnDisk");

            case Type::EssentialFileMissing:
                return QString("EssentialFileMissing");

            case Type::InvalidArgument:
                return QString("InvalidArgument");
            }

            return QString::number(static_cast<int>(p_type));
        }
    };
} // ns vnotex

#endif // EXCEPTION_H
