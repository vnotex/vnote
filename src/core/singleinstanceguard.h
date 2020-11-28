#ifndef SINGLEINSTANCEGUARD_H
#define SINGLEINSTANCEGUARD_H

#include <QSharedMemory>
#include <QStringList>

namespace vnotex
{
    class SingleInstanceGuard
    {
    public:
        SingleInstanceGuard();

        // Return ture if this is the only instance of VNote.
        bool tryRun();

        // There is already another instance running.
        // Call this to ask that instance to open external files passed in
        // via command line arguments.
        void openExternalFiles(const QStringList &p_files);

        // Ask another instance to show itself.
        void showInstance();

        // Fetch files from shared memory to open.
        // Will clear the shared memory.
        QStringList fetchFilesToOpen();

        // Whether this instance is asked to show itself.
        bool fetchAskedToShow();

        // A running instance requests to exit.
        void exit();

    private:
        // The count of the entries in the buffer to hold the path of the files to open.
        enum { FilesBufCount = 1024 };

        struct SharedStruct {
            // A magic number to identify if this struct is initialized
            int m_magic;

            // Next empty entry in m_filesBuf.
            int m_filesBufIdx;

            // File paths to be opened.
            // Encoded in this way with 2 bytes for each size part.
            // [size of file1][file1][size of file2][file 2]
            // Unicode representation of QString.
            ushort m_filesBuf[FilesBufCount];

            // Whether other instances ask to show the legal instance.
            bool m_askedToShow;
        };

        // Append @p_file to the shared struct files buffer.
        // Returns true if succeeds or false if there is no enough space.
        bool appendFileToBuffer(SharedStruct *p_str, const QString &p_file);

        bool m_online;

        QSharedMemory m_sharedMemory;

        static const QString c_memKey;
        static const int c_magic;
    };
} // ns vnotex

#endif // SINGLEINSTANCEGUARD_H
