// Explicit executable, not a CTest timing test. Fixture setup and validation are untimed.
// JSON-to-Qt reference conversion belongs here only; typed conversion uses production code.
#include <QByteArrayView>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <core/notebookresulttypes.h>
#include <core/searchresulttypes.h>
#include <core/services/notebookcoreservice.h>
#include <core/services/tagcoreservice.h>
#include <vxcore/vxcore.h>
#include <vxcore/vxcore_log.h>

// Untimed fixture seeding only. Measured reads use the public C ABI and Qt adapters.
#include <core/context.h>
#include <core/metadata_store.h>
#include <core/notebook.h>
#include <core/notebook_manager.h>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>
#else
#include <sys/resource.h>
#ifdef Q_OS_LINUX
#include <unistd.h>
#elif defined(Q_OS_MACOS)
#include <mach/mach.h>
#endif
#endif

using namespace vnotex;

namespace {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;

[[noreturn]] void fail(const QString &message) {
  throw std::runtime_error(message.toUtf8().constData());
}

void require(bool condition, const QString &message) {
  if (!condition) {
    fail(message);
  }
}

void check(VxCoreError error, const char *operation) {
  if (error != VXCORE_OK) {
    fail(QStringLiteral("%1: %2 (%3)")
             .arg(QString::fromLatin1(operation), QString::fromUtf8(vxcore_error_message(error)))
             .arg(static_cast<int>(error)));
  }
}

double milliseconds(Time begin, Time end) {
  return std::chrono::duration<double, std::milli>(end - begin).count();
}

template <typename T, auto Free> struct Deleter {
  void operator()(T *value) const { Free(value); }
};
template <typename T, auto Free> using Owner = std::unique_ptr<T, Deleter<T, Free>>;
using JsonOwner = Owner<char, vxcore_string_free>;
using JsonResults = std::vector<JsonOwner>;
using FolderOwner = Owner<VxCoreFolderListing, vxcore_folder_listing_free>;
using FolderOwners = std::vector<FolderOwner>;
using FileOwner = Owner<VxCoreFileSearchResult, vxcore_file_search_result_free>;
using ContentOwner = Owner<VxCoreContentSearchResult, vxcore_content_search_result_free>;
using TagsOwner = Owner<VxCoreTagList, vxcore_tag_list_free>;
using CountsOwner = Owner<VxCoreTagCounts, vxcore_tag_counts_free>;
using TagFilesOwner = Owner<VxCoreTagFileResult, vxcore_tag_file_result_free>;

template <typename Result, typename Function>
Result acquire(Function function, const char *operation = "result acquisition") {
  typename Result::pointer pointer = nullptr;
  const auto error = function(&pointer);
  Result result(pointer);
  check(error, operation);
  require(pointer != nullptr, QStringLiteral("Successful result acquisition returned no owner"));
  return result;
}

template <typename Function>
JsonResults acquireJson(Function function, const char *operation = "JSON result acquisition") {
  JsonResults results;
  results.push_back(acquire<JsonOwner>(function, operation));
  return results;
}

QByteArray compact(const QJsonObject &object) {
  return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QJsonDocument parse(const char *json) {
  require(json != nullptr, QStringLiteral("Null JSON result"));
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(
      QByteArray::fromRawData(json, static_cast<qsizetype>(std::strlen(json))), &error);
  require(error.error == QJsonParseError::NoError, error.errorString());
  return document;
}

QStringList strings(const QJsonValue &value) {
  const auto array = value.toArray();
  QStringList result;
  result.reserve(array.size());
  for (const auto &entry : array) {
    result.append(entry.toString());
  }
  return result;
}

QJsonValue metadata(const QJsonObject &object) {
  return object.contains(QStringLiteral("metadata")) ? object.value(QStringLiteral("metadata"))
                                                     : QJsonValue(QJsonObject());
}

// Reference adapters deliberately materialize every field in the same owning Qt records.
FolderListing folderFromJson(const QJsonObject &object) {
  FolderListing result;
  const auto entries = [](const QJsonArray &array) {
    QVector<FolderEntry> output;
    output.reserve(array.size());
    for (const auto &value : array) {
      const auto item = value.toObject();
      FolderEntry entry;
      entry.m_id = item.value(QStringLiteral("id")).toString();
      entry.m_name = item.value(QStringLiteral("name")).toString();
      entry.m_createdUtc = item.value(QStringLiteral("createdUtc")).toInteger();
      entry.m_modifiedUtc = item.value(QStringLiteral("modifiedUtc")).toInteger();
      entry.m_metadata = metadata(item);
      entry.m_tags = strings(item.value(QStringLiteral("tags")));
      entry.m_attachments = strings(item.value(QStringLiteral("attachments")));
      entry.m_existsKnown = item.value(QStringLiteral("exists")).isBool();
      entry.m_exists = item.value(QStringLiteral("exists")).toBool(true);
      output.append(std::move(entry));
    }
    return output;
  };
  result.m_files = entries(object.value(QStringLiteral("files")).toArray());
  result.m_folders = entries(object.value(QStringLiteral("folders")).toArray());
  return result;
}

QVector<FolderListing> foldersFromJson(const JsonResults &json) {
  QVector<FolderListing> result;
  result.reserve(static_cast<qsizetype>(json.size()));
  for (const auto &item : json) {
    result.append(folderFromJson(parse(item.get()).object()));
  }
  return result;
}

SearchResult filesFromJson(const JsonResults &json, const QString &notebookId) {
  const auto object = parse(json.front().get()).object();
  SearchResult result;
  result.m_matchCount = object.value(QStringLiteral("matchCount")).toInt();
  result.m_truncated = object.value(QStringLiteral("truncated")).toBool();
  const auto files = object.value(QStringLiteral("matches")).toArray();
  result.m_fileResults.reserve(files.size());
  for (const auto &value : files) {
    const auto item = value.toObject();
    SearchFileResult file;
    file.m_type = item.value(QStringLiteral("type")).toString() == QStringLiteral("folder")
                      ? SearchResultType::Folder
                      : SearchResultType::File;
    file.m_path = item.value(QStringLiteral("path")).toString();
    file.m_absolutePath = item.value(QStringLiteral("absolute_path")).toString();
    file.m_id = item.value(QStringLiteral("id")).toString();
    file.m_notebookId = notebookId;
    file.m_tags = strings(item.value(QStringLiteral("tags")));
    file.m_createdUtc = item.value(QStringLiteral("createdUtc")).toInteger();
    file.m_modifiedUtc = item.value(QStringLiteral("modifiedUtc")).toInteger();
    result.m_fileResults.append(std::move(file));
  }
  return result;
}

SearchContentResult contentFromJson(const char *json) {
  const auto object = parse(json).object();
  SearchContentResult result;
  result.m_truncated = object.value(QStringLiteral("truncated")).toBool();
  result.m_encryptedSkippedCount =
      object.value(QStringLiteral("encryptedSkippedCount")).toInteger();
  const auto files = object.value(QStringLiteral("matches")).toArray();
  result.m_files.reserve(files.size());
  for (const auto &value : files) {
    const auto item = value.toObject();
    SearchContentFile file;
    file.m_path = item.value(QStringLiteral("path")).toString();
    file.m_id = item.value(QStringLiteral("id")).toString();
    const auto matches = item.value(QStringLiteral("matches")).toArray();
    file.m_matches.reserve(matches.size());
    for (const auto &matchValue : matches) {
      const auto match = matchValue.toObject();
      SearchOccurrence occurrence;
      occurrence.m_lineNumber = match.value(QStringLiteral("lineNumber")).toInt(-1);
      occurrence.m_columnStart = match.value(QStringLiteral("columnStart")).toInt();
      occurrence.m_columnEnd = match.value(QStringLiteral("columnEnd")).toInt();
      occurrence.m_lineText = match.value(QStringLiteral("lineText")).toString();
      file.m_matches.append(std::move(occurrence));
    }
    result.m_files.append(std::move(file));
  }
  return result;
}

QVector<TagRecord> tagsFromJson(const JsonResults &json) {
  const auto array = parse(json.front().get()).array();
  QVector<TagRecord> result;
  result.reserve(array.size());
  for (const auto &value : array) {
    const auto object = value.toObject();
    TagRecord tag;
    tag.m_name = object.value(QStringLiteral("name")).toString();
    tag.m_parent = object.value(QStringLiteral("parent")).toString();
    tag.m_metadata = metadata(object);
    result.append(std::move(tag));
  }
  return result;
}

QVector<TagCountRecord> countsFromJson(const JsonResults &json) {
  const auto array = parse(json.front().get()).array();
  QVector<TagCountRecord> result;
  result.reserve(array.size());
  for (const auto &value : array) {
    const auto object = value.toObject();
    TagCountRecord tag;
    tag.m_tag = object.value(QStringLiteral("tag")).toString();
    tag.m_count = object.value(QStringLiteral("count")).toInteger();
    result.append(std::move(tag));
  }
  return result;
}

QVector<TagFileMatch> tagFilesFromJson(const JsonResults &json) {
  const auto array = parse(json.front().get()).object().value(QStringLiteral("matches")).toArray();
  QVector<TagFileMatch> result;
  result.reserve(array.size());
  for (const auto &value : array) {
    const auto object = value.toObject();
    TagFileMatch file;
    file.m_filePath = object.value(QStringLiteral("filePath")).toString();
    file.m_fileName = object.value(QStringLiteral("fileName")).toString();
    file.m_tags = strings(object.value(QStringLiteral("tags")));
    if (object.contains(QStringLiteral("notebookId"))) {
      file.m_notebookId = object.value(QStringLiteral("notebookId")).toString();
    }
    result.append(std::move(file));
  }
  return result;
}

// Untimed concrete normalization. File/tag enumeration order may differ between backend runs;
// folder child order, tag arrays, occurrence order, null notebook IDs and every field are kept.
void encode(QDataStream &stream, const FolderListing &value) {
  const auto entries = [&stream](const QVector<FolderEntry> &items) {
    stream << qint64(items.size());
    for (const auto &entry : items) {
      stream << entry.m_id << entry.m_name << entry.m_createdUtc << entry.m_modifiedUtc
             << entry.m_metadata << entry.m_tags << entry.m_attachments << entry.m_existsKnown
             << entry.m_exists;
    }
  };
  entries(value.m_files);
  entries(value.m_folders);
}

void encode(QDataStream &stream, const QVector<FolderListing> &values) {
  stream << qint64(values.size());
  for (const auto &value : values) {
    encode(stream, value);
  }
}

template <typename T, typename Key, typename Encode>
void encodeSorted(QDataStream &stream, const QVector<T> &values, Key key, Encode encodeItem) {
  std::vector<const T *> sorted;
  sorted.reserve(static_cast<size_t>(values.size()));
  for (const auto &value : values) {
    sorted.push_back(&value);
  }
  std::sort(sorted.begin(), sorted.end(),
            [&](const T *a, const T *b) { return key(*a) < key(*b); });
  stream << qint64(sorted.size());
  for (const auto *value : sorted) {
    encodeItem(stream, *value);
  }
}

void encode(QDataStream &stream, const SearchContentResult &value) {
  stream << value.m_truncated << qint64(value.m_encryptedSkippedCount);
  encodeSorted(
      stream, value.m_files, [](const auto &file) { return file.m_path; },
      [](QDataStream &out, const SearchContentFile &file) {
        out << file.m_path << file.m_id << qint64(file.m_matches.size());
        for (const auto &match : file.m_matches) {
          out << qint32(match.m_lineNumber) << qint32(match.m_columnStart)
              << qint32(match.m_columnEnd) << match.m_lineText;
        }
      });
}

void encode(QDataStream &stream, const SearchResult &value) {
  stream << qint64(value.m_matchCount) << value.m_truncated
         << qint64(value.m_encryptedSkippedCount);
  encodeSorted(
      stream, value.m_fileResults, [](const auto &file) { return file.m_path; },
      [](QDataStream &out, const SearchFileResult &file) {
        out << qint32(file.m_type) << file.m_path << file.m_absolutePath << file.m_id
            << file.m_notebookId.isNull() << file.m_notebookId << file.m_tags << file.m_createdUtc
            << file.m_modifiedUtc << qint64(file.m_matchCount) << file.m_replacementSupported
            << qint64(file.m_lineMatches.size());
        for (const auto &line : file.m_lineMatches) {
          out << qint32(line.m_lineNumber) << line.m_lineText << qint64(line.m_segments.size());
          for (const auto &segment : line.m_segments) {
            out << qint32(segment.m_columnStart) << qint32(segment.m_columnEnd);
          }
        }
      });
}

void encode(QDataStream &stream, const QVector<TagRecord> &values) {
  encodeSorted(
      stream, values, [](const auto &tag) { return tag.m_name; },
      [](QDataStream &out, const TagRecord &tag) {
        out << tag.m_name << tag.m_parent << tag.m_metadata;
      });
}

void encode(QDataStream &stream, const QVector<TagCountRecord> &values) {
  encodeSorted(
      stream, values, [](const auto &tag) { return tag.m_tag; },
      [](QDataStream &out, const TagCountRecord &tag) { out << tag.m_tag << tag.m_count; });
}

void encode(QDataStream &stream, const QVector<TagFileMatch> &values) {
  encodeSorted(
      stream, values, [](const auto &file) { return file.m_filePath; },
      [](QDataStream &out, const TagFileMatch &file) {
        out << file.m_filePath << file.m_fileName << file.m_tags << file.m_notebookId.isNull()
            << file.m_notebookId;
      });
}

template <typename T> QByteArray normalized(const T &value) {
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_6_0);
  encode(stream, value);
  require(stream.status() == QDataStream::Ok, QStringLiteral("Normalization failed"));
  return bytes;
}

QString checksum(const QByteArray &bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

qint64 resultCount(const QVector<FolderListing> &values) {
  qint64 result = 0;
  for (const auto &value : values) {
    result += value.m_files.size() + value.m_folders.size();
  }
  return result;
}
qint64 resultCount(const SearchResult &value) { return value.m_fileResults.size(); }
qint64 resultCount(const SearchContentResult &value) { return value.m_files.size(); }
template <typename T> qint64 resultCount(const QVector<T> &value) { return value.size(); }

template <typename T> qint64 occurrenceCount(const T &) { return -1; }
qint64 occurrenceCount(const SearchContentResult &value) {
  qint64 count = 0;
  for (const auto &file : value.m_files) {
    count += file.m_matches.size();
  }
  return count;
}
qint64 occurrenceCount(const SearchResult &value) {
  qint64 count = 0;
  for (const auto &file : value.m_fileResults) {
    count += file.m_matchCount;
  }
  return count;
}

struct Payload {
  qint64 count = 0;
  quint64 bytes = 0;
};

quint64 stringBytes(const VxCoreUtf8View *strings, size_t count) {
  quint64 bytes = count * sizeof(VxCoreUtf8View);
  for (size_t i = 0; i < count; ++i) {
    bytes += strings[i].size;
  }
  return bytes;
}

Payload payload(const FolderOwners &owners) {
  Payload result;
  for (const auto &owner : owners) {
    const auto &view = *vxcore_folder_listing_view(owner.get());
    result.count += static_cast<qint64>(view.file_count + view.folder_count);
    result.bytes +=
        sizeof(view) + (view.file_count + view.folder_count) * sizeof(VxCoreFolderEntryView);
    const auto add = [&result](const VxCoreFolderEntryView *entries, size_t count) {
      for (size_t i = 0; i < count; ++i) {
        const auto &entry = entries[i];
        result.bytes += entry.id.size + entry.name.size + entry.metadata_json.size +
                        stringBytes(entry.tags, entry.tag_count) +
                        stringBytes(entry.attachments, entry.attachment_count);
      }
    };
    add(view.files, view.file_count);
    add(view.folders, view.folder_count);
  }
  return result;
}

Payload payload(const FileOwner &owner) {
  const auto &view = *vxcore_file_search_result_view(owner.get());
  Payload result{static_cast<qint64>(view.file_count),
                 sizeof(view) + view.file_count * sizeof(*view.files)};
  for (size_t i = 0; i < view.file_count; ++i) {
    const auto &file = view.files[i];
    result.bytes += file.path.size + file.absolute_path.size + file.id.size +
                    stringBytes(file.tags, file.tag_count);
  }
  return result;
}

Payload payload(const VxCoreContentSearchView &view) {
  Payload result{static_cast<qint64>(view.file_count),
                 sizeof(view) + view.file_count * sizeof(*view.files)};
  for (size_t i = 0; i < view.file_count; ++i) {
    const auto &file = view.files[i];
    result.bytes += file.path.size + file.id.size + file.match_count * sizeof(*file.matches);
    for (size_t j = 0; j < file.match_count; ++j) {
      result.bytes += file.matches[j].line_text.size;
    }
  }
  return result;
}
Payload payload(const ContentOwner &owner) {
  return payload(*vxcore_content_search_result_view(owner.get()));
}

Payload payload(const TagsOwner &owner) {
  const auto &view = *vxcore_tag_list_view(owner.get());
  Payload result{static_cast<qint64>(view.tag_count),
                 sizeof(view) + view.tag_count * sizeof(*view.tags)};
  for (size_t i = 0; i < view.tag_count; ++i) {
    result.bytes +=
        view.tags[i].name.size + view.tags[i].parent.size + view.tags[i].metadata_json.size;
  }
  return result;
}

Payload payload(const CountsOwner &owner) {
  const auto &view = *vxcore_tag_counts_view(owner.get());
  Payload result{static_cast<qint64>(view.tag_count),
                 sizeof(view) + view.tag_count * sizeof(*view.tags)};
  for (size_t i = 0; i < view.tag_count; ++i) {
    result.bytes += view.tags[i].tag.size;
  }
  return result;
}

Payload payload(const TagFilesOwner &owner) {
  const auto &view = *vxcore_tag_file_result_view(owner.get());
  Payload result{static_cast<qint64>(view.file_count),
                 sizeof(view) + view.file_count * sizeof(*view.files)};
  for (size_t i = 0; i < view.file_count; ++i) {
    const auto &file = view.files[i];
    result.bytes +=
        file.file_path.size + file.file_name.size + stringBytes(file.tags, file.tag_count);
  }
  return result;
}

Payload payload(const JsonResults &json) {
  Payload result;
  for (const auto &value : json) {
    result.bytes += std::strlen(value.get());
  }
  return result;
}

QJsonObject memory() {
  QJsonObject result;
#ifdef Q_OS_WIN
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                           sizeof(counters))) {
    result[QStringLiteral("resident_bytes")] = static_cast<qint64>(counters.WorkingSetSize);
    result[QStringLiteral("process_lifetime_peak_resident_bytes")] =
        static_cast<qint64>(counters.PeakWorkingSetSize);
    result[QStringLiteral("private_committed_bytes")] = static_cast<qint64>(counters.PrivateUsage);
  }
  result[QStringLiteral("source")] = QStringLiteral("GetProcessMemoryInfo");
#else
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
#ifdef Q_OS_MACOS
    result[QStringLiteral("process_lifetime_peak_resident_bytes")] =
        static_cast<qint64>(usage.ru_maxrss);
#else
    result[QStringLiteral("process_lifetime_peak_resident_bytes")] =
        static_cast<qint64>(usage.ru_maxrss) * 1024;
#endif
  }
#ifdef Q_OS_LINUX
  QFile statm(QStringLiteral("/proc/self/statm"));
  if (statm.open(QIODevice::ReadOnly)) {
    const auto fields = statm.readAll().simplified().split(' ');
    bool ok = false;
    const auto pages = fields.size() > 1 ? fields.at(1).toLongLong(&ok) : 0;
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (ok && pageSize > 0) {
      result[QStringLiteral("resident_bytes")] = pages * pageSize;
    }
  }
  result[QStringLiteral("source")] = QStringLiteral("getrusage(RUSAGE_SELF) and /proc/self/statm");
#elif defined(Q_OS_MACOS)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info),
                &count) == KERN_SUCCESS) {
    result[QStringLiteral("resident_bytes")] = static_cast<qint64>(info.resident_size);
  }
  result[QStringLiteral("source")] = QStringLiteral("getrusage(RUSAGE_SELF) and task_info");
#else
  result[QStringLiteral("source")] = QStringLiteral("getrusage(RUSAGE_SELF)");
#endif
#endif
  return result;
}

QJsonObject distribution(std::vector<double> values) {
  require(!values.empty(), QStringLiteral("Empty measurement series"));
  std::sort(values.begin(), values.end());
  const size_t middle = values.size() / 2;
  const double median =
      values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
  const size_t p95 = static_cast<size_t>(std::ceil(0.95 * static_cast<double>(values.size()))) - 1;
  return {{QStringLiteral("samples"), static_cast<qint64>(values.size())},
          {QStringLiteral("median"), median},
          {QStringLiteral("p95"), values[p95]},
          {QStringLiteral("min"), values.front()},
          {QStringLiteral("max"), values.back()}};
}

struct Sample {
  double acquisition = 0;
  double conversion = -1;
  double release = 0;
  double total = 0;
  qint64 count = 0;
  qint64 occurrences = -1;
  quint64 bytes = 0;
};

QJsonObject sampleJson(const Sample &sample) {
  return {{QStringLiteral("acquisition_transport_ms"), sample.acquisition},
          {QStringLiteral("conversion_ms"),
           sample.conversion >= 0 ? QJsonValue(sample.conversion) : QJsonValue()},
          {QStringLiteral("native_release_ms"), sample.release},
          {QStringLiteral("total_ms"), sample.total},
          {QStringLiteral("result_count"), sample.count},
          {QStringLiteral("occurrence_count"),
           sample.occurrences >= 0 ? QJsonValue(sample.occurrences) : QJsonValue()},
          {QStringLiteral("payload_bytes"), static_cast<qint64>(sample.bytes)}};
}

QJsonObject samplesJson(const std::vector<Sample> &samples) {
  std::vector<double> acquisition, conversion, release, total, counts, occurrences, bytes;
  for (const auto &sample : samples) {
    acquisition.push_back(sample.acquisition);
    if (sample.conversion >= 0) {
      conversion.push_back(sample.conversion);
    }
    release.push_back(sample.release);
    total.push_back(sample.total);
    counts.push_back(static_cast<double>(sample.count));
    if (sample.occurrences >= 0) {
      occurrences.push_back(static_cast<double>(sample.occurrences));
    }
    bytes.push_back(static_cast<double>(sample.bytes));
  }
  return {{QStringLiteral("acquisition_transport_ms"), distribution(acquisition)},
          {QStringLiteral("conversion_ms"),
           conversion.empty() ? QJsonValue() : QJsonValue(distribution(conversion))},
          {QStringLiteral("native_release_ms"), distribution(release)},
          {QStringLiteral("total_ms"), distribution(total)},
          {QStringLiteral("result_count"), distribution(counts)},
          {QStringLiteral("occurrence_count"),
           occurrences.empty() ? QJsonValue() : QJsonValue(distribution(occurrences))},
          {QStringLiteral("payload_bytes"), distribution(bytes)}};
}

struct Options {
  QStringList domains, notebooks, metadata, shapes, hierarchies, densities, backends;
  std::vector<int> sizes;
  int pairs = 30;
  int warmups = 3;
  int occurrences = 4;
  int batchSize = 32;
  QString filter;
  QString tempParent;
  bool keepFixtures = false;
};

// Keep completed cases available even when a later fixture or command times out.
void reportProgress(const char *phase, QJsonObject description) {
  description[QStringLiteral("progress")] = QString::fromLatin1(phase);
  const auto line = QJsonDocument(description).toJson(QJsonDocument::Compact);
  std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

// The C ABI combines production, transport construction and (JSON) serialization in one call.
// Do not invent a separate native producer time or subtract two noisy samples to estimate it.
template <typename JsonAcquire, typename NativeAcquire, typename JsonConvert, typename TypedConvert>
void pairedCase(QJsonArray &rows, QJsonObject description, const Options &options,
                qint64 expectedCount, JsonAcquire jsonAcquire, NativeAcquire nativeAcquire,
                JsonConvert jsonConvert, TypedConvert typedConvert,
                qint64 expectedOccurrences = -1) try {
  if (!description.value(QStringLiteral("operation")).toString().contains(options.filter)) {
    return;
  }
  reportProgress("case_begin", description);
  const auto before = memory();
  description[QStringLiteral("active_phase")] =
      QStringLiteral("JSON semantic preflight acquisition");
  auto json = jsonAcquire();
  description[QStringLiteral("active_phase")] =
      QStringLiteral("typed semantic preflight acquisition");
  auto native = nativeAcquire();
  description[QStringLiteral("active_phase")] = QStringLiteral("JSON reference conversion");
  const auto jsonValue = jsonConvert(json);
  description[QStringLiteral("active_phase")] =
      QStringLiteral("production typed conversion/service");
  const auto typedValue = typedConvert(native);
  description[QStringLiteral("active_phase")] = QStringLiteral("concrete semantic comparison");
  const auto reference = normalized(jsonValue);
  require(reference == normalized(typedValue),
          QStringLiteral("JSON/typed semantic mismatch: %1")
              .arg(description.value(QStringLiteral("operation")).toString()));
  require(resultCount(jsonValue) == expectedCount && payload(native).count == expectedCount,
          QStringLiteral("Fixture result count mismatch: %1")
              .arg(description.value(QStringLiteral("operation")).toString()));
  require(expectedOccurrences < 0 || occurrenceCount(jsonValue) == expectedOccurrences,
          QStringLiteral("Fixture repeated-occurrence count mismatch"));
  description[QStringLiteral("normalized_sha256")] = checksum(reference);
  description[QStringLiteral("normalized_bytes")] = reference.size();

  const auto jsonRun = [&]() {
    description[QStringLiteral("active_phase")] =
        QStringLiteral("JSON acquisition/conversion measurement");
    Sample sample;
    const auto begin = Clock::now();
    auto owner = jsonAcquire();
    const auto acquired = Clock::now();
    auto value = jsonConvert(owner);
    const auto converted = Clock::now();
    sample.bytes = payload(owner).bytes; // Traversal/counting is deliberately outside timed spans.
    sample.count = resultCount(value);
    sample.occurrences = occurrenceCount(value);
    const auto releaseBegin = Clock::now();
    owner = decltype(owner)();
    const auto released = Clock::now();
    sample.acquisition = milliseconds(begin, acquired);
    sample.conversion = milliseconds(acquired, converted);
    sample.release = milliseconds(releaseBegin, released);
    sample.total = sample.acquisition + sample.conversion + sample.release;
    require(sample.count == expectedCount, QStringLiteral("JSON count changed during measurement"));
    return sample;
  };
  const auto typedRun = [&]() {
    description[QStringLiteral("active_phase")] =
        QStringLiteral("typed acquisition/conversion measurement");
    Sample sample;
    const auto begin = Clock::now();
    auto owner = nativeAcquire();
    const auto acquired = Clock::now();
    auto value = typedConvert(owner);
    const auto converted = Clock::now();
    sample.count = resultCount(value);
    sample.occurrences = occurrenceCount(value);
    sample.bytes = payload(owner).bytes;
    const auto releaseBegin = Clock::now();
    owner = decltype(owner)();
    const auto released = Clock::now();
    sample.conversion = milliseconds(acquired, converted);
    sample.release = milliseconds(releaseBegin, released);
    sample.total = milliseconds(begin, acquired) + sample.conversion + sample.release;
    sample.acquisition = milliseconds(begin, acquired);
    require(sample.count == expectedCount,
            QStringLiteral("Typed Qt count changed during measurement"));
    return sample;
  };

  // These are first observed measurement pairs, NOT cold-cache claims: setup/validation already
  // opened the notebook and read its index, and this executable does not evict filesystem caches.
  const auto firstJson = jsonRun();
  const auto firstTyped = typedRun();
  for (int i = 0; i < options.warmups; ++i) {
    if (i % 2) {
      typedRun();
      jsonRun();
    } else {
      jsonRun();
      typedRun();
    }
  }
  std::vector<Sample> jsonSamples, typedSamples;
  jsonSamples.reserve(options.pairs);
  typedSamples.reserve(options.pairs);
  for (int i = 0; i < options.pairs; ++i) {
    if (i % 2) {
      typedSamples.push_back(typedRun());
      jsonSamples.push_back(jsonRun());
    } else {
      jsonSamples.push_back(jsonRun());
      typedSamples.push_back(typedRun());
    }
  }

  // A separate retained-snapshot experiment isolates warm-cache Qt construction from C calls.
  std::vector<double> jsonConversions, typedConversions;
  const auto convertJson = [&]() {
    const auto begin = Clock::now();
    auto value = jsonConvert(json);
    const auto end = Clock::now();
    require(resultCount(value) == expectedCount, QStringLiteral("Retained JSON count mismatch"));
    return milliseconds(begin, end);
  };
  const auto convertTyped = [&]() {
    const auto begin = Clock::now();
    auto value = typedConvert(native);
    const auto end = Clock::now();
    require(resultCount(value) == expectedCount, QStringLiteral("Retained typed count mismatch"));
    return milliseconds(begin, end);
  };
  for (int i = -options.warmups; i < options.pairs; ++i) {
    double jsonTime = 0, typedTime = 0;
    if (i % 2) {
      typedTime = convertTyped();
      jsonTime = convertJson();
    } else {
      jsonTime = convertJson();
      typedTime = convertTyped();
    }
    if (i >= 0) {
      jsonConversions.push_back(jsonTime);
      typedConversions.push_back(typedTime);
    }
  }
  description[QStringLiteral("status")] = QStringLiteral("ok");
  description[QStringLiteral("typed_path")] =
      QStringLiteral("typed C API and production fromView conversion");
  description[QStringLiteral("total_scope")] = QStringLiteral(
      "sum of acquisition, Qt conversion and native free; Qt value destruction excluded");
  description[QStringLiteral("first_observed_pair")] =
      QJsonObject{{QStringLiteral("json"), sampleJson(firstJson)},
                  {QStringLiteral("typed"), sampleJson(firstTyped)}};
  description[QStringLiteral("json")] = samplesJson(jsonSamples);
  description[QStringLiteral("typed")] = samplesJson(typedSamples);
  description[QStringLiteral("retained_snapshot_conversion_ms")] =
      QJsonObject{{QStringLiteral("json"), distribution(jsonConversions)},
                  {QStringLiteral("typed"), distribution(typedConversions)}};
  description[QStringLiteral("memory_before")] = before;
  description[QStringLiteral("memory_after")] = memory();
  description.remove(QStringLiteral("active_phase"));
  rows.append(description);
  reportProgress("case_complete", description);
} catch (const std::exception &error) {
  description[QStringLiteral("status")] = QStringLiteral("error");
  description[QStringLiteral("error")] = QString::fromUtf8(error.what());
  rows.append(description);
  reportProgress("case_error", description);
  throw;
}

void writeFile(const QString &path, const QByteArray &bytes) {
  require(QDir().mkpath(QFileInfo(path).absolutePath()),
          QStringLiteral("Cannot create fixture parent: ") + path);
  QFile file(path);
  require(file.open(QIODevice::WriteOnly | QIODevice::Truncate), file.errorString());
  require(file.write(bytes) == bytes.size(), file.errorString());
}

QJsonObject readObject(const QString &path) {
  QFile file(path);
  require(file.open(QIODevice::ReadOnly), file.errorString());
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(file.readAll(), &error);
  require(error.error == QJsonParseError::NoError && document.isObject(),
          QStringLiteral("Invalid fixture config: ") + path);
  return document.object();
}

QString tagName(int index) { return QStringLiteral("tag_%1").arg(index, 5, 10, QLatin1Char('0')); }

QJsonObject fixtureMetadata(bool populated, int index) {
  if (!populated) {
    return {};
  }
  return {{QStringLiteral("title"), QStringLiteral("Entry %1 \u4e2d\u6587").arg(index)},
          {QStringLiteral("priority"), index % 7},
          {QStringLiteral("custom"),
           QJsonObject{{QStringLiteral("enabled"), true},
                       {QStringLiteral("values"),
                        QJsonArray{index, QStringLiteral("value"), QJsonValue()}}}}};
}

QString stableId(const QString &key) {
  static const QUuid nameSpace(QStringLiteral("54fd50eb-2528-5327-b1cb-241d8dfe32ac"));
  return QUuid::createUuidV5(nameSpace, key.toUtf8()).toString(QUuid::WithoutBraces);
}

QJsonObject node(const QString &name, const QString &key, bool populated, int index) {
  return {{QStringLiteral("id"), stableId(key)},
          {QStringLiteral("name"), name},
          {QStringLiteral("createdUtc"), qint64(1700000000000LL + index)},
          {QStringLiteral("modifiedUtc"), qint64(1700000001000LL + index)},
          {QStringLiteral("metadata"), fixtureMetadata(populated, index)}};
}

QJsonArray fileTags(int index, int size, bool dense) {
  QJsonArray result;
  if (dense) {
    for (int tag = 0; tag < 16; ++tag) {
      result.append(tagName(tag));
    }
    result.append(tagName(16 + index % (size - 16)));
  } else {
    if (index % 10 == 0) {
      result.append(tagName(0));
    }
    if (index % 15 == 0) {
      result.append(tagName(1));
    }
    result.append(tagName(2 + index % (size - 2)));
  }
  return result;
}

struct Fixture {
  VxCoreContextHandle context = nullptr;
  QString path;
  QByteArray id;
  QStringList listingPaths;
  int size = 0;
  bool raw = false;
  bool populated = false;
  bool tree = false;
  bool dense = false;
  bool deepTags = false;

  ~Fixture() {
    if (!id.isEmpty()) {
      vxcore_notebook_close(context, id.constData());
    }
  }
  Fixture() = default;
  Fixture(const Fixture &) = delete;
  Fixture &operator=(const Fixture &) = delete;
};

// Bundled fixtures use the native-created notebook configuration and the documented on-disk
// vx.json format. Bulk generation avoids timing thousands of setup mutations. Raw fixtures are
// real filesystem trees indexed by the C API; metadata is seeded through native store writes.
void createFixture(Fixture &fixture, VxCoreContextHandle context, const QString &root, int sequence,
                   int size, bool raw, bool populated, bool tree, bool wideFolders, bool tagDomain,
                   bool dense, bool deepTags, int occurrences) {
  const QJsonObject setup{
      {QStringLiteral("sequence"), sequence},
      {QStringLiteral("notebook"), raw ? QStringLiteral("raw") : QStringLiteral("bundled")},
      {QStringLiteral("size"), size},
      {QStringLiteral("metadata"),
       populated ? QStringLiteral("populated") : QStringLiteral("empty")},
      {QStringLiteral("shape"), tree ? QStringLiteral("deep") : QStringLiteral("wide")}};
  reportProgress("fixture_begin", setup);
  fixture.context = context;
  fixture.path = root + QStringLiteral("/f%1").arg(sequence);
  fixture.size = size;
  fixture.raw = raw;
  fixture.populated = populated;
  fixture.tree = tree;
  fixture.dense = dense;
  fixture.deepTags = deepTags;
  require(QDir().mkpath(fixture.path), QStringLiteral("Cannot create fixture root"));
  const QByteArray path = fixture.path.toUtf8();
  auto created = acquire<JsonOwner>([&](char **out) {
    return vxcore_notebook_create(context, path.constData(),
                                  "{\"name\":\"Typed result benchmark\"}",
                                  raw ? VXCORE_NOTEBOOK_RAW : VXCORE_NOTEBOOK_BUNDLED, out);
  });
  fixture.id = created.get();
  if (!raw) {
    check(vxcore_notebook_close(context, fixture.id.constData()), "close fixture before seeding");
  }

  QByteArray line = QStringLiteral("\u03bc prefix ").toUtf8();
  for (int occurrence = 0; occurrence < occurrences; ++occurrence) {
    if (occurrence) {
      line += ' ';
    }
    line += "needle";
  }
  const QByteArray body = line + "\nquiet line\n" + line + "\n";
  QString directory = QStringLiteral(".");
  const int levels = tree ? 51 : 1;
  QStringList metadataPaths;
  for (int level = 0; level < levels; ++level) {
    fixture.listingPaths.append(directory);
    const QString relative =
        directory == QStringLiteral(".") ? QString() : directory + QLatin1Char('/');
    QJsonObject folder = node(level ? QStringLiteral("d") : QStringLiteral("."),
                              QStringLiteral("folder/") + directory, populated, level);
    QJsonArray files;
    const int fileCount = tree ? 1 : size;
    for (int index = 0; index < fileCount; ++index) {
      const QString name = QStringLiteral("note_%1.md").arg(index, 5, 10, QLatin1Char('0'));
      const QString relativeFile = relative + name;
      writeFile(fixture.path + QLatin1Char('/') + relativeFile, body);
      QJsonObject file = node(name, QStringLiteral("file/") + relativeFile, populated, index);
      file[QStringLiteral("tags")] = tagDomain ? fileTags(index, size, dense) : QJsonArray();
      // A valid attachment path is carried in populated metadata cases without requiring a
      // content read; existence is not part of the folder listing's attachment contract.
      if (populated && !raw) {
        file[QStringLiteral("attachments")] = QJsonArray{
            file.value(QStringLiteral("id")).toString() + QStringLiteral("/attachment.bin")};
      }
      files.append(file);
      metadataPaths.append(relativeFile);
    }
    QJsonArray folders;
    if (tree && level < 50) {
      folders.append(QStringLiteral("d"));
      require(QDir().mkpath(fixture.path + QLatin1Char('/') + relative + QStringLiteral("d")),
              QStringLiteral("Cannot create deep fixture folder"));
      metadataPaths.append(relative + QStringLiteral("d"));
    } else if (!tree && wideFolders) {
      for (int index = 0; index < size; ++index) {
        const QString name = QStringLiteral("child_%1").arg(index, 5, 10, QLatin1Char('0'));
        folders.append(name);
        require(QDir().mkpath(fixture.path + QLatin1Char('/') + name),
                QStringLiteral("Cannot create wide fixture folder"));
        metadataPaths.append(name);
        if (!raw) {
          auto child = node(name, QStringLiteral("folder/") + name, populated, index);
          child[QStringLiteral("files")] = QJsonArray();
          child[QStringLiteral("folders")] = QJsonArray();
          writeFile(fixture.path + QStringLiteral("/vx_notebook/contents/") + name +
                        QStringLiteral("/vx.json"),
                    compact(child));
        }
      }
    }
    folder[QStringLiteral("files")] = files;
    folder[QStringLiteral("folders")] = folders;
    if (!raw) {
      writeFile(fixture.path + QStringLiteral("/vx_notebook/contents/") + relative +
                    QStringLiteral("vx.json"),
                compact(folder));
    }
    directory = relative + QStringLiteral("d");
  }

  if (!raw) {
    const QString configPath = fixture.path + QStringLiteral("/vx_notebook/config.json");
    auto config = readObject(configPath);
    QJsonArray tags;
    if (tagDomain) {
      for (int index = 0; index < size; ++index) {
        tags.append(QJsonObject{
            {QStringLiteral("name"), tagName(index)},
            {QStringLiteral("parent"), deepTags && index % 50 ? tagName(index - 1) : QString()},
            {QStringLiteral("metadata"), fixtureMetadata(populated, index)}});
      }
    }
    config[QStringLiteral("tags")] = tags;
    writeFile(configPath, compact(config));
    auto opened = acquire<JsonOwner>(
        [&](char **out) { return vxcore_notebook_open(context, path.constData(), out); });
    fixture.id = opened.get();
    check(vxcore_notebook_rebuild_cache(context, fixture.id.constData()),
          "rebuild bundled fixture index");
  }
  // Force initial indexing outside all measurement spans (also verifies the native fixture).
  for (const auto &folder : fixture.listingPaths) {
    const auto bytes = folder.toUtf8();
    acquire<FolderOwner>([&](VxCoreFolderListing **out) {
      return vxcore_folder_list_children_typed(context, fixture.id.constData(), bytes.constData(),
                                               out);
    });
  }
  if (raw && populated) {
    // The public per-node setter rescans the directory before every write. Seed
    // already-indexed fixture records through the same store operations in one
    // transaction, avoiding quadratic setup without changing production APIs.
    auto *native = reinterpret_cast<vxcore::VxCoreContext *>(context);
    auto *notebook = native->notebook_manager->GetNotebook(fixture.id.constData());
    auto *store = notebook ? notebook->GetMetadataStore() : nullptr;
    require(store && store->IsOpen(), QStringLiteral("Raw fixture store is unavailable"));
    require(store->BeginTransaction(), QStringLiteral("Cannot begin raw fixture transaction"));
    QHash<QString, QJsonValue> expected;
    try {
      int index = 0;
      for (const auto &entry : metadataPaths) {
        const auto value = fixtureMetadata(true, index++);
        const auto encoded = compact(value).toStdString();
        const auto path = entry.toUtf8().toStdString();
        bool updated = false;
        if (const auto file = store->GetFileByPath(path)) {
          updated = store->UpdateFile(file->id, file->name, file->modified_utc, encoded);
        } else if (const auto folder = store->GetFolderByPath(path)) {
          updated = store->UpdateFolder(folder->id, folder->name, folder->modified_utc, encoded);
        }
        require(updated, QStringLiteral("Cannot seed indexed raw fixture metadata"));
        expected.insert(entry, value);
      }
      require(store->CommitTransaction(), QStringLiteral("Cannot commit raw fixture metadata"));
    } catch (...) {
      store->RollbackTransaction();
      throw;
    }
    // Observe the seeded values through the actual public read path before timing.
    qsizetype verified = 0;
    for (const auto &folder : fixture.listingPaths) {
      const auto bytes = folder.toUtf8();
      auto owner = acquire<FolderOwner>([&](VxCoreFolderListing **out) {
        return vxcore_folder_list_children_typed(context, fixture.id.constData(), bytes.constData(),
                                                 out);
      });
      const auto view = vxcore_folder_listing_view(owner.get());
      const QString prefix = folder == QStringLiteral(".") ? QString() : folder + QLatin1Char('/');
      const auto verify = [&](const VxCoreFolderEntryView *entries, size_t count) {
        for (size_t i = 0; i < count; ++i) {
          const auto &entry = entries[i];
          const auto path =
              prefix + QString::fromUtf8(entry.name.data, static_cast<qsizetype>(entry.name.size));
          const auto found = expected.constFind(path);
          require(found != expected.cend(), QStringLiteral("Unexpected raw fixture entry"));
          const auto metadata = QJsonValue::fromJson(QByteArrayView(
              entry.metadata_json.data, static_cast<qsizetype>(entry.metadata_json.size)));
          require(metadata == found.value(),
                  QStringLiteral("Raw fixture metadata was not observed by the public API"));
          ++verified;
        }
      };
      verify(view->files, view->file_count);
      verify(view->folders, view->folder_count);
    }
    require(verified == expected.size(),
            QStringLiteral("Incomplete raw fixture metadata validation"));
  }
  reportProgress("fixture_ready", setup);
}

QJsonObject describe(const Fixture &fixture, const QString &operation) {
  return {
      {QStringLiteral("operation"), operation},
      {QStringLiteral("notebook"), fixture.raw ? QStringLiteral("raw") : QStringLiteral("bundled")},
      {QStringLiteral("size"), fixture.size},
      {QStringLiteral("metadata"),
       fixture.populated ? QStringLiteral("populated") : QStringLiteral("empty")}};
}

void folderCases(QJsonArray &rows, const Options &options, Fixture &fixture) {
  auto description = describe(fixture, fixture.tree ? QStringLiteral("folder/expand_50")
                                                    : QStringLiteral("folder/list"));
  description[QStringLiteral("listing_calls_per_sample")] = fixture.listingPaths.size();
  description[QStringLiteral("fixture_files")] = fixture.tree ? 51 : fixture.size;
  description[QStringLiteral("fixture_folders")] = fixture.tree ? 50 : fixture.size;
  pairedCase(
      rows, description, options, fixture.tree ? 101 : fixture.size * 2,
      [&]() {
        JsonResults results;
        results.reserve(static_cast<size_t>(fixture.listingPaths.size()));
        for (const auto &folder : fixture.listingPaths) {
          const auto bytes = folder.toUtf8();
          results.push_back(acquire<JsonOwner>([&](char **out) {
            return vxcore_folder_list_children(fixture.context, fixture.id.constData(),
                                               bytes.constData(), out);
          }));
        }
        return results;
      },
      [&]() {
        FolderOwners results;
        results.reserve(static_cast<size_t>(fixture.listingPaths.size()));
        for (const auto &folder : fixture.listingPaths) {
          const auto bytes = folder.toUtf8();
          results.push_back(acquire<FolderOwner>([&](VxCoreFolderListing **out) {
            return vxcore_folder_list_children_typed(fixture.context, fixture.id.constData(),
                                                     bytes.constData(), out);
          }));
        }
        return results;
      },
      foldersFromJson,
      [](const FolderOwners &owners) {
        QVector<FolderListing> result;
        result.reserve(static_cast<qsizetype>(owners.size()));
        for (const auto &owner : owners) {
          result.append(
              NotebookCoreService::folderListingFromView(*vxcore_folder_listing_view(owner.get())));
        }
        return result;
      });
}

QByteArray searchQuery(bool content, bool bounded) {
  return compact(QJsonObject{
      {QStringLiteral("pattern"), content ? QStringLiteral("needle") : QStringLiteral("note_")},
      {QStringLiteral("includeFiles"), true},
      {QStringLiteral("includeFolders"), false},
      {QStringLiteral("maxResults"),
       bounded ? (content ? 17 : 37) : (content ? 0 : std::numeric_limits<int>::max())},
      {QStringLiteral("scope"), QJsonObject{{QStringLiteral("folderPath"), QStringLiteral(".")},
                                            {QStringLiteral("recursive"), true}}}});
}

struct StreamState {
  std::mutex mutex;
  std::map<int, SearchContentResult> batches;
  Time begin;
  Time first;
  Time cancelled;
  double conversion = 0;
  quint64 bytes = 0;
  int totalBatches = -1;
  int callbacks = 0;
  volatile int cancelFlag = 0;
  bool requestCancel = false;
  std::exception_ptr error;
};

template <typename Convert, typename Size>
void acceptBatch(StreamState &state, int index, int total, Convert convert, Size size) noexcept {
  try {
    const auto arrived = Clock::now();
    auto value = convert(); // All borrowed UTF-8/JSON has become owning Qt data before return.
    const auto converted = Clock::now();
    const auto bytes = size();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.callbacks || arrived < state.first) {
      state.first = arrived;
    }
    require(index >= 0 && index < total, QStringLiteral("Invalid streaming batch index"));
    require(state.totalBatches == -1 || state.totalBatches == total,
            QStringLiteral("Inconsistent streaming batch total"));
    require(!value.m_truncated, QStringLiteral("Native streaming batch unexpectedly capped"));
    require(index == 0 || value.m_encryptedSkippedCount == 0,
            QStringLiteral("Repeated encrypted skip count"));
    state.totalBatches = total;
    require(state.batches.emplace(index, std::move(value)).second,
            QStringLiteral("Duplicate streaming batch"));
    ++state.callbacks;
    state.conversion += milliseconds(arrived, converted);
    state.bytes += bytes;
    if (state.requestCancel && !state.cancelFlag) {
      state.cancelled = Clock::now();
      state.cancelFlag = 1;
    }
  } catch (...) {
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.error) {
      state.error = std::current_exception();
    }
    state.cancelFlag = 1;
  }
}

void jsonBatch(int index, int total, const char *json, void *userdata) noexcept {
  auto &state = *static_cast<StreamState *>(userdata);
  acceptBatch(
      state, index, total, [&]() { return contentFromJson(json); },
      [&]() { return std::strlen(json); });
}
void typedBatch(int index, int total, const VxCoreContentSearchView *view,
                void *userdata) noexcept {
  auto &state = *static_cast<StreamState *>(userdata);
  acceptBatch(
      state, index, total,
      [&]() {
        require(view != nullptr, QStringLiteral("Null streaming view"));
        return SearchContentResult::fromView(*view);
      },
      [&]() { return payload(*view).bytes; });
}

struct StreamRun {
  SearchContentResult result;
  double completion = 0;
  double firstBatch = 0;
  double conversion = 0;
  double merge = 0;
  double cancelCompletion = -1;
  quint64 bytes = 0;
  int callbacks = 0;
};

StreamRun streamRun(Fixture &fixture, const QByteArray &query, const Options &options, bool typed,
                    bool cancel) {
  StreamState state;
  state.requestCancel = cancel;
  state.begin = Clock::now();
  const auto error =
      typed ? vxcore_search_content_streaming_typed(fixture.context, fixture.id.constData(),
                                                    query.constData(), nullptr, options.batchSize,
                                                    typedBatch, &state, &state.cancelFlag)
            : vxcore_search_content_streaming(fixture.context, fixture.id.constData(),
                                              query.constData(), nullptr, options.batchSize,
                                              jsonBatch, &state, &state.cancelFlag);
  const auto returned = Clock::now();
  // The blocking C contract guarantees callback completion; no detached worker or borrowed
  // batch can outlive state. Synchronization in acceptBatch also supports concurrent callbacks.
  if (state.error) {
    std::rethrow_exception(state.error);
  }
  require(error == (cancel ? VXCORE_ERR_CANCELLED : VXCORE_OK),
          QStringLiteral("%1 streaming completion: %2 (%3)")
              .arg(typed ? QStringLiteral("typed") : QStringLiteral("JSON"),
                   QString::fromUtf8(vxcore_error_message(error)))
              .arg(error));
  require(state.callbacks > 0, QStringLiteral("Populated streaming fixture emitted no callback"));
  if (!cancel) {
    require(state.callbacks == state.totalBatches,
            QStringLiteral("Streaming omitted completed chunks"));
  }
  StreamRun output;
  output.completion = milliseconds(state.begin, returned);
  output.firstBatch = milliseconds(state.begin, state.first);
  output.conversion = state.conversion;
  output.bytes = state.bytes;
  output.callbacks = state.callbacks;
  if (cancel) {
    output.cancelCompletion = milliseconds(state.cancelled, returned);
  }
  const auto mergeBegin = Clock::now();
  qsizetype fileCount = 0;
  for (const auto &entry : state.batches) {
    fileCount += entry.second.m_files.size();
  }
  output.result.m_files.reserve(fileCount);
  for (auto &entry : state.batches) {
    output.result.m_encryptedSkippedCount += entry.second.m_encryptedSkippedCount;
    for (auto &file : entry.second.m_files) {
      output.result.m_files.append(std::move(file));
    }
  }
  output.merge = milliseconds(mergeBegin, Clock::now());
  return output;
}

void verifyPartial(const SearchContentResult &partial, const SearchContentResult &full) {
  require(!partial.m_truncated && partial.m_encryptedSkippedCount == full.m_encryptedSkippedCount,
          QStringLiteral("Cancellation changed fixture result flags"));
  std::map<QString, const SearchContentFile *> files;
  for (const auto &file : full.m_files) {
    files.emplace(file.m_path, &file);
  }
  for (const auto &file : partial.m_files) {
    const auto found = files.find(file.m_path);
    require(found != files.end() && found->second->m_id == file.m_id,
            QStringLiteral("Cancellation returned an unknown file"));
    require(file.m_matches.size() <= found->second->m_matches.size(),
            QStringLiteral("Cancellation invented occurrences"));
    for (qsizetype i = 0; i < file.m_matches.size(); ++i) {
      const auto &actual = file.m_matches[i];
      const auto &expected = found->second->m_matches[i];
      require(actual.m_lineNumber == expected.m_lineNumber &&
                  actual.m_columnStart == expected.m_columnStart &&
                  actual.m_columnEnd == expected.m_columnEnd &&
                  actual.m_lineText == expected.m_lineText,
              QStringLiteral("Cancellation returned a different occurrence"));
    }
    files.erase(found); // Reject duplicate files as well as unknown ones.
  }
}

QJsonObject streamStats(const std::vector<StreamRun> &runs, bool cancel) {
  std::vector<double> completion, total, first, conversion, merge, cancellation, counts,
      occurrences, bytes, callbacks;
  for (const auto &run : runs) {
    completion.push_back(run.completion);
    total.push_back(run.completion + run.merge);
    first.push_back(run.firstBatch);
    conversion.push_back(run.conversion);
    merge.push_back(run.merge);
    if (cancel) {
      cancellation.push_back(run.cancelCompletion);
    }
    counts.push_back(static_cast<double>(run.result.m_files.size()));
    qint64 matches = 0;
    for (const auto &file : run.result.m_files) {
      matches += file.m_matches.size();
    }
    occurrences.push_back(static_cast<double>(matches));
    bytes.push_back(static_cast<double>(run.bytes));
    callbacks.push_back(run.callbacks);
  }
  return {{QStringLiteral("api_completion_ms"), distribution(completion)},
          {QStringLiteral("completion_plus_merge_ms"), distribution(total)},
          {QStringLiteral("first_batch_ms"), distribution(first)},
          {QStringLiteral("callback_conversion_sum_wall_ms"), distribution(conversion)},
          {QStringLiteral("ordered_merge_ms"), distribution(merge)},
          {QStringLiteral("cancel_request_to_return_ms"),
           cancel ? QJsonValue(distribution(cancellation)) : QJsonValue()},
          {QStringLiteral("result_count"), distribution(counts)},
          {QStringLiteral("occurrence_count"), distribution(occurrences)},
          {QStringLiteral("payload_bytes"), distribution(bytes)},
          {QStringLiteral("callbacks"), distribution(callbacks)}};
}

void streamingCase(QJsonArray &rows, const Options &options, Fixture &fixture,
                   const QString &backend, bool cancel) try {
  auto description = describe(fixture, cancel ? QStringLiteral("search/stream_cancel")
                                              : QStringLiteral("search/stream_full"));
  if (!description.value(QStringLiteral("operation")).toString().contains(options.filter)) {
    return;
  }
  reportProgress("case_begin", description);
  const auto before = memory();
  const auto query = searchQuery(true, false);
  auto fullJson = acquireJson(
      [&](char **out) {
        return vxcore_search_content(fixture.context, fixture.id.constData(), query.constData(),
                                     nullptr, out);
      },
      "JSON blob reference acquisition before streaming");
  const auto full = contentFromJson(fullJson.front().get());
  require(full.m_files.size() == fixture.size,
          QStringLiteral("Streaming reference lost fixture files"));
  const int emittedPerFile = backend == QStringLiteral("rg") ? 2 : options.occurrences * 2;
  require(occurrenceCount(full) == qint64(fixture.size) * emittedPerFile,
          QStringLiteral("Streaming reference occurrence count differs from backend semantics"));
  auto firstJson = streamRun(fixture, query, options, false, cancel);
  auto firstTyped = streamRun(fixture, query, options, true, cancel);
  if (cancel) {
    verifyPartial(firstJson.result, full);
    verifyPartial(firstTyped.result, full);
  } else {
    require(normalized(firstJson.result) == normalized(full) &&
                normalized(firstTyped.result) == normalized(full),
            QStringLiteral("Streaming JSON/typed/blob values differ"));
  }
  // Release measured Qt payloads after extracting their metrics, rather than retaining 30 full
  // searches and attributing benchmark bookkeeping to production peak memory.
  struct StreamMetric {
    StreamRun run;
    qint64 count = 0;
    qint64 occurrences = 0;
  };
  std::vector<StreamMetric> jsonMetrics, typedMetrics;
  const auto record = [&](bool typed, bool measured) {
    auto run = streamRun(fixture, query, options, typed, cancel);
    if (cancel) {
      verifyPartial(run.result, full);
    } else {
      require(normalized(run.result) == normalized(full),
              QStringLiteral("Streaming values changed during measurement"));
    }
    if (!measured) {
      return;
    }
    StreamMetric metric;
    metric.count = run.result.m_files.size();
    for (const auto &file : run.result.m_files) {
      metric.occurrences += file.m_matches.size();
    }
    run.result = {};
    metric.run = std::move(run);
    (typed ? typedMetrics : jsonMetrics).push_back(std::move(metric));
  };
  for (int i = -options.warmups; i < options.pairs; ++i) {
    if (i % 2) {
      record(true, i >= 0);
      record(false, i >= 0);
    } else {
      record(false, i >= 0);
      record(true, i >= 0);
    }
  }
  const auto summarize = [&](const std::vector<StreamMetric> &metrics) {
    std::vector<StreamRun> times;
    std::vector<double> counts, occurrences;
    times.reserve(metrics.size());
    for (const auto &metric : metrics) {
      times.push_back(metric.run);
      counts.push_back(static_cast<double>(metric.count));
      occurrences.push_back(static_cast<double>(metric.occurrences));
    }
    auto result = streamStats(times, cancel);
    result[QStringLiteral("result_count")] = distribution(counts);
    result[QStringLiteral("occurrence_count")] = distribution(occurrences);
    return result;
  };
  description[QStringLiteral("status")] = QStringLiteral("ok");
  description[QStringLiteral("backend")] = backend;
  description[QStringLiteral("batch_size")] = options.batchSize;
  description[QStringLiteral("text_occurrences_per_file")] = options.occurrences * 2;
  description[QStringLiteral("expected_result_occurrences_per_file")] = emittedPerFile;
  description[QStringLiteral("full_reference_sha256")] = checksum(normalized(full));
  description[QStringLiteral("cancel_policy")] =
      cancel ? QStringLiteral("set the public cancel flag after retaining the first callback; rg "
                              "may deliver its single batch only after its whole process finishes")
             : QStringLiteral("none; streaming primitive is uncapped");
  description[QStringLiteral("measurement_scope")] =
      QStringLiteral("blocking API includes callbacks; callback conversion is summed wall "
                     "slices, not CPU time and not additive to completion; merge is separate");
  description[QStringLiteral("first_observed_pair")] = QJsonObject{
      {QStringLiteral("json"), streamStats(std::vector<StreamRun>{firstJson}, cancel)},
      {QStringLiteral("typed"), streamStats(std::vector<StreamRun>{firstTyped}, cancel)}};
  description[QStringLiteral("json")] = summarize(jsonMetrics);
  description[QStringLiteral("typed")] = summarize(typedMetrics);
  description[QStringLiteral("memory_before")] = before;
  description[QStringLiteral("memory_after")] = memory();
  rows.append(description);
  reportProgress("case_complete", description);
} catch (const std::exception &error) {
  auto description = describe(fixture, cancel ? QStringLiteral("search/stream_cancel")
                                              : QStringLiteral("search/stream_full"));
  description[QStringLiteral("backend")] = backend;
  description[QStringLiteral("status")] = QStringLiteral("error");
  description[QStringLiteral("error")] = QString::fromUtf8(error.what());
  rows.append(description);
  reportProgress("case_error", description);
  throw;
}

void searchCases(QJsonArray &rows, const Options &options, Fixture &fixture,
                 const QString &backend) {
  const auto config = compact(QJsonObject{
      {QStringLiteral("search"), QJsonObject{{QStringLiteral("backends"), QJsonArray{backend}}}}});
  check(vxcore_context_update_config(fixture.context, config.constData()), "select search backend");
  const QString notebookId = QString::fromUtf8(fixture.id);
  // rg currently reports just the first submatch on each matching line. Keep that existing
  // behavior visible rather than claiming that its response contains every textual occurrence.
  const int emittedPerFile = backend == QStringLiteral("rg") ? 2 : options.occurrences * 2;
  for (bool bounded : {true, false}) {
    const auto query = searchQuery(false, bounded);
    auto description = describe(fixture, bounded ? QStringLiteral("search/files_bounded")
                                                 : QStringLiteral("search/files_uncapped"));
    description[QStringLiteral("backend")] = backend;
    description[QStringLiteral("max_results")] = bounded ? 37 : std::numeric_limits<int>::max();
    pairedCase(
        rows, description, options, bounded ? std::min(fixture.size, 37) : fixture.size,
        [&]() {
          return acquireJson([&](char **out) {
            return vxcore_search_files(fixture.context, fixture.id.constData(), query.constData(),
                                       nullptr, out);
          });
        },
        [&]() {
          return acquire<FileOwner>([&](VxCoreFileSearchResult **out) {
            return vxcore_search_files_typed(fixture.context, fixture.id.constData(),
                                             query.constData(), nullptr, out);
          });
        },
        [&](const JsonResults &json) { return filesFromJson(json, notebookId); },
        [&](const FileOwner &owner) {
          return SearchResult::fromFileSearchView(*vxcore_file_search_result_view(owner.get()),
                                                  notebookId);
        });

    const auto contentQuery = searchQuery(true, bounded);
    const auto expected =
        bounded ? std::min(fixture.size, (17 + emittedPerFile - 1) / emittedPerFile) : fixture.size;
    const qint64 expectedOccurrences =
        bounded ? std::min<qint64>(17, qint64(fixture.size) * emittedPerFile)
                : qint64(fixture.size) * emittedPerFile;
    for (bool cancellable : {false, true}) {
      auto contentDescription = describe(
          fixture, QStringLiteral("search/content%1_%2")
                       .arg(cancellable ? QStringLiteral("_ex") : QString(),
                            bounded ? QStringLiteral("bounded") : QStringLiteral("uncapped")));
      contentDescription[QStringLiteral("backend")] = backend;
      contentDescription[QStringLiteral("max_results")] = bounded ? 17 : 0;
      contentDescription[QStringLiteral("text_occurrences_per_file")] = options.occurrences * 2;
      contentDescription[QStringLiteral("expected_result_occurrences_per_file")] = emittedPerFile;
      pairedCase(
          rows, contentDescription, options, expected,
          [&]() {
            return acquireJson([&](char **out) {
              volatile int cancel = 0;
              return cancellable
                         ? vxcore_search_content_ex(fixture.context, fixture.id.constData(),
                                                    contentQuery.constData(), nullptr, &cancel, out)
                         : vxcore_search_content(fixture.context, fixture.id.constData(),
                                                 contentQuery.constData(), nullptr, out);
            });
          },
          [&]() {
            return acquire<ContentOwner>([&](VxCoreContentSearchResult **out) {
              volatile int cancel = 0;
              return cancellable
                         ? vxcore_search_content_ex_typed(fixture.context, fixture.id.constData(),
                                                          contentQuery.constData(), nullptr,
                                                          &cancel, out)
                         : vxcore_search_content_typed(fixture.context, fixture.id.constData(),
                                                       contentQuery.constData(), nullptr, out);
            });
          },
          [](const JsonResults &json) { return contentFromJson(json.front().get()); },
          [](const ContentOwner &owner) {
            return SearchContentResult::fromView(*vxcore_content_search_result_view(owner.get()));
          },
          expectedOccurrences);
    }
    auto groupedDescription =
        describe(fixture, bounded ? QStringLiteral("search/content_grouped_bounded")
                                  : QStringLiteral("search/content_grouped_uncapped"));
    groupedDescription[QStringLiteral("backend")] = backend;
    groupedDescription[QStringLiteral("max_results")] = bounded ? 17 : 0;
    groupedDescription[QStringLiteral("text_occurrences_per_file")] = options.occurrences * 2;
    groupedDescription[QStringLiteral("expected_result_occurrences_per_file")] = emittedPerFile;
    pairedCase(
        rows, groupedDescription, options, expected,
        [&]() {
          return acquireJson([&](char **out) {
            return vxcore_search_content(fixture.context, fixture.id.constData(),
                                         contentQuery.constData(), nullptr, out);
          });
        },
        [&]() {
          return acquire<ContentOwner>([&](VxCoreContentSearchResult **out) {
            return vxcore_search_content_typed(fixture.context, fixture.id.constData(),
                                               contentQuery.constData(), nullptr, out);
          });
        },
        [&](const JsonResults &json) {
          return SearchResult::fromContentSearchResult(contentFromJson(json.front().get()),
                                                       notebookId);
        },
        [&](const ContentOwner &owner) {
          return SearchResult::fromContentSearchResult(
              SearchContentResult::fromView(*vxcore_content_search_result_view(owner.get())),
              notebookId);
        },
        expectedOccurrences);
  }
  streamingCase(rows, options, fixture, backend, false);
  streamingCase(rows, options, fixture, backend, true);
}

void tagCases(QJsonArray &rows, const Options &options, Fixture &fixture) {
  const QString notebookId = QString::fromUtf8(fixture.id);
  const auto description = [&](const QString &operation) {
    auto result = describe(fixture, operation);
    result[QStringLiteral("hierarchy")] =
        fixture.deepTags ? QStringLiteral("chains_of_50") : QStringLiteral("wide");
    result[QStringLiteral("associations")] = fixture.dense
                                                 ? QStringLiteral("dense_16_shared_plus_one")
                                                 : QStringLiteral("sparse_one_plus_periodic");
    return result;
  };
  pairedCase(
      rows, description(QStringLiteral("tags/list")), options, fixture.size,
      [&]() {
        return acquireJson([&](char **out) {
          return vxcore_tag_list(fixture.context, fixture.id.constData(), out);
        });
      },
      [&]() {
        return acquire<TagsOwner>([&](VxCoreTagList **out) {
          return vxcore_tag_list_typed(fixture.context, fixture.id.constData(), out);
        });
      },
      tagsFromJson,
      [](const TagsOwner &owner) {
        return TagCoreService::tagsFromView(*vxcore_tag_list_view(owner.get()));
      });
  pairedCase(
      rows, description(QStringLiteral("tags/count_direct")), options, fixture.size,
      [&]() {
        return acquireJson([&](char **out) {
          return vxcore_tag_count_files_by_tag(fixture.context, fixture.id.constData(), out);
        });
      },
      [&]() {
        return acquire<CountsOwner>([&](VxCoreTagCounts **out) {
          return vxcore_tag_count_files_by_tag_typed(fixture.context, fixture.id.constData(), out);
        });
      },
      countsFromJson,
      [](const CountsOwner &owner) {
        return TagCoreService::tagCountsFromView(*vxcore_tag_counts_view(owner.get()));
      });

  const QStringList tags{tagName(0), tagName(1)};
  const auto tagsJson =
      QJsonDocument(QJsonArray::fromStringList(tags)).toJson(QJsonDocument::Compact);
  for (const QByteArray op : {QByteArray("AND"), QByteArray("OR")}) {
    const auto ceilDivide = [&](int divisor) { return (fixture.size + divisor - 1) / divisor; };
    const int expected =
        fixture.dense
            ? fixture.size
            : (op == "AND" ? ceilDivide(30) : ceilDivide(10) + ceilDivide(15) - ceilDivide(30));
    pairedCase(
        rows, description(QStringLiteral("tags/find_") + QString::fromLatin1(op)), options,
        expected,
        [&]() {
          return acquireJson([&](char **out) {
            return vxcore_tag_find_files(fixture.context, fixture.id.constData(),
                                         tagsJson.constData(), op.constData(), out);
          });
        },
        [&]() {
          return acquire<TagFilesOwner>([&](VxCoreTagFileResult **out) {
            return vxcore_tag_find_files_typed(fixture.context, fixture.id.constData(),
                                               tagsJson.constData(), op.constData(), out);
          });
        },
        tagFilesFromJson,
        [](const TagFilesOwner &owner) {
          return TagCoreService::tagFilesFromView(*vxcore_tag_file_result_view(owner.get()));
        });
    const auto query =
        compact(QJsonObject{{QStringLiteral("tags"), QJsonArray::fromStringList(tags)},
                            {QStringLiteral("operator"), QString::fromLatin1(op)},
                            {QStringLiteral("maxResults"), std::numeric_limits<int>::max()}});
    pairedCase(
        rows, description(QStringLiteral("tags/search_") + QString::fromLatin1(op)), options,
        expected,
        [&]() {
          return acquireJson([&](char **out) {
            return vxcore_search_by_tags(fixture.context, fixture.id.constData(), query.constData(),
                                         nullptr, out);
          });
        },
        [&]() {
          return acquire<FileOwner>([&](VxCoreFileSearchResult **out) {
            return vxcore_search_by_tags_typed(fixture.context, fixture.id.constData(),
                                               query.constData(), nullptr, out);
          });
        },
        [&](const JsonResults &json) { return filesFromJson(json, notebookId); },
        [&](const FileOwner &owner) {
          return SearchResult::fromFileSearchView(*vxcore_file_search_result_view(owner.get()),
                                                  notebookId);
        });
  }
}

class IsolatedRuntime {
public:
  explicit IsolatedRuntime(const Options &options)
      : directory((options.tempParent.isEmpty() ? QDir::tempPath() : options.tempParent) +
                  QStringLiteral("/vxt-XXXXXX")) {
    require(directory.isValid(), QStringLiteral("Cannot create isolated temporary root"));
    directory.setAutoRemove(!options.keepFixtures);
    try {
      for (const QByteArray key : {QByteArray("TEMP"), QByteArray("TMP"), QByteArray("TMPDIR")}) {
        environment.emplace_back(key, qEnvironmentVariableIsSet(key.constData()),
                                 qgetenv(key.constData()));
        setEnvironment(key, directory.path().toUtf8());
      }
      // Isolate inherited user rg configuration and make bounded result membership stable.
      // Both APIs see this same explicitly reported configuration; native code is untouched.
      const QByteArray rgKey("RIPGREP_CONFIG_PATH");
      environment.emplace_back(rgKey, qEnvironmentVariableIsSet(rgKey.constData()),
                               qgetenv(rgKey.constData()));
      const QString rgConfig = directory.path() + QStringLiteral("/rg.conf");
      writeFile(rgConfig, "--sort=path\n");
      setEnvironment(rgKey, rgConfig.toUtf8());
      // std::filesystem::temp_directory_path() must already resolve below our private root.
      // Never clear the shared test directory; another process may be using it.
      vxcore_set_test_mode(1);
      vxcore_set_app_info("VNote", "TypedResultsBenchmark");
      check(vxcore_log_set_level(VXCORE_LOG_LEVEL_WARN), "configure native logging");
      check(vxcore_context_create(
                "{\"recoverLastSession\":false,\"search\":{\"backends\":[\"simple\"]}}", &context),
            "create isolated context");
      for (const auto location : {VXCORE_DATA_APP, VXCORE_DATA_LOCAL}) {
        const auto path = acquire<JsonOwner>(
            [&](char **out) { return vxcore_context_get_data_path(context, location, out); });
        const auto relative =
            QDir(directory.path()).relativeFilePath(QString::fromUtf8(path.get()));
        require(!QDir::isAbsolutePath(relative) && relative != QStringLiteral("..") &&
                    !relative.startsWith(QStringLiteral("../")),
                QStringLiteral("Native profile escaped isolated temporary root"));
      }
    } catch (...) {
      vxcore_context_destroy(context);
      context = nullptr;
      restoreEnvironment();
      throw;
    }
  }
  ~IsolatedRuntime() {
    vxcore_context_destroy(context);
    restoreEnvironment();
  }
  IsolatedRuntime(const IsolatedRuntime &) = delete;
  IsolatedRuntime &operator=(const IsolatedRuntime &) = delete;

  QTemporaryDir directory;
  VxCoreContextHandle context = nullptr;

private:
  struct Variable {
    QByteArray key;
    bool present;
    QByteArray value;
    Variable(QByteArray p_key, bool p_present, QByteArray p_value)
        : key(std::move(p_key)), present(p_present), value(std::move(p_value)) {}
  };
  std::vector<Variable> environment;

  static void setEnvironment(const QByteArray &key, const QByteArray &value) {
    require(qputenv(key.constData(), value),
            QStringLiteral("Cannot isolate temporary environment"));
#ifdef Q_OS_WIN
    const auto wideKey = QString::fromLatin1(key).toStdWString();
    const auto wideValue = QString::fromUtf8(value).toStdWString();
    require(SetEnvironmentVariableW(wideKey.c_str(), wideValue.c_str()) != 0,
            QStringLiteral("Cannot isolate Win32 temporary environment"));
#endif
  }
  void restoreEnvironment() noexcept {
    for (const auto &variable : environment) {
      if (variable.present) {
        qputenv(variable.key.constData(), variable.value);
      } else {
        qunsetenv(variable.key.constData());
      }
#ifdef Q_OS_WIN
      const auto key = QString::fromLatin1(variable.key).toStdWString();
      const auto value = QString::fromUtf8(variable.value).toStdWString();
      SetEnvironmentVariableW(key.c_str(), variable.present ? value.c_str() : nullptr);
#endif
    }
    environment.clear();
  }
};

bool rgAvailable(QString &version) {
  QProcess process;
  process.start(QStringLiteral("rg"), {QStringLiteral("--version")});
  if (!process.waitForStarted(10000)) {
    return false;
  }
  if (!process.waitForFinished(10000)) {
    process.kill();
    process.waitForFinished();
    return false;
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return false;
  }
  version =
      QString::fromUtf8(process.readAllStandardOutput()).section(QLatin1Char('\n'), 0, 0).trimmed();
  return true;
}

Options arguments(QCoreApplication &application) {
  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Paired native JSON/typed result and owning Qt conversion benchmark. JSON "
                     "report on stdout; no CTest registration."));
  parser.addHelpOption();
  parser.addOption(
      {QStringLiteral("matrix"),
       QStringLiteral(
           "Select the complete 100/1000/10000 matrix; explicit dimension flags override it.")});
  for (const auto &name : {"domains", "sizes", "notebooks", "metadata", "shapes", "hierarchies",
                           "densities", "backends"}) {
    parser.addOption({QString::fromLatin1(name),
                      QStringLiteral("Comma-separated %1 selection; see report dimensions.")
                          .arg(QString::fromLatin1(name)),
                      QStringLiteral("values")});
  }
  parser.addOption({QStringLiteral("pairs"),
                    QStringLiteral("Measured interleaved pairs (default 30)."), QStringLiteral("n"),
                    QStringLiteral("30")});
  parser.addOption({QStringLiteral("warmups"), QStringLiteral("Warm-up pairs (default 3)."),
                    QStringLiteral("n"), QStringLiteral("3")});
  parser.addOption({QStringLiteral("occurrences"),
                    QStringLiteral("Occurrences on each of two matched lines (default 4)."),
                    QStringLiteral("n"), QStringLiteral("4")});
  parser.addOption({QStringLiteral("batch-size"),
                    QStringLiteral("Streaming file batch size (default 32)."), QStringLiteral("n"),
                    QStringLiteral("32")});
  parser.addOption({QStringLiteral("case"),
                    QStringLiteral("Only operation names containing this substring."),
                    QStringLiteral("substring")});
  parser.addOption({QStringLiteral("temp-parent"),
                    QStringLiteral("Existing writable parent for the unique isolated root; "
                                   "useful for short Windows paths."),
                    QStringLiteral("directory")});
  parser.addOption(
      {QStringLiteral("keep-fixtures"),
       QStringLiteral("Keep this invocation's isolated root for external inspection/profiling.")});
  parser.process(application);
  const bool matrix = parser.isSet(QStringLiteral("matrix"));
  const auto selection = [&](const QString &name, const QString &normal, const QString &full,
                             const QStringList &allowed) {
    const auto values = (parser.isSet(name) ? parser.value(name) : (matrix ? full : normal))
                            .split(QLatin1Char(','), Qt::SkipEmptyParts);
    require(!values.isEmpty(), QStringLiteral("Empty selection: ") + name);
    for (const auto &value : values) {
      require(allowed.contains(value), QStringLiteral("Invalid %1 value: %2; allowed: %3")
                                           .arg(name, value, allowed.join(QLatin1Char(','))));
    }
    QStringList result = values;
    result.removeDuplicates();
    return result;
  };
  Options options;
  options.domains =
      selection(QStringLiteral("domains"), QStringLiteral("folder,search,tags"),
                QStringLiteral("folder,search,tags"),
                {QStringLiteral("folder"), QStringLiteral("search"), QStringLiteral("tags")});
  options.notebooks =
      selection(QStringLiteral("notebooks"), QStringLiteral("bundled"),
                QStringLiteral("bundled,raw"), {QStringLiteral("bundled"), QStringLiteral("raw")});
  options.metadata = selection(QStringLiteral("metadata"), QStringLiteral("empty"),
                               QStringLiteral("empty,populated"),
                               {QStringLiteral("empty"), QStringLiteral("populated")});
  options.shapes =
      selection(QStringLiteral("shapes"), QStringLiteral("wide,deep"), QStringLiteral("wide,deep"),
                {QStringLiteral("wide"), QStringLiteral("deep")});
  options.hierarchies =
      selection(QStringLiteral("hierarchies"), QStringLiteral("wide"), QStringLiteral("wide,deep"),
                {QStringLiteral("wide"), QStringLiteral("deep")});
  options.densities = selection(QStringLiteral("densities"), QStringLiteral("sparse"),
                                QStringLiteral("sparse,dense"),
                                {QStringLiteral("sparse"), QStringLiteral("dense")});
  options.backends =
      selection(QStringLiteral("backends"), QStringLiteral("simple"), QStringLiteral("simple,rg"),
                {QStringLiteral("simple"), QStringLiteral("rg")});
  for (const auto &size :
       selection(QStringLiteral("sizes"), QStringLiteral("100"), QStringLiteral("100,1000,10000"),
                 {QStringLiteral("100"), QStringLiteral("1000"), QStringLiteral("10000")})) {
    options.sizes.push_back(size.toInt());
  }
  const auto number = [&](const QString &name, int minimum, int maximum) {
    bool ok = false;
    const int value = parser.value(name).toInt(&ok);
    require(ok && value >= minimum && value <= maximum,
            QStringLiteral("Invalid numeric option: ") + name);
    return value;
  };
  options.pairs = number(QStringLiteral("pairs"), 1, 100000);
  options.warmups = number(QStringLiteral("warmups"), 0, 100000);
  options.occurrences = number(QStringLiteral("occurrences"), 1, 1000);
  options.batchSize = number(QStringLiteral("batch-size"), 1, 1000000);
  options.filter = parser.value(QStringLiteral("case"));
  options.tempParent = parser.value(QStringLiteral("temp-parent"));
  options.keepFixtures = parser.isSet(QStringLiteral("keep-fixtures"));
  return options;
}

void execute(const Options &options, QJsonObject &report, QJsonArray &rows) {
  IsolatedRuntime runtime(options);
  report[QStringLiteral("isolated_root")] = runtime.directory.path();
  report[QStringLiteral("fixtures_retained")] = options.keepFixtures;
  QString rgVersion;
  const bool hasRg = !options.backends.contains(QStringLiteral("rg")) || rgAvailable(rgVersion);
  report[QStringLiteral("rg_version")] = rgVersion;
  report[QStringLiteral("rg_configuration")] = QJsonObject{
      {QStringLiteral("source"),
       QStringLiteral("isolated RIPGREP_CONFIG_PATH; inherited value restored on exit")},
      {QStringLiteral("arguments"), QJsonArray{QStringLiteral("--sort=path")}},
      {QStringLiteral("scope"),
       QStringLiteral("both JSON and typed calls; deterministic cap membership; sorting may "
                      "disable rg parallel scanning")}};
  int sequence = 0;
  for (const auto &notebook : options.notebooks) {
    const bool raw = notebook == QStringLiteral("raw");
    for (const auto &metadataMode : options.metadata) {
      const bool populated = metadataMode == QStringLiteral("populated");
      if (options.domains.contains(QStringLiteral("folder")) &&
          options.shapes.contains(QStringLiteral("deep"))) {
        Fixture fixture;
        createFixture(fixture, runtime.context, runtime.directory.path(), sequence++, 50, raw,
                      populated, true, false, false, false, false, options.occurrences);
        folderCases(rows, options, fixture);
      }
      for (int size : options.sizes) {
        if (options.domains.contains(QStringLiteral("folder")) &&
            options.shapes.contains(QStringLiteral("wide"))) {
          Fixture fixture;
          createFixture(fixture, runtime.context, runtime.directory.path(), sequence++, size, raw,
                        populated, false, true, false, false, false, options.occurrences);
          folderCases(rows, options, fixture);
        }
        if (options.domains.contains(QStringLiteral("search"))) {
          Fixture fixture;
          createFixture(fixture, runtime.context, runtime.directory.path(), sequence++, size, raw,
                        populated, false, false, false, false, false, options.occurrences);
          for (const auto &backend : options.backends) {
            if (backend == QStringLiteral("rg") && !hasRg) {
              auto skipped = describe(fixture, QStringLiteral("search/rg"));
              skipped[QStringLiteral("status")] = QStringLiteral("skipped");
              skipped[QStringLiteral("reason")] = QStringLiteral(
                  "rg --version was unavailable or failed; no Simple fallback is reported as rg");
              rows.append(skipped);
            } else {
              searchCases(rows, options, fixture, backend);
            }
          }
        }
        if (!raw && options.domains.contains(QStringLiteral("tags"))) {
          for (const auto &hierarchy : options.hierarchies) {
            for (const auto &density : options.densities) {
              Fixture fixture;
              createFixture(fixture, runtime.context, runtime.directory.path(), sequence++, size,
                            false, populated, false, false, true,
                            density == QStringLiteral("dense"), hierarchy == QStringLiteral("deep"),
                            options.occurrences);
              tagCases(rows, options, fixture);
            }
          }
        }
      }
    }
  }
  require(
      !rows.isEmpty(),
      QStringLiteral("Selection produced no cases (tag associations require bundled notebooks)"));
  report[QStringLiteral("memory_at_end")] = memory();
}

void printReport(const QJsonObject &report) {
  const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
  std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stdout);
  std::fflush(stdout);
}
} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("benchmark_vxcore_typed_results"));
  QJsonObject report;
  QJsonArray rows;
  try {
    const auto options = arguments(application);
    report[QStringLiteral("schema_version")] = 1;
    report[QStringLiteral("vxcore_version")] = QString::fromUtf8(vxcore_get_version_string());
    report[QStringLiteral("qt_version")] = QString::fromLatin1(qVersion());
#ifdef QT_NO_DEBUG
    report[QStringLiteral("qt_debug_assertions")] = false;
#else
    report[QStringLiteral("qt_debug_assertions")] = true;
#endif
    report[QStringLiteral("warmup_pairs")] = options.warmups;
    report[QStringLiteral("measured_pairs")] = options.pairs;
    report[QStringLiteral("recommended_sample_count_met")] =
        options.warmups >= 3 && options.pairs >= 30;
    report[QStringLiteral("cache_scope")] = QStringLiteral(
        "warm filesystem/index; fixture creation and concrete semantic validation excluded; "
        "retained-snapshot conversion reported separately; first observed pair is not cold");
    report[QStringLiteral("pair_order")] =
        QStringLiteral("alternating JSON/typed and typed/JSON within each case");
    report[QStringLiteral("payload_byte_scope")] =
        QStringLiteral("JSON: exact UTF-8 bytes excluding NUL; typed: ABI-reachable fixed "
                       "records/arrays and counted UTF-8 bytes, not allocator usage, capacities, "
                       "terminators or owner-private storage");
    report[QStringLiteral("memory_scope")] = QStringLiteral(
        "native process metrics only; process-lifetime high-water includes fixture creation, "
        "retained validation snapshots and preceding cases, and fixture generation may dominate; "
        "current resident/private bytes are reported where available; interleaved JSON/typed paths "
        "share one process, so these are NOT independent per-path allocation or peak-memory "
        "evidence and cannot establish the 5% memory gate; no child rg RSS, allocation count or "
        "profiler estimate");
    report[QStringLiteral("measurement_limits")] = QJsonArray{
        QStringLiteral("C ABI acquisition combines producer work and transport construction; they "
                       "cannot be separated without production instrumentation"),
        QStringLiteral("Qt conversion uses the same pure converters as the production services; "
                       "retained snapshots isolate it from acquisition"),
        QStringLiteral("Tag dense means 16 common associations plus one rotating tag per file, not "
                       "a quadratic all-files/all-tags fixture"),
        QStringLiteral("Tag association cases use bundled notebooks; raw notebooks do not support "
                       "native tag count/find operations"),
        QStringLiteral(
            "Raw IDs/timestamps are native-assigned; fixture names/content/metadata/topology are "
            "deterministic, checksums are within-run, not portable across temp roots"),
        QStringLiteral("Raw populated metadata is seeded in one native-store transaction after "
                       "filesystem indexing and verified through public reads; setup is untimed"),
        QStringLiteral("rg preserves its existing first-submatch-per-line response; text "
                       "occurrence counts and emitted occurrence counts are reported separately"),
        QStringLiteral("rg uses an isolated --sort=path configuration in both paired paths; "
                       "command-length/native backend errors remain errors and return nonzero"),
        QStringLiteral("No cache eviction, allocation profiler, GUI model-refresh/render timing or "
                       "end-to-end application smoke; collect those externally"),
        QStringLiteral("Cancellation is requested after the first callback is owned; rg's "
                       "single-shot backend may have completed scanning before that point")};
    report[QStringLiteral("selection")] = QJsonObject{
        {QStringLiteral("domains"), QJsonArray::fromStringList(options.domains)},
        {QStringLiteral("notebooks"), QJsonArray::fromStringList(options.notebooks)},
        {QStringLiteral("metadata"), QJsonArray::fromStringList(options.metadata)},
        {QStringLiteral("shapes"), QJsonArray::fromStringList(options.shapes)},
        {QStringLiteral("hierarchies"), QJsonArray::fromStringList(options.hierarchies)},
        {QStringLiteral("densities"), QJsonArray::fromStringList(options.densities)},
        {QStringLiteral("backends"), QJsonArray::fromStringList(options.backends)},
        {QStringLiteral("case_filter"), options.filter}};
    QJsonArray sizes;
    for (int size : options.sizes) {
      sizes.append(size);
    }
    auto selection = report.value(QStringLiteral("selection")).toObject();
    selection[QStringLiteral("sizes")] = sizes;
    report[QStringLiteral("selection")] = selection;
    execute(options, report, rows);
    report[QStringLiteral("status")] = QStringLiteral("ok");
    report[QStringLiteral("cases")] = rows;
    printReport(report);
    return 0;
  } catch (const std::exception &error) {
    report[QStringLiteral("status")] = QStringLiteral("error");
    report[QStringLiteral("error")] = QString::fromUtf8(error.what());
    report[QStringLiteral("cases")] = rows;
    printReport(report);
    return 1;
  } catch (...) {
    report[QStringLiteral("status")] = QStringLiteral("error");
    report[QStringLiteral("error")] = QStringLiteral("Unknown exception");
    report[QStringLiteral("cases")] = rows;
    printReport(report);
    return 1;
  }
}
