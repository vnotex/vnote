#ifndef ICONFIG_H
#define ICONFIG_H

#include <QSharedPointer>
#include <QJsonObject>
#include <QJsonArray>
#include <QBitArray>
#include <QDataStream>
#include <QIODevice>

namespace vnotex
{
    class ConfigMgr;

    // Interface for Config.
    class IConfig
    {
    public:
        IConfig(ConfigMgr *p_mgr, IConfig *p_topConfig = nullptr)
            : m_topConfig(p_topConfig),
              m_mgr(p_mgr)
        {
        }

        virtual ~IConfig()
        {
        }

        // Called to init top level config.
        virtual void init()
        {
            Q_ASSERT(false);
        }

        // Init from QJsonObject.
        virtual void init(const QJsonObject &p_default, const QJsonObject &p_user)
        {
            Q_UNUSED(p_default);
            Q_UNUSED(p_user);
            Q_ASSERT(false);
        }

        virtual void writeToSettings() const
        {
            Q_ASSERT(m_topConfig);
            m_topConfig->writeToSettings();
        }

        virtual QJsonObject toJson() const = 0;

        const QString &getSessionName() const
        {
            return m_sessionName;
        }

        virtual int revision() const
        {
            return m_revision;
        }

        void update()
        {
            ++m_revision;
            writeToSettings();
        }

    protected:
        ConfigMgr *getMgr() const
        {
            return m_mgr;
        }

        // First read user config, then the default config.
        static QJsonValue read(const QJsonObject &p_default,
                               const QJsonObject &p_user,
                               const QString &p_key)
        {
            auto it = p_user.find(p_key);
            if (it != p_user.end()) {
                return it.value();
            } else {
                return p_default.value(p_key);
            }
        }

        static QString readString(const QJsonObject &p_default,
                                  const QJsonObject &p_user,
                                  const QString &p_key)
        {
            return read(p_default, p_user, p_key).toString();
        }

        static QStringList readStringList(const QJsonObject &p_default,
                                          const QJsonObject &p_user,
                                          const QString &p_key)
        {
            auto arr = read(p_default, p_user, p_key).toArray();
            QStringList res;
            res.reserve(arr.size());
            for (int i = 0; i < arr.size(); ++i) {
                res.push_back(arr[i].toString());
            }
            return res;
        }

        static QStringList readStringList(const QJsonObject &p_obj,
                                          const QString &p_key)
        {
            auto arr = p_obj.value(p_key).toArray();
            QStringList res;
            res.reserve(arr.size());
            for (int i = 0; i < arr.size(); ++i) {
                res.push_back(arr[i].toString());
            }
            return res;
        }

        static void writeStringList(QJsonObject &p_obj,
                                    const QString &p_key,
                                    const QStringList &p_list)
        {
            QJsonArray arr;
            for (const auto &ele : p_list) {
                arr.push_back(ele);
            }

            p_obj[p_key] = arr;
        }

        static QString readString(const QJsonObject &p_obj,
                                  const QString &p_key)
        {
            return p_obj.value(p_key).toString();
        }

        static QByteArray readByteArray(const QJsonObject &p_obj,
                                        const QString &p_key)
        {
            return QByteArray::fromBase64(readString(p_obj, p_key).toLatin1());
        }

        static void writeByteArray(QJsonObject &p_obj,
                                   const QString &p_key,
                                   const QByteArray &p_bytes)
        {
            p_obj.insert(p_key, QLatin1String(p_bytes.toBase64()));
        }

        static QBitArray readBitArray(const QJsonObject &p_obj,
                                      const QString &p_key)
        {
            auto bytes = readByteArray(p_obj, p_key);
            if (bytes.isEmpty()) {
                return QBitArray();
            }

            QDataStream ds(bytes);
            ds.setVersion(QDataStream::Qt_5_12);

            QBitArray bits;
            ds >> bits;
            return bits;
        }

        static void writeBitArray(QJsonObject &p_obj,
                                  const QString &p_key,
                                  const QBitArray &p_bits)
        {
            QByteArray bytes;
            QDataStream ds(&bytes, QIODevice::WriteOnly);
            ds.setVersion(QDataStream::Qt_5_12);
            ds << p_bits;

            writeByteArray(p_obj, p_key, bytes);
        }

        static bool readBool(const QJsonObject &p_default,
                             const QJsonObject &p_user,
                             const QString &p_key)
        {
            return read(p_default, p_user, p_key).toBool();
        }

        static bool readBool(const QJsonObject &p_obj,
                             const QString &p_key)
        {
            return p_obj.value(p_key).toBool();
        }

        static int readInt(const QJsonObject &p_default,
                           const QJsonObject &p_user,
                           const QString &p_key)
        {
            return read(p_default, p_user, p_key).toInt();
        }

        static qreal readReal(const QJsonObject &p_default,
                              const QJsonObject &p_user,
                              const QString &p_key)
        {
            return read(p_default, p_user, p_key).toDouble();
        }

        static bool isUndefinedKey(const QJsonObject &p_default,
                                   const QJsonObject &p_user,
                                   const QString &p_key)
        {
            return !p_default.contains(p_key) && !p_user.contains(p_key);
        }

        static bool isUndefinedKey(const QJsonObject &p_obj,
                                   const QString &p_key)
        {
            return !p_obj.contains(p_key);
        }

        template <typename T>
        static void updateConfig(T &p_cur, const T &p_new, IConfig *p_config)
        {
            if (p_cur == p_new) {
                return;
            }

            ++p_config->m_revision;
            p_cur = p_new;
            p_config->writeToSettings();
        }

        template <typename T>
        static void updateConfigWithoutCheck(T &p_cur, const T &p_new, IConfig *p_config)
        {
            ++p_config->m_revision;
            p_cur = p_new;
            p_config->writeToSettings();
        }

        IConfig *m_topConfig = nullptr;

        QString m_sessionName;

        // Used to indicate whether there is change after last read.
        int m_revision = 0;

    private:
        ConfigMgr *m_mgr = nullptr;
    };
} // ns vnotex

#endif // ICONFIG_H
