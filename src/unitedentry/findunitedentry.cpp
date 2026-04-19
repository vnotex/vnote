#include "findunitedentry.h"

#include <QCommandLineOption>
#include <QDebug>
#include <QFileInfo>
#include <QLabel>
#include <QTimer>
#include <QTreeWidgetItem>

#include "entrywidgetfactory.h"
#include "unitedentryhelper.h"
#include "unitedentrymgr.h"
#include <core/fileopensettings.h>
#include <core/global.h>
#include <core/nodeidentifier.h>
#include <core/searchresulttypes.h>
#include <core/servicelocator.h>
#include <core/services/bufferservice.h>
#include <gui/services/themeservice.h>
#include <models/searchresultmodel.h>
#include <utils/processutils.h>
#include <widgets/treewidget.h>

using namespace vnotex;

FindUnitedEntry::FindUnitedEntry(ServiceLocator &p_services, UnitedEntryMgr *p_mgr,
                                 QObject *p_parent)
    : IUnitedEntry("find", tr("Search for files in notebooks"), p_mgr, p_parent),
      m_services(p_services),
      m_helper(p_services.get<ThemeService>()) {
  m_processTimer = new QTimer(this);
  m_processTimer->setSingleShot(true);
  m_processTimer->setInterval(500);
  connect(m_processTimer, &QTimer::timeout, this, &FindUnitedEntry::doProcessInternal);

  auto *themeService = m_services.get<ThemeService>();
  connect(themeService, &ThemeService::themeChanged, this, [this]() { m_helper.refreshIcons(); });
}

void FindUnitedEntry::initOnFirstProcess() {
  m_parser.setApplicationDescription(tr(
      "Search for files in notebooks with advanced options for scope, object, target and so on."));

  m_parser.addPositionalArgument("keywords", tr("Keywords to search for."));

  QCommandLineOption scopeOpt(
      {"s", "scope"}, tr("Search scope. Possible values: buffer/folder/notebook/all_notebook."),
      tr("search_scope"), "notebook");
  m_parser.addOption(scopeOpt);

  QCommandLineOption objectOpt({"b", "object"},
                               tr("Search objects. Possible values: name/content/tag/path."),
                               tr("search_objects"));
  objectOpt.setDefaultValues({"name", "content"});
  m_parser.addOption(objectOpt);

  QCommandLineOption targetOpt({"t", "target"},
                               tr("Search targets. Possible values: file/folder/notebook."),
                               tr("search_targets"));
  targetOpt.setDefaultValues({"file", "folder"});
  m_parser.addOption(targetOpt);

  QCommandLineOption patternOpt({"p", "pattern"}, tr("Wildcard pattern of files to search."),
                                tr("file_pattern"));
  m_parser.addOption(patternOpt);

  m_parser.addOption(QCommandLineOption({"c", "case-sensitive"}, tr("Search in case sensitive.")));
  m_parser.addOption(
      QCommandLineOption({"r", "regular-expression"}, tr("Search by regular expression.")));
  m_parser.addOption(QCommandLineOption({"w", "whole-word-only"}, tr("Search whole word only.")));
  m_parser.addOption(QCommandLineOption(
      {"f", "fuzzy-search"}, tr("Do a fuzzy search (not applicable to content search).")));

  m_searchController = new SearchController(m_services, this);
  m_resultModel = new SearchResultModel(this);
  m_searchController->setModel(m_resultModel);

  connect(m_searchController, &SearchController::searchStarted, this,
          [this]() { m_searchActive = true; });
  connect(m_searchController, &SearchController::searchFinished, this, [this](int, bool) {
    m_searchActive = false;
    populateResultTree();
    finish();
  });
  connect(m_searchController, &SearchController::searchFailed, this,
          [this](const QString &p_errorMessage) {
            m_searchActive = false;
            showMessageInResultTree(tr("Error: %1").arg(p_errorMessage));
            finish();
          });
  connect(m_searchController, &SearchController::searchCancelled, this, [this]() {
    m_searchActive = false;
    finish();
  });
}

FindUnitedEntry::SearchParams
FindUnitedEntry::mapArgsToSearchParams(const QCommandLineParser &p_parser) {
  SearchParams params;
  params.keyword = p_parser.positionalArguments().join(QLatin1Char(' ')).trimmed();
  params.caseSensitive = p_parser.isSet(QStringLiteral("c"));
  params.useRegex = p_parser.isSet(QStringLiteral("r"));
  params.filePattern = p_parser.value(QStringLiteral("p")).trimmed();

  const auto scopeStr = p_parser.value(QStringLiteral("s"));
  if (scopeStr == QStringLiteral("buffer")) {
    params.scope = SearchController::Buffers;
  } else if (scopeStr == QStringLiteral("folder")) {
    params.scope = SearchController::CurrentFolder;
  } else if (scopeStr == QStringLiteral("all_notebook")) {
    params.scope = SearchController::AllNotebooks;
  } else {
    params.scope = SearchController::CurrentNotebook;
  }

  bool hasContent = false;
  bool hasTag = false;
  bool hasNameOrPath = false;
  const auto objectStrs = p_parser.values(QStringLiteral("b"));
  for (const auto &str : objectStrs) {
    if (str == QStringLiteral("content")) {
      hasContent = true;
    } else if (str == QStringLiteral("tag")) {
      hasTag = true;
    } else if (str == QStringLiteral("name") || str == QStringLiteral("path")) {
      hasNameOrPath = true;
    }
  }

  if (hasContent) {
    params.searchMode = SearchController::ContentSearch;
  } else if (hasNameOrPath || objectStrs.size() > 1) {
    params.searchMode = SearchController::FileNameSearch;
  } else if (hasTag) {
    params.searchMode = SearchController::TagSearch;
  }

  if (p_parser.isSet(QStringLiteral("w"))) {
    qDebug() << "FindUnitedEntry: -w is unsupported in SearchController and will be ignored";
  }

  if (p_parser.isSet(QStringLiteral("f"))) {
    qDebug() << "FindUnitedEntry: -f is unsupported in SearchController and will be ignored";
  }

  if (p_parser.isSet(QStringLiteral("t"))) {
    qDebug() << "FindUnitedEntry: -t is unsupported in SearchController and will be ignored";
  }

  return params;
}

void FindUnitedEntry::processInternal(
    const QString &p_args,
    const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc) {
  m_processTimer->stop();

  Q_ASSERT(!isOngoing());

  setOngoing(true);

  auto args = ProcessUtils::parseCombinedArgString(p_args);
  args.prepend(name());
  const bool ret = m_parser.parse(args);
  const auto positionalArgs = m_parser.positionalArguments();
  if (!ret) {
    auto label = EntryWidgetFactory::createLabel(m_parser.errorText());
    p_popupWidgetFunc(label);
    finish();
    return;
  }

  if (positionalArgs.isEmpty()) {
    auto label = EntryWidgetFactory::createLabel(getHelpText());
    p_popupWidgetFunc(label);
    finish();
    return;
  }

  const auto params = mapArgsToSearchParams(m_parser);
  if (params.keyword.isEmpty()) {
    auto label = EntryWidgetFactory::createLabel(getHelpText());
    p_popupWidgetFunc(label);
    finish();
    return;
  }

  const bool sameSearch = m_hasLastSearchParams && m_lastSearchParams.keyword == params.keyword &&
                          m_lastSearchParams.scope == params.scope &&
                          m_lastSearchParams.searchMode == params.searchMode &&
                          m_lastSearchParams.caseSensitive == params.caseSensitive &&
                          m_lastSearchParams.useRegex == params.useRegex &&
                          m_lastSearchParams.filePattern == params.filePattern;
  if (sameSearch && m_resultTree) {
    p_popupWidgetFunc(m_resultTree);
    finish();
    return;
  }

  m_lastSearchParams = params;
  m_hasLastSearchParams = true;

  prepareResultTree();
  p_popupWidgetFunc(m_resultTree);
  m_processTimer->start();
}

QString FindUnitedEntry::getHelpText() const {
  auto text = m_parser.helpText();
  return text.mid(text.indexOf('\n') + 1);
}

void FindUnitedEntry::prepareResultTree() {
  if (!m_resultTree) {
    m_resultTree = EntryWidgetFactory::createTreeWidget(1);
    connect(m_resultTree.data(), &QTreeWidget::itemActivated, this,
            &FindUnitedEntry::handleItemActivated);
  }

  m_resultTree->clear();
}

void FindUnitedEntry::populateResultTree() {
  prepareResultTree();

  const int topLevelRows = m_resultModel ? m_resultModel->rowCount() : 0;
  for (int i = 0; i < topLevelRows; ++i) {
    const QModelIndex fileIdx = m_resultModel->index(i, 0);
    if (!fileIdx.isValid() || fileIdx.internalId() != 0) {
      continue;
    }

    const QString relativePath = m_resultModel->data(fileIdx, Qt::ToolTipRole).toString();
    const NodeIdentifier nodeId =
        qvariant_cast<NodeIdentifier>(m_resultModel->data(fileIdx, SearchResultModel::NodeIdRole));
    const QString absolutePath =
        m_resultModel->data(fileIdx, SearchResultModel::AbsolutePathRole).toString();

    UnitedEntryHelper::ItemType itemType = UnitedEntryHelper::File;
    if ((!absolutePath.isEmpty() && QFileInfo(absolutePath).isDir()) ||
        relativePath.endsWith(QLatin1Char('/'))) {
      itemType = UnitedEntryHelper::Folder;
    }

    auto *item = new QTreeWidgetItem(m_resultTree.data());
    item->setText(0, relativePath);
    item->setToolTip(0, relativePath);
    item->setIcon(0, m_helper.itemIcon(itemType));
    item->setData(0, Qt::UserRole, QVariant::fromValue(nodeId));

    const int childRows = m_resultModel->rowCount(fileIdx);
    for (int j = 0; j < childRows; ++j) {
      const QModelIndex lineIdx = m_resultModel->index(j, 0, fileIdx);
      if (!lineIdx.isValid() || lineIdx.internalId() == 0) {
        continue;
      }

      QString lineText = m_resultModel->data(lineIdx, Qt::DisplayRole).toString();
      const int columnStart =
          m_resultModel->data(lineIdx, SearchResultModel::ColumnStartRole).toInt();
      const int columnEnd = m_resultModel->data(lineIdx, SearchResultModel::ColumnEndRole).toInt();
      const int lineNumber =
          m_resultModel->data(lineIdx, SearchResultModel::LineNumberRole).toInt();

      QList<Segment> segments;
      if (columnEnd > columnStart && columnStart >= 0) {
        int textLength = lineText.size();
        if (textLength > 500) {
          textLength = 500;
          lineText = lineText.left(500);
        }

        if (columnStart < textLength) {
          segments.append(Segment(columnStart, qMin(columnEnd, textLength) - columnStart));
        }
      } else if (lineText.size() > 500) {
        lineText = lineText.left(500);
      }

      auto *subItem = new QTreeWidgetItem(item);
      subItem->setText(0, lineText);
      subItem->setData(0, Qt::UserRole, lineNumber);
      if (!segments.isEmpty()) {
        subItem->setData(0, HighlightsRole, QVariant::fromValue(segments));
      }
    }

    if (m_mgr->getExpandAllEnabled()) {
      item->setExpanded(true);
    }
  }

  if (m_resultTree->topLevelItemCount() > 0) {
    m_resultTree->setCurrentItem(m_resultTree->topLevelItem(0));
  }
}

void FindUnitedEntry::showMessageInResultTree(const QString &p_message) {
  prepareResultTree();

  auto *item = new QTreeWidgetItem(m_resultTree.data());
  item->setText(0, p_message);
  m_resultTree->setCurrentItem(item);
}

void FindUnitedEntry::doProcessInternal() {
  if (isAskedToStop()) {
    finish();
    return;
  }

  if (!m_searchController) {
    finish();
    return;
  }

  m_searchController->setCurrentNotebookId(m_mgr->currentNotebookId());
  m_searchController->setCurrentFolderId(m_mgr->currentFolderId());
  m_searchActive = true;
  m_searchController->search(m_lastSearchParams.keyword, m_lastSearchParams.scope,
                             m_lastSearchParams.searchMode, m_lastSearchParams.caseSensitive,
                             m_lastSearchParams.useRegex, m_lastSearchParams.filePattern);
}

void FindUnitedEntry::stop() {
  IUnitedEntry::stop();

  if (m_processTimer->isActive()) {
    m_processTimer->stop();
    finish();
    return;
  }

  if (m_searchActive && m_searchController) {
    m_searchController->cancel();
    return;
  }

  finish();
}

void FindUnitedEntry::finish() {
  setOngoing(false);
  emit finished();
}

QSharedPointer<QWidget> FindUnitedEntry::currentPopupWidget() const { return m_resultTree; }

void FindUnitedEntry::handleItemActivated(QTreeWidgetItem *p_item, int p_column) {
  Q_UNUSED(p_column);

  if (!p_item) {
    return;
  }

  NodeIdentifier nodeId;
  int lineNumber = -1;
  auto *parentItem = p_item->parent();
  if (parentItem) {
    nodeId = qvariant_cast<NodeIdentifier>(parentItem->data(0, Qt::UserRole));
    lineNumber = p_item->data(0, Qt::UserRole).toInt();
  } else {
    nodeId = qvariant_cast<NodeIdentifier>(p_item->data(0, Qt::UserRole));
    if (p_item->childCount() > 0) {
      lineNumber = p_item->child(0)->data(0, Qt::UserRole).toInt();
    }
  }

  if (!nodeId.isValid()) {
    return;
  }

  FileOpenSettings settings;
  if (lineNumber > -1) {
    settings.m_lineNumber = lineNumber;
  }

  if (m_lastSearchParams.searchMode == SearchController::ContentSearch &&
      !m_lastSearchParams.keyword.isEmpty()) {
    settings.m_searchHighlight.m_patterns = QStringList{m_lastSearchParams.keyword};
    settings.m_searchHighlight.m_options = FindNone;
    if (m_lastSearchParams.caseSensitive) {
      settings.m_searchHighlight.m_options |= CaseSensitive;
    }
    if (m_lastSearchParams.useRegex) {
      settings.m_searchHighlight.m_options |= RegularExpression;
    }
    settings.m_searchHighlight.m_currentMatchLine = lineNumber;
    settings.m_searchHighlight.m_isValid = true;
  }

  auto *bufferSvc = m_services.get<BufferService>();
  if (bufferSvc) {
    bufferSvc->openBuffer(nodeId, settings);
  }

  emit itemActivated(true, false);
}
