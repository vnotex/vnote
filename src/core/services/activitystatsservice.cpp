#include "activitystatsservice.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <vxcore/notebook_json_keys.h>
#include <vxcore/vxcore.h>

using namespace vnotex;

namespace {

// vxcore query dates are local 'YYYY-MM-DD'.
QString formatDate(const QDate &p_date) {
  return p_date.toString(QStringLiteral("yyyy-MM-dd"));
}

// Activity JSON keys. These are activity-specific and NOT in the shared
// json-key-drift gated set; kept as named constants per repo convention.
constexpr char kKeyActiveMs[] = "activeMs";
constexpr char kKeyNotesCreated[] = "notesCreated";
constexpr char kKeyNotesRead[] = "notesRead";
constexpr char kKeyNotesEdited[] = "notesEdited";
constexpr char kKeyDaily[] = "daily";
constexpr char kKeyDate[] = "date";
constexpr char kKeyFiles[] = "files";
constexpr char kKeyFileId[] = "fileId";
constexpr char kKeyPath[] = "path";
constexpr char kKeyReads[] = "reads";
constexpr char kKeyEdits[] = "edits";
constexpr char kKeyScore[] = "score";

// Parse a vxcore JSON buffer (freeing it) into a QJsonObject. Returns an empty
// object on any failure.
QJsonObject parseAndFree(char *p_json) {
  if (!p_json) {
    return QJsonObject();
  }
  QJsonParseError parseErr;
  const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(p_json), &parseErr);
  vxcore_string_free(p_json);
  if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
    qWarning() << "ActivityStatsService: failed to parse activity JSON:" << parseErr.errorString();
    return QJsonObject();
  }
  return doc.object();
}

} // namespace

ActivityStatsService::ActivityStatsService(VxCoreContextHandle p_context, QObject *p_parent)
    : QObject(p_parent), m_context(p_context) {}

ActivityStatsService::ActivityRange ActivityStatsService::getRange(const QDate &p_from,
                                                                   const QDate &p_to) const {
  ActivityRange range;
  range.from = p_from;
  range.to = p_to;

  if (!m_context || !p_from.isValid() || !p_to.isValid()) {
    return range;
  }

  char *json = nullptr;
  const VxCoreError err = vxcore_activity_get_range(
      m_context, formatDate(p_from).toUtf8().constData(),
      formatDate(p_to).toUtf8().constData(), &json);
  if (err != VXCORE_OK) {
    qWarning() << "ActivityStatsService::getRange failed with error" << err;
    return range;
  }

  const QJsonObject obj = parseAndFree(json);
  range.activeMs = static_cast<qint64>(obj.value(QLatin1String(kKeyActiveMs)).toDouble());
  range.notesCreated = obj.value(QLatin1String(kKeyNotesCreated)).toInt();
  range.notesRead = obj.value(QLatin1String(kKeyNotesRead)).toInt();
  range.notesEdited = obj.value(QLatin1String(kKeyNotesEdited)).toInt();

  const QJsonArray daily = obj.value(QLatin1String(kKeyDaily)).toArray();
  range.daily.reserve(daily.size());
  for (const auto &dayVal : daily) {
    const QJsonObject dayObj = dayVal.toObject();
    ActivityDay day;
    day.date = QDate::fromString(dayObj.value(QLatin1String(kKeyDate)).toString(),
                                 QStringLiteral("yyyy-MM-dd"));
    day.activeMs = static_cast<qint64>(dayObj.value(QLatin1String(kKeyActiveMs)).toDouble());
    day.notesCreated = dayObj.value(QLatin1String(kKeyNotesCreated)).toInt();
    day.notesRead = dayObj.value(QLatin1String(kKeyNotesRead)).toInt();
    day.notesEdited = dayObj.value(QLatin1String(kKeyNotesEdited)).toInt();
    range.daily.append(day);
  }

  return range;
}

QVector<ActivityStatsService::HotFile>
ActivityStatsService::getHotFiles(const QDate &p_from, const QDate &p_to, int p_limit) const {
  QVector<HotFile> files;

  if (!m_context || !p_from.isValid() || !p_to.isValid() || p_limit <= 0) {
    return files;
  }

  char *json = nullptr;
  const VxCoreError err = vxcore_activity_get_hot_files(
      m_context, formatDate(p_from).toUtf8().constData(),
      formatDate(p_to).toUtf8().constData(), p_limit, &json);
  if (err != VXCORE_OK) {
    qWarning() << "ActivityStatsService::getHotFiles failed with error" << err;
    return files;
  }

  const QJsonObject obj = parseAndFree(json);
  const QJsonArray arr = obj.value(QLatin1String(kKeyFiles)).toArray();
  files.reserve(arr.size());
  for (const auto &fileVal : arr) {
    const QJsonObject fileObj = fileVal.toObject();
    HotFile file;
    file.notebookId = fileObj.value(QLatin1String(vxcore::kJsonKeyNotebookId)).toString();
    file.fileId = fileObj.value(QLatin1String(kKeyFileId)).toString();
    file.path = fileObj.value(QLatin1String(kKeyPath)).toString();
    file.reads = fileObj.value(QLatin1String(kKeyReads)).toInt();
    file.edits = fileObj.value(QLatin1String(kKeyEdits)).toInt();
    file.score = fileObj.value(QLatin1String(kKeyScore)).toDouble();
    files.append(file);
  }

  return files;
}
