#include "findunitedentry.h"

#include <QLabel>
#include <QCommandLineOption>
#include <QTimer>
#include <QDebug>

#include <utils/processutils.h>
#include <search/searchtoken.h>
#include <search/searchdata.h>
#include <search/isearchinfoprovider.h>
#include <search/searchresultitem.h>
#include <search/searcher.h>
#include <search/searchhelper.h>
#include <search/searchtoken.h>
#include <core/fileopenparameters.h>
#include <core/vnotex.h>
#include <widgets/treewidget.h>
#include "unitedentryhelper.h"
#include "entrywidgetfactory.h"
#include "unitedentrymgr.h"

using namespace vnotex;

FindUnitedEntry::FindUnitedEntry(const QSharedPointer<ISearchInfoProvider> &p_provider,
                                 UnitedEntryMgr *p_mgr,
                                 QObject *p_parent)
    : IUnitedEntry("find",
                   tr("Search for files in notebooks"),
                   p_mgr,
                   p_parent),
      m_provider(p_provider)
{
    m_processTimer = new QTimer(this);
    m_processTimer->setSingleShot(true);
    m_processTimer->setInterval(500);
    connect(m_processTimer, &QTimer::timeout,
            this, &FindUnitedEntry::doProcessInternal);
}

void FindUnitedEntry::initOnFirstProcess()
{
    m_parser.setApplicationDescription(tr("Search for files in notebooks with advanced options for scope, object, target and so on."));

    m_parser.addPositionalArgument("keywords", tr("Keywords to search for."));

    QCommandLineOption scopeOpt({"s", "scope"},
                                tr("Search scope. Possible values: buffer/folder/notebook/all_notebook."),
                                tr("search_scope"),
                                "notebook");
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

    QCommandLineOption patternOpt({"p", "pattern"},
                                 tr("Wildcard pattern of files to search."),
                                 tr("file_pattern"));
    m_parser.addOption(patternOpt);

    SearchToken::addSearchOptionsToCommand(&m_parser);
}

void FindUnitedEntry::processInternal(const QString &p_args,
                                      const std::function<void(const QSharedPointer<QWidget> &)> &p_popupWidgetFunc)
{
    // Do another timer delay here since it is a very expensive operation.
    m_processTimer->stop();

    Q_ASSERT(!isOngoing());

    setOngoing(true);

    auto args = ProcessUtils::parseCombinedArgString(p_args);
    // The parser needs the first arg to be the application name.
    args.prepend(name());
    bool ret = m_parser.parse(args);
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

    auto opt = collectOptions();
    if (m_searchOption && m_searchOption->strictEquals(*opt)) {
        // Reuse last result.
        p_popupWidgetFunc(m_resultTree);
        finish();
        return;
    }

    m_searchOption = opt;

    m_searchTokenOfSession.clear();

    prepareResultTree();

    p_popupWidgetFunc(m_resultTree);

    m_processTimer->start();
}

QSharedPointer<SearchOption> FindUnitedEntry::collectOptions() const
{
    auto opt = QSharedPointer<SearchOption>::create();

    opt->m_engine = SearchEngine::Internal;

    opt->m_keyword = m_parser.positionalArguments().join(QLatin1Char(' '));
    Q_ASSERT(!opt->m_keyword.isEmpty());

    opt->m_filePattern = m_parser.value("p");

    {
        SearchScope scope = SearchScope::CurrentNotebook;
        const auto scopeStr = m_parser.value("s");
        if (scopeStr == QStringLiteral("buffer")) {
            scope = SearchScope::Buffers;
        } else if (scopeStr == QStringLiteral("folder")) {
            scope = SearchScope::CurrentFolder;
        } else if (scopeStr == QStringLiteral("notebook")) {
            scope = SearchScope::CurrentNotebook;
        } else if (scopeStr == QStringLiteral("all_notebook")) {
            scope = SearchScope::AllNotebooks;
        }
        opt->m_scope = scope;
    }

    {
        SearchObjects objects = SearchObject::ObjectNone;
        const auto objectStrs = m_parser.values("b");
        for (const auto &str : objectStrs) {
            if (str == QStringLiteral("name")) {
                objects |= SearchObject::SearchName;
            } else if (str == QStringLiteral("content")) {
                objects |= SearchObject::SearchContent;
            } else if (str == QStringLiteral("tag")) {
                objects |= SearchObject::SearchTag;
            } else if (str == QStringLiteral("path")) {
                objects |= SearchObject::SearchPath;
            }
        }
        opt->m_objects = objects;
    }

    {
        SearchTargets targets = SearchTarget::TargetNone;
        auto targetStrs = m_parser.values("t");
        for (const auto &str : targetStrs) {
            if (str == QStringLiteral("file")) {
                targets |= SearchTarget::SearchFile;
            } else if (str == QStringLiteral("folder")) {
                targets |= SearchTarget::SearchFolder;
            } else if (str == QStringLiteral("notebook")) {
                targets |= SearchTarget::SearchNotebook;
            }
        }
        opt->m_targets = targets;
    }

    {
        FindOptions options = FindOption::FindNone;
        if (m_parser.isSet("c")) {
            options |= FindOption::CaseSensitive;
        }
        if (m_parser.isSet("r")) {
            options |= FindOption::RegularExpression;
        }
        if (m_parser.isSet("w")) {
            options |= FindOption::WholeWordOnly;
        }
        if (m_parser.isSet("f")) {
            options |= FindOption::FuzzySearch;
        }
        opt->m_findOptions = options;
    }

    return opt;
}

QString FindUnitedEntry::getHelpText() const
{
    auto text = m_parser.helpText();
    // Skip the first line containing the application name.
    return text.mid(text.indexOf('\n') + 1);
}

Searcher *FindUnitedEntry::getSearcher()
{
    if (!m_searcher) {
        m_searcher = new Searcher(this);
        connect(m_searcher, &Searcher::resultItemAdded,
                this, [this](const QSharedPointer<SearchResultItem> &p_item) {
                    addLocation(p_item->m_location);
                });
        connect(m_searcher, &Searcher::resultItemsAdded,
                this, [this](const QVector<QSharedPointer<SearchResultItem>> &p_items) {
                    for (const auto &item : p_items) {
                        addLocation(item->m_location);
                    }
                });
        connect(m_searcher, &Searcher::finished,
                this, &FindUnitedEntry::handleSearchFinished);
    }
    return m_searcher;
}

void FindUnitedEntry::handleSearchFinished(SearchState p_state)
{
    if (p_state != SearchState::Busy) {
        getSearcher()->clear();
        finish();
    }
}

void FindUnitedEntry::prepareResultTree()
{
    if (!m_resultTree) {
        m_resultTree = EntryWidgetFactory::createTreeWidget(1);
        connect(m_resultTree.data(), &QTreeWidget::itemActivated,
                this, &FindUnitedEntry::handleItemActivated);
    }

    m_resultTree->clear();
}

void FindUnitedEntry::addLocation(const ComplexLocation &p_location)
{
    auto item = new QTreeWidgetItem(m_resultTree.data());
    item->setText(0, p_location.m_displayPath);
    item->setIcon(0, UnitedEntryHelper::itemIcon(UnitedEntryHelper::locationTypeToItemType(p_location.m_type)));
    item->setData(0, Qt::UserRole, p_location.m_path);
    item->setToolTip(0, p_location.m_path);

    // Add sub items.
    for (const auto &line : p_location.m_lines) {
        auto subItem = new QTreeWidgetItem(item);

        // Truncate the text.
        if (line.m_text.size() > 500) {
            subItem->setText(0, line.m_text.left(500));
        } else {
            subItem->setText(0, line.m_text);
        }

        if (!line.m_segments.isEmpty()) {
            subItem->setData(0, HighlightsRole, QVariant::fromValue(line.m_segments));
        }

        subItem->setData(0, Qt::UserRole, line.m_lineNumber);
    }

    if (m_mgr->getExpandAllEnabled()) {
        item->setExpanded(true);
    }

    if (m_resultTree->topLevelItemCount() == 1) {
        m_resultTree->setCurrentItem(item);
    }
}

void FindUnitedEntry::doProcessInternal()
{
    if (isAskedToStop()) {
        finish();
        return;
    }

    QString msg;
    auto state = SearchHelper::searchOnProvider(getSearcher(), m_searchOption, m_provider, msg);
    if (!msg.isEmpty()) {
        qWarning() << msg;
    }

    handleSearchFinished(state);
}

void FindUnitedEntry::stop()
{
    IUnitedEntry::stop();

    if (m_processTimer->isActive()) {
        m_processTimer->stop();
        // Let it go finished.
        doProcessInternal();
    }
}

void FindUnitedEntry::finish()
{
    setOngoing(false);
    emit finished();
}

QSharedPointer<QWidget> FindUnitedEntry::currentPopupWidget() const
{
    return m_resultTree;
}

void FindUnitedEntry::handleItemActivated(QTreeWidgetItem *p_item, int p_column)
{
    Q_UNUSED(p_column);

    if (!m_searchTokenOfSession) {
        if (m_searchOption->m_objects & SearchObject::SearchContent) {
            m_searchTokenOfSession = QSharedPointer<SearchToken>::create(getSearcher()->getToken());
        }
    }

    // TODO: decode the path of location and handle different types of destination.
    auto paras = QSharedPointer<FileOpenParameters>::create();

    QString itemPath;
    auto pa = p_item->parent();
    if (pa) {
        itemPath = pa->data(0, Qt::UserRole).toString();
        paras->m_lineNumber = p_item->data(0, Qt::UserRole).toInt();
    } else {
        itemPath = p_item->data(0, Qt::UserRole).toString();
        // Use the first line number if there is any.
        if (p_item->childCount() > 0) {
            auto childItem = p_item->child(0);
            paras->m_lineNumber = childItem->data(0, Qt::UserRole).toInt();
        }
    }

    paras->m_searchToken = m_searchTokenOfSession;
    emit VNoteX::getInst().openFileRequested(itemPath, paras);

    emit itemActivated(true, false);
}
