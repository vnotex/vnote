#include "searchresulttypes.h"

#include <QJsonArray>
#include <QJsonObject>

using namespace vnotex;

SearchResult SearchResult::fromContentSearchJson(const QJsonObject &p_json,
                                                 const QString &p_notebookId)
{
  SearchResult result;
  result.m_matchCount = p_json.value(QStringLiteral("matchCount")).toInt(0);
  result.m_truncated = p_json.value(QStringLiteral("truncated")).toBool(false);

  const QJsonArray matches = p_json.value(QStringLiteral("matches")).toArray();
  result.m_fileResults.reserve(matches.size());

  for (const QJsonValue &fileVal : matches) {
    const QJsonObject fileObj = fileVal.toObject();

    SearchFileResult fileResult;
    fileResult.m_type = SearchResultType::File;
    fileResult.m_path = fileObj.value(QStringLiteral("path")).toString();
    fileResult.m_id = fileObj.value(QStringLiteral("id")).toString();
    fileResult.m_notebookId = p_notebookId;

    const QJsonArray lineMatches = fileObj.value(QStringLiteral("matches")).toArray();
    fileResult.m_lineMatches.reserve(lineMatches.size());

    for (const QJsonValue &lineVal : lineMatches) {
      const QJsonObject lineObj = lineVal.toObject();

      SearchLineMatch lineMatch;
      lineMatch.m_lineNumber = lineObj.value(QStringLiteral("lineNumber")).toInt(-1);
      lineMatch.m_columnStart = lineObj.value(QStringLiteral("columnStart")).toInt(0);
      lineMatch.m_columnEnd = lineObj.value(QStringLiteral("columnEnd")).toInt(0);
      lineMatch.m_lineText = lineObj.value(QStringLiteral("lineText")).toString();

      fileResult.m_lineMatches.append(lineMatch);
    }

    result.m_fileResults.append(fileResult);
  }

  return result;
}

SearchResult SearchResult::fromFileSearchJson(const QJsonObject &p_json,
                                              const QString &p_notebookId)
{
  SearchResult result;
  result.m_matchCount = p_json.value(QStringLiteral("matchCount")).toInt(0);
  result.m_truncated = p_json.value(QStringLiteral("truncated")).toBool(false);

  const QJsonArray matches = p_json.value(QStringLiteral("matches")).toArray();
  result.m_fileResults.reserve(matches.size());

  for (const QJsonValue &itemVal : matches) {
    const QJsonObject itemObj = itemVal.toObject();

    SearchFileResult fileResult;

    const QString typeStr = itemObj.value(QStringLiteral("type")).toString();
    fileResult.m_type = (typeStr == QStringLiteral("folder"))
                            ? SearchResultType::Folder
                            : SearchResultType::File;

    fileResult.m_path = itemObj.value(QStringLiteral("path")).toString();
    fileResult.m_absolutePath = itemObj.value(QStringLiteral("absolute_path")).toString();
    fileResult.m_id = itemObj.value(QStringLiteral("id")).toString();
    fileResult.m_notebookId = p_notebookId;
    // m_lineMatches left empty for file search results.

    result.m_fileResults.append(fileResult);
  }

  return result;
}
