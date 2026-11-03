#ifndef VNOTEX_IAPICREDENTIALSTORE_H
#define VNOTEX_IAPICREDENTIALSTORE_H

#include <QJsonArray>
#include <QString>

namespace vnotex {
class IApiCredentialStore {
public:
  virtual ~IApiCredentialStore() = default;
  // Only the constructing/admin thread may access durable storage. Missing load does not create it.
  virtual bool load(QJsonArray *p_records, QString *p_error) = 0;
  virtual bool save(const QJsonArray &p_records, QString *p_error) = 0;
};
} // namespace vnotex
#endif
