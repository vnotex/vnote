#ifndef SEARCHPANEL_H
#define SEARCHPANEL_H

#include <QFrame>
#include <QSharedPointer>
#include <QList>

#include <search/searchdata.h>
#include <search/searcher.h>

class QComboBox;
class QCheckBox;
class QFormLayout;
class QProgressBar;
class QToolButton;
class QPlainTextEdit;
class QRadioButton;
class QButtonGroup;
class QVBoxLayout;

namespace vnotex
{
    class TitleBar;
    class Buffer;
    class Node;
    class Notebook;
    class LocationList;
    struct Location;

    class ISearchInfoProvider
    {
    public:
        ISearchInfoProvider() = default;

        virtual ~ISearchInfoProvider() = default;

        virtual QList<Buffer *> getBuffers() const = 0;

        virtual Node *getCurrentFolder() const = 0;

        virtual Notebook *getCurrentNotebook() const = 0;

        virtual QVector<Notebook *> getNotebooks() const = 0;
    };

    class SearchPanel : public QFrame
    {
        Q_OBJECT
    public:
        explicit SearchPanel(const QSharedPointer<ISearchInfoProvider> &p_provider, QWidget *p_parent = nullptr);

    private slots:
        void startSearch();

        void stopSearch();

        void handleSearchFinished(SearchState p_state);

        void updateProgress(int p_val, int p_maximum);

        void appendLog(const QString &p_text);

    private:
        void setupUI();

        TitleBar *setupTitleBar(const QString &p_title, QWidget *p_parent = nullptr);

        void setupSearchObject(QFormLayout *p_layout, QWidget *p_parent = nullptr);

        void setupSearchTarget(QFormLayout *p_layout, QWidget *p_parent = nullptr);

        void setupFindOption(QFormLayout *p_layout, QWidget *p_parent = nullptr);

        void initOptions();

        void restoreFields(const SearchOption &p_option);

        void saveFields(SearchOption &p_option);

        void updateUIOnSearch();

        void clearLog();

        SearchState search(const QSharedPointer<SearchOption> &p_option);

        bool isSearchOptionValid(const SearchOption &p_option);

        Searcher *getSearcher();

        void prepareLocationList();

        void handleLocationActivated(const Location &p_location);

        QSharedPointer<ISearchInfoProvider> m_provider;

        QVBoxLayout *m_mainLayout = nullptr;

        QToolButton *m_searchBtn = nullptr;

        QToolButton *m_advancedSettingsBtn = nullptr;

        QComboBox *m_keywordComboBox = nullptr;

        QComboBox *m_searchScopeComboBox = nullptr;

        QCheckBox *m_searchObjectNameCheckBox = nullptr;

        QCheckBox *m_searchObjectContentCheckBox = nullptr;

        QCheckBox *m_searchObjectOutlineCheckBox = nullptr;

        QCheckBox *m_searchObjectTagCheckBox = nullptr;

        QCheckBox *m_searchObjectPathCheckBox = nullptr;

        QCheckBox *m_searchTargetFileCheckBox = nullptr;

        QCheckBox *m_searchTargetFolderCheckBox = nullptr;

        QCheckBox *m_searchTargetNotebookCheckBox = nullptr;

        QComboBox *m_filePatternComboBox = nullptr;

        QCheckBox *m_caseSensitiveCheckBox = nullptr;

        // WholeWordOnly/RegularExpression/FuzzySearch is exclusive.
        QRadioButton *m_plainTextRadioBtn = nullptr;

        QRadioButton *m_wholeWordOnlyRadioBtn = nullptr;

        QRadioButton *m_fuzzySearchRadioBtn = nullptr;

        QRadioButton *m_regularExpressionRadioBtn = nullptr;

        QWidget *m_advancedSettings = nullptr;

        QProgressBar *m_progressBar = nullptr;

        QPlainTextEdit *m_infoTextEdit = nullptr;

        QSharedPointer<SearchOption> m_option;

        bool m_searchOngoing = false;

        Searcher *m_searcher = nullptr;

        LocationList *m_locationList = nullptr;
    };
}

#endif // SEARCHPANEL_H
