#include "apiauth.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>
#include <mutex>
#include <shared_mutex>

using namespace vnotex;

namespace {
const QSet<QString> c_scopes = {QStringLiteral("notes:read"), QStringLiteral("notes:read-live"),
                                QStringLiteral("notes:write"), QStringLiteral("notes:trash")};
struct Record {
  ApiPrincipal principal;
  QString label;
  QByteArray verifier;
  QDateTime issuedAt;
  std::optional<QDateTime> expiresAt;
  bool enabled = true;
};

bool fail(QString *p_error, const char *p_message) {
  if (p_error) {
    *p_error = QCoreApplication::translate("ApiAuth", p_message);
  }
  return false;
}

QJsonArray strings(const QSet<QString> &p_values) {
  QStringList sorted(p_values.begin(), p_values.end());
  sorted.sort();
  return QJsonArray::fromStringList(sorted);
}

QJsonObject publicRecord(const Record &p_record) {
  return {{QStringLiteral("id"), p_record.principal.tokenId},
          {QStringLiteral("label"), p_record.label},
          {QStringLiteral("notebookIds"), strings(p_record.principal.notebookIds)},
          {QStringLiteral("scopes"), strings(p_record.principal.scopes)},
          {QStringLiteral("issuedAt"), p_record.issuedAt.toUTC().toString(Qt::ISODateWithMs)},
          {QStringLiteral("expiresAt"),
           p_record.expiresAt ? QJsonValue(p_record.expiresAt->toUTC().toString(Qt::ISODateWithMs))
                              : QJsonValue(QJsonValue::Null)},
          {QStringLiteral("enabled"), p_record.enabled}};
}

QJsonArray serialize(const QHash<QString, Record> &p_records) {
  QStringList ids = p_records.keys();
  ids.sort();
  QJsonArray result;
  for (const auto &id : ids) {
    const auto &record = p_records.constFind(id).value();
    auto object = publicRecord(record);
    object.insert(QStringLiteral("verifier"), QString::fromLatin1(record.verifier.toHex()));
    result.append(object);
  }
  return result;
}

bool validId(const QString &p_value) {
  static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9_-]{1,128}$"));
  return expression.match(p_value).hasMatch();
}

bool stringSet(const QJsonValue &p_value, QSet<QString> *p_result) {
  if (!p_value.isArray() || p_value.toArray().isEmpty() || p_value.toArray().size() > 256) {
    return false;
  }
  for (const auto &item : p_value.toArray()) {
    if (!item.isString() || item.toString().isEmpty() || item.toString().size() > 128 ||
        p_result->contains(item.toString())) {
      return false;
    }
    p_result->insert(item.toString());
  }
  return true;
}

bool permits(const Record &p_record, const ApiPrincipal &p_principal, const ApiCommand &p_command) {
  if (!p_record.enabled ||
      (p_record.expiresAt && *p_record.expiresAt <= QDateTime::currentDateTimeUtc())) {
    return false;
  }
  auto hasNotebook = [&](const QString &id) {
    return p_record.principal.notebookIds.contains(id) && p_principal.notebookIds.contains(id);
  };
  if (!p_command.notebookId.isEmpty() && !hasNotebook(p_command.notebookId)) {
    return false;
  }
  for (const auto &id : p_command.notebookIds) {
    if (!hasNotebook(id)) {
      return false;
    }
  }
  // Empty search means the entire captured grant, not newly added notebooks.
  if (p_command.kind == ApiCommandKind::Search && p_command.notebookIds.isEmpty()) {
    for (const auto &id : p_principal.notebookIds) {
      if (!hasNotebook(id)) {
        return false;
      }
    }
  }
  QString scope;
  switch (p_command.kind) {
  case ApiCommandKind::CreateNote:
  case ApiCommandKind::ReplaceContent:
  case ApiCommandKind::AppendContent:
  case ApiCommandKind::RenameNote:
    scope = QStringLiteral("notes:write");
    break;
  case ApiCommandKind::TrashNote:
    scope = QStringLiteral("notes:trash");
    break;
  default:
    scope = QStringLiteral("notes:read");
    break;
  }
  if (!p_record.principal.scopes.contains(scope) || !p_principal.scopes.contains(scope)) {
    return false;
  }
  return p_command.kind != ApiCommandKind::ReadContent || p_command.view != ApiContentView::Live ||
         (p_record.principal.scopes.contains(QStringLiteral("notes:read-live")) &&
          p_principal.scopes.contains(QStringLiteral("notes:read-live")));
}

bool constantEqual(const QByteArray &p_left, const QByteArray &p_right) {
  // Both inputs are fixed SHA-256 digests. Volatile prevents an early-exit comparison rewrite.
  volatile unsigned int difference = 0;
  for (int i = 0; i < 32; ++i) {
    difference = difference |
                 (static_cast<unsigned char>(p_left[i]) ^ static_cast<unsigned char>(p_right[i]));
  }
  return difference == 0;
}

QByteArray randomBytes() {
  quint32 words[8];
  QRandomGenerator::system()->fillRange(words);
  return QByteArray(reinterpret_cast<const char *>(words), sizeof(words));
}
} // namespace

struct ApiAuth::State {
  mutable std::shared_mutex mutex;
  QHash<QString, Record> records;
  bool loaded = false;
  bool alive = true;
  quint64 generation = 0;
};

ApiAuth::ApiAuth(IApiCredentialStore &p_store)
    : m_store(p_store), m_owner(QThread::currentThread()), m_state(std::make_shared<State>()) {}

ApiAuth::~ApiAuth() {
  std::unique_lock lock(m_state->mutex);
  m_state->alive = false;
  m_state->records.clear();
}

bool ApiAuth::adminThread(QString *p_error) const {
  return QThread::currentThread() == m_owner ||
         fail(p_error, "Token administration must run on the owner thread");
}

bool ApiAuth::load(QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  if (!adminThread(p_error)) {
    return false;
  }
  QJsonArray array;
  QHash<QString, Record> records;
  bool valid = m_store.load(&array, p_error);
  if (valid && array.size() > 64) {
    valid = false;
  }
  static const QSet<QString> keys = {QStringLiteral("id"),        QStringLiteral("label"),
                                     QStringLiteral("verifier"),  QStringLiteral("notebookIds"),
                                     QStringLiteral("scopes"),    QStringLiteral("issuedAt"),
                                     QStringLiteral("expiresAt"), QStringLiteral("enabled")};
  static const QRegularExpression hexId(QStringLiteral("^[0-9a-f]{32}$"));
  static const QRegularExpression hexDigest(QStringLiteral("^[0-9a-f]{64}$"));
  for (const auto &value : array) {
    if (!valid || !value.isObject()) {
      valid = false;
      break;
    }
    const auto object = value.toObject();
    const auto objectKeys = object.keys();
    Record record;
    record.principal.tokenId = object.value(QStringLiteral("id")).toString();
    record.label = object.value(QStringLiteral("label")).toString();
    const auto verifier = object.value(QStringLiteral("verifier")).toString();
    record.issuedAt = QDateTime::fromString(object.value(QStringLiteral("issuedAt")).toString(),
                                            Qt::ISODateWithMs);
    const auto expiresAt = object.value(QStringLiteral("expiresAt"));
    if (!expiresAt.isNull()) {
      record.expiresAt = QDateTime::fromString(expiresAt.toString(), Qt::ISODateWithMs);
    }
    if (QSet<QString>(objectKeys.begin(), objectKeys.end()) != keys ||
        !hexId.match(record.principal.tokenId).hasMatch() ||
        records.contains(record.principal.tokenId) || record.label.trimmed().isEmpty() ||
        record.label.size() > 128 || !hexDigest.match(verifier).hasMatch() ||
        !stringSet(object.value(QStringLiteral("notebookIds")), &record.principal.notebookIds) ||
        !stringSet(object.value(QStringLiteral("scopes")), &record.principal.scopes) ||
        !c_scopes.contains(record.principal.scopes) || !record.issuedAt.isValid() ||
        (record.expiresAt &&
         (!record.expiresAt->isValid() || *record.expiresAt <= record.issuedAt)) ||
        !object.value(QStringLiteral("enabled")).isBool()) {
      valid = false;
      break;
    }
    for (const auto &id : record.principal.notebookIds) {
      if (!validId(id)) {
        valid = false;
      }
    }
    record.verifier = QByteArray::fromHex(verifier.toLatin1());
    record.enabled = object.value(QStringLiteral("enabled")).toBool();
    const auto tokenId = record.principal.tokenId;
    records.insert(tokenId, std::move(record));
  }
  std::unique_lock lock(m_state->mutex);
  ++m_state->generation;
  m_state->loaded = valid;
  m_state->records = valid ? std::move(records) : QHash<QString, Record>();
  if (!valid && (!p_error || p_error->isEmpty())) {
    fail(p_error, "The token registry is invalid");
  }
  return valid;
}

ApiAuth::IssueResult ApiAuth::issue(const ApiTokenGrant &p_grant, ApiIssuedToken *p_token,
                                    QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  if (p_token) {
    *p_token = {};
  }
  if (!adminThread(p_error)) {
    return IssueResult::Unavailable;
  }
  if (!p_token || p_grant.label.trimmed().isEmpty() || p_grant.label.size() > 128 ||
      p_grant.notebookIds.isEmpty() || p_grant.notebookIds.size() > 256 ||
      p_grant.scopes.isEmpty() || !c_scopes.contains(p_grant.scopes) ||
      (p_grant.expiresAt &&
       (!p_grant.expiresAt->isValid() || *p_grant.expiresAt <= QDateTime::currentDateTimeUtc()))) {
    fail(p_error, "Select notebooks, valid scopes, a label, and a valid expiry option");
    return IssueResult::Rejected;
  }
  for (const auto &id : p_grant.notebookIds) {
    if (!validId(id)) {
      fail(p_error, "Select notebooks, valid scopes, a label, and a valid expiry option");
      return IssueResult::Rejected;
    }
  }
  bool loaded;
  {
    std::shared_lock lock(m_state->mutex);
    loaded = m_state->loaded;
  }
  if (!loaded && !load(p_error)) {
    return IssueResult::Unavailable;
  }
  QHash<QString, Record> next;
  {
    std::shared_lock lock(m_state->mutex);
    next = m_state->records;
  }
  const auto now = QDateTime::currentDateTimeUtc();
  for (auto it = next.begin(); it != next.end();) {
    if (!it->enabled || (it->expiresAt && *it->expiresAt <= now)) {
      it = next.erase(it);
    } else {
      ++it;
    }
  }
  if (next.size() >= 64) {
    fail(p_error, "The token limit of 64 has been reached");
    return IssueResult::Rejected;
  }
  Record record;
  do {
    record.principal.tokenId = QString::fromLatin1(randomBytes().first(16).toHex());
  } while (next.contains(record.principal.tokenId));
  const auto secret =
      randomBytes().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  const auto bearer = record.principal.tokenId.toLatin1() + '.' + secret;
  record.principal.notebookIds = p_grant.notebookIds;
  record.principal.scopes = p_grant.scopes;
  record.label = p_grant.label.trimmed();
  record.verifier = QCryptographicHash::hash(bearer, QCryptographicHash::Sha256);
  record.issuedAt = now;
  if (p_grant.expiresAt) {
    record.expiresAt = p_grant.expiresAt->toUTC();
  }
  next.insert(record.principal.tokenId, record);
  if (!m_store.save(serialize(next), p_error)) {
    return IssueResult::Unavailable;
  }
  {
    std::unique_lock lock(m_state->mutex);
    m_state->records = std::move(next);
  }
  p_token->id = record.principal.tokenId;
  p_token->secret = QString::fromLatin1(bearer);
  return IssueResult::Issued;
}

bool ApiAuth::revoke(const QString &p_id, QString *p_error) {
  if (p_error) {
    p_error->clear();
  }
  if (!adminThread(p_error)) {
    return false;
  }
  QJsonArray records;
  {
    std::unique_lock lock(m_state->mutex);
    if (!m_state->loaded || !m_state->records.contains(p_id)) {
      return fail(p_error, "The token no longer exists");
    }
    m_state->records.remove(p_id);
    records = serialize(m_state->records);
  }
  // In-memory revocation is never rolled back. The controller must stop on save failure.
  return m_store.save(records, p_error);
}

QJsonArray ApiAuth::tokens() const {
  std::shared_lock lock(m_state->mutex);
  auto ids = m_state->records.keys();
  ids.sort();
  QJsonArray result;
  for (const auto &id : ids) {
    result.append(publicRecord(m_state->records.constFind(id).value()));
  }
  return result;
}

std::optional<ApiPrincipal> ApiAuth::authenticate(const QByteArray &p_bearerValue) const {
  if (p_bearerValue.size() != 76 || p_bearerValue[32] != '.') {
    return std::nullopt;
  }
  for (int i = 0; i < p_bearerValue.size(); ++i) {
    const char ch = p_bearerValue[i];
    if (i < 32 ? !((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))
               : i != 32 && !((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_')) {
      return std::nullopt;
    }
  }
  const auto digest = QCryptographicHash::hash(p_bearerValue, QCryptographicHash::Sha256);
  std::shared_lock lock(m_state->mutex);
  const auto it = m_state->records.constFind(QString::fromLatin1(p_bearerValue.first(32)));
  // Always compare a full digest, including for unknown token IDs.
  static const QByteArray absentVerifier(32, '\0');
  const QByteArray &expected = it == m_state->records.cend() ? absentVerifier : it->verifier;
  const bool equal = constantEqual(digest, expected);
  if (!equal || it == m_state->records.cend() || !m_state->alive || !it->enabled ||
      (it->expiresAt && *it->expiresAt <= QDateTime::currentDateTimeUtc())) {
    return std::nullopt;
  }
  return it->principal;
}

bool ApiAuth::isAuthorized(const ApiPrincipal &p_principal, const ApiCommand &p_command) const {
  std::shared_lock lock(m_state->mutex);
  const auto it = m_state->records.constFind(p_principal.tokenId);
  return m_state->alive && it != m_state->records.cend() && permits(*it, p_principal, p_command);
}

std::function<bool()> ApiAuth::authorizer(const ApiPrincipal &p_principal,
                                          const ApiCommand &p_command) const {
  std::weak_ptr<State> weak = m_state;
  quint64 generation;
  {
    std::shared_lock lock(m_state->mutex);
    generation = m_state->generation;
  }
  ApiCommand policyCommand;
  policyCommand.kind = p_command.kind;
  policyCommand.notebookId = p_command.notebookId;
  policyCommand.notebookIds = p_command.notebookIds;
  policyCommand.view = p_command.view;
  return [weak, generation, principal = p_principal, command = std::move(policyCommand)]() {
    const auto state = weak.lock();
    if (!state) {
      return false;
    }
    std::shared_lock lock(state->mutex);
    const auto it = state->records.constFind(principal.tokenId);
    return state->alive && state->generation == generation && it != state->records.cend() &&
           permits(*it, principal, command);
  };
}
