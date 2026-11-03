#ifndef VNOTEX_APICREDENTIALSTORE_H
#define VNOTEX_APICREDENTIALSTORE_H

#include <QString>
#include <api/iapicredentialstore.h>

namespace vnotex {

// Stores one-way token verifiers and grants, never bearer secrets. The directory
// is private to this store and must not be a notebook or a synchronized folder.
class ApiCredentialStore final : public IApiCredentialStore {
public:
  explicit ApiCredentialStore(QString p_directory);
  bool load(QJsonArray *p_records, QString *p_error) override;
  bool save(const QJsonArray &p_records, QString *p_error) override;

private:
  QString m_directory;
};

} // namespace vnotex

#endif // VNOTEX_APICREDENTIALSTORE_H
