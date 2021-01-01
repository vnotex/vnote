#ifndef SINGLEINSTANCEGUARD_H
#define SINGLEINSTANCEGUARD_H

#include <QObject>
#include <QString>
#include <QSharedPointer>

class QLocalServer;
class QLocalSocket;

namespace vnotex
{
    class SingleInstanceGuard : public QObject
    {
        Q_OBJECT
    public:
        SingleInstanceGuard();

        ~SingleInstanceGuard();

        // Try to run. Return true on success.
        bool tryRun();

        // Server API.
    public:
        // A running instance requests to exit.
        void exit();

        // Clients API.
    public:
        void requestOpenFiles(const QStringList &p_files);

        void requestShow();

    signals:
        void openFilesRequested(const QStringList &p_files);

        void showRequested();

    private:
        enum OpCode
        {
            Null = 0,
            Show
        };

        struct Command
        {
            void clear()
            {
                m_opCode = OpCode::Null;
                m_size = 0;
            }

            OpCode m_opCode = OpCode::Null;
            int m_size = 0;
        };

        QSharedPointer<QLocalSocket> tryConnect();

        QSharedPointer<QLocalServer> tryListen();

        void setupServer();

        void receiveCommand(QLocalSocket *p_socket);

        void sendRequest(QLocalSocket *p_socket, OpCode p_code, const QString &p_payload);

        // Whether succeeded to run.
        bool m_online = false;

        QSharedPointer<QLocalSocket> m_client;

        QSharedPointer<QLocalServer> m_server;

        bool m_ongoingConnect = false;

        Command m_command;

        static const QString c_serverName;
    };
} // ns vnotex

#endif // SINGLEINSTANCEGUARD_H
