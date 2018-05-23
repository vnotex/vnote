#ifndef VSEARCHER_H
#define VSEARCHER_H

#include <QWidget>
#include <QSharedPointer>

#include "vsearch.h"

#include "vnavigationmode.h"

class QComboBox;
class QCheckBox;
class QPushButton;
class QLabel;
class VSearchResultTree;
class QProgressBar;
class QPlainTextEdit;
class QShowEvent;

class VSearcher : public QWidget, public VNavigationMode
{
    Q_OBJECT
public:
    explicit VSearcher(QWidget *p_parent = nullptr);

    void focusToSearch();

    // Implementations for VNavigationMode.
    void showNavigation() Q_DECL_OVERRIDE;
    bool handleKeyNavigation(int p_key, bool &p_succeed) Q_DECL_OVERRIDE;

protected:
    void showEvent(QShowEvent *p_event) Q_DECL_OVERRIDE;

private slots:
    void handleSearchFinished(const QSharedPointer<VSearchResult> &p_result);

private:
    void startSearch();

    void setupUI();

    void init();

    void initUIFields();

    void setProgressVisible(bool p_visible);

    void appendLogLine(const QString &p_text);

    void handleInputChanged();

    void animateSearchClick();

    void updateItemToComboBox(QComboBox *p_comboBox);

    // Get the OR of the search options.
    int getSearchOption() const;

    void updateNumLabel(int p_count);

    void showMessage(const QString &p_text) const;

    QComboBox *m_keywordCB;

    // All notebooks, current notebook, and so on.
    QComboBox *m_searchScopeCB;

    // Name, content, tag.
    QComboBox *m_searchObjectCB;

    // Notebook, folder, note.
    QComboBox *m_searchTargetCB;

    QComboBox *m_filePatternCB;

    QComboBox *m_searchEngineCB;

    QCheckBox *m_caseSensitiveCB;

    QCheckBox *m_wholeWordOnlyCB;

    QCheckBox *m_fuzzyCB;

    QCheckBox *m_regularExpressionCB;

    QPushButton *m_searchBtn;

    QPushButton *m_clearBtn;

    QPushButton *m_advBtn;

    QPushButton *m_consoleBtn;

    QLabel *m_numLabel;

    QWidget *m_advWidget;

    VSearchResultTree *m_results;

    QProgressBar *m_proBar;

    QPushButton *m_cancelBtn;

    QPlainTextEdit *m_consoleEdit;

    bool m_initialized;

    bool m_uiInitialized;

    bool m_inSearch;

    bool m_askedToStop;

    VSearch m_search;
};

#endif // VSEARCHER_H
