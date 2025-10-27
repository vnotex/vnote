#include "searchresultitem.h"

using namespace vnotex;

QSharedPointer<SearchResultItem>
SearchResultItem::createBufferItem(const QString &p_targetPath, const QString &p_displayPath,
                                   int p_lineNumber, const QString &p_text,
                                   const QList<Segment> &p_segments) {
  auto item = createBufferItem(p_targetPath, p_displayPath);
  item->m_location.addLine(p_lineNumber, p_text, p_segments);
  return item;
}

QSharedPointer<SearchResultItem> SearchResultItem::createBufferItem(const QString &p_targetPath,
                                                                    const QString &p_displayPath) {
  auto item = QSharedPointer<SearchResultItem>::create();
  item->m_location.m_type = LocationType::Buffer;
  item->m_location.m_path = p_targetPath;
  item->m_location.m_displayPath = p_displayPath;
  return item;
}

QSharedPointer<SearchResultItem>
SearchResultItem::createFileItem(const QString &p_targetPath, const QString &p_displayPath,
                                 int p_lineNumber, const QString &p_text,
                                 const QList<Segment> &p_segments) {
  auto item = createFileItem(p_targetPath, p_displayPath);
  item->m_location.addLine(p_lineNumber, p_text, p_segments);
  return item;
}

QSharedPointer<SearchResultItem> SearchResultItem::createFileItem(const QString &p_targetPath,
                                                                  const QString &p_displayPath) {
  auto item = QSharedPointer<SearchResultItem>::create();
  item->m_location.m_type = LocationType::File;
  item->m_location.m_path = p_targetPath;
  item->m_location.m_displayPath = p_displayPath;
  return item;
}

QSharedPointer<SearchResultItem> SearchResultItem::createFolderItem(const QString &p_targetPath,
                                                                    const QString &p_displayPath) {
  auto item = QSharedPointer<SearchResultItem>::create();
  item->m_location.m_type = LocationType::Folder;
  item->m_location.m_path = p_targetPath;
  item->m_location.m_displayPath = p_displayPath;
  return item;
}

QSharedPointer<SearchResultItem>
SearchResultItem::createNotebookItem(const QString &p_targetPath, const QString &p_displayPath) {
  auto item = QSharedPointer<SearchResultItem>::create();
  item->m_location.m_type = LocationType::Notebook;
  item->m_location.m_path = p_targetPath;
  item->m_location.m_displayPath = p_displayPath;
  return item;
}

void SearchResultItem::addLine(int p_lineNumber, const QString &p_text,
                               const QList<Segment> &p_segments) {
  m_location.addLine(p_lineNumber, p_text, p_segments);
}
