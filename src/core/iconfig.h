#ifndef ICONFIG_H
#define ICONFIG_H

#include <QBitArray>
#include <QDataStream>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonObject>
#include <QSharedPointer>

namespace vnotex {
class IConfigMgr;

// Interface for Config.
class IConfig {
public:
  IConfig(IConfigMgr *p_mgr, IConfig *p_parentConfig = nullptr)
      : m_parentConfig(p_parentConfig), m_mgr(p_mgr) {}

  virtual ~IConfig() {}

  // Init from QJsonObject.
  virtual void fromJson(const QJsonObject &p_jobj) = 0;

  virtual QJsonObject toJson() const = 0;

  int revision() const { return m_revision; }

  const QString &getSectionName() const { return m_sectionName; }

  virtual void update() {
    ++m_revision;
    if (m_parentConfig) {
      m_parentConfig->update();
    }
  }

protected:
  IConfigMgr *getMgr() const { return m_mgr; }

  static QStringList readStringList(const QJsonObject &p_obj, const QString &p_key) {
    auto arr = p_obj.value(p_key).toArray();
    QStringList res;
    res.reserve(arr.size());
    for (int i = 0; i < arr.size(); ++i) {
      res.push_back(arr[i].toString());
    }
    return res;
  }

  static void writeStringList(QJsonObject &p_obj, const QString &p_key, const QStringList &p_list) {
    QJsonArray arr;
    for (const auto &ele : p_list) {
      arr.push_back(ele);
    }

    p_obj[p_key] = arr;
  }

  static QString readString(const QJsonObject &p_obj, const QString &p_key) {
    return p_obj.value(p_key).toString();
  }

  static QByteArray readByteArray(const QJsonObject &p_obj, const QString &p_key) {
    return QByteArray::fromBase64(readString(p_obj, p_key).toLatin1());
  }

  static void writeByteArray(QJsonObject &p_obj, const QString &p_key, const QByteArray &p_bytes) {
    p_obj.insert(p_key, QLatin1String(p_bytes.toBase64()));
  }

  static QBitArray readBitArray(const QJsonObject &p_obj, const QString &p_key) {
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

  static void writeBitArray(QJsonObject &p_obj, const QString &p_key, const QBitArray &p_bits) {
    QByteArray bytes;
    QDataStream ds(&bytes, QIODevice::WriteOnly);
    ds.setVersion(QDataStream::Qt_5_12);
    ds << p_bits;

    writeByteArray(p_obj, p_key, bytes);
  }

  static bool readBool(const QJsonObject &p_obj, const QString &p_key) {
    return p_obj.value(p_key).toBool();
  }

  // Presence-aware overload, for a key whose default is NOT false.
  //
  // The plain readBool() above maps an absent key to false, which is correct only for a
  // false-default setting. ConfigMgr2::init() skips fromJson() only when the config file is
  // entirely absent, so on every existing installation a newly introduced key IS read - and a
  // true-default one would silently flip to false on upgrade. Use this overload for any
  // true-default key you ADD.
  //
  // The pre-existing true-default keys (markdowneditorconfig, widgetconfig, texteditorconfig,
  // coreconfig) still use the two-argument form. They are not exposed to the upgrade path
  // above, because any config file written by a version that had them also contains them;
  // only a hand-edited or truncated file can hit it. Converting them is a separate audit with
  // its own behavior change, deliberately not folded into the commit that added this overload.
  static bool readBool(const QJsonObject &p_obj, const QString &p_key, bool p_defaultValue) {
    const auto value = p_obj.value(p_key);
    return value.isBool() ? value.toBool() : p_defaultValue;
  }

  static int readInt(const QJsonObject &p_obj, const QString &p_key) {
    return p_obj.value(p_key).toInt();
  }

  static qreal readReal(const QJsonObject &p_obj, const QString &p_key) {
    return p_obj.value(p_key).toDouble();
  }

  static QJsonValue read(const QJsonObject &p_obj, const QString &p_key) {
    return p_obj.value(p_key);
  }

  static bool isUndefinedKey(const QJsonObject &p_obj, const QString &p_key) {
    return !p_obj.contains(p_key);
  }

  template <typename T> static void updateConfig(T &p_cur, const T &p_new, IConfig *p_config) {
    if (p_cur == p_new) {
      return;
    }

    p_cur = p_new;
    p_config->update();
  }

  template <typename T>
  static void updateConfigWithoutCheck(T &p_cur, const T &p_new, IConfig *p_config) {
    p_cur = p_new;
    p_config->update();
  }

  IConfig *m_parentConfig = nullptr;

  QString m_sectionName;

  int m_revision = 0;

private:
  IConfigMgr *m_mgr = nullptr;
};
} // namespace vnotex

#endif // ICONFIG_H
