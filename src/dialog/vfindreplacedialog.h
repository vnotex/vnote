#ifndef VFINDREPLACEDIALOG_H
#define VFINDREPLACEDIALOG_H

#include <QWidget>
#include <QString>
#include "vconstants.h"

class QLineEdit;
class QPushButton;
class QCheckBox;

enum FindOption
{
    CaseSensitive = 0x1U,
    WholeWordOnly = 0x2U,
    RegularExpression = 0x4U,
    IncrementalSearch = 0x8U
};

class VFindReplaceDialog : public QWidget
{
    Q_OBJECT
public:
    explicit VFindReplaceDialog(QWidget *p_parent = 0);
    void setOption(FindOption p_opt, bool p_enabled);
    // Update the options enabled/disabled state according to current
    // edit tab.
    void updateState(DocType p_docType, bool p_editMode);

signals:
    void dialogClosed();
    void findTextChanged(const QString &p_text, uint p_options);
    void findOptionChanged(uint p_options);
    void findNext(const QString &p_text, uint p_options, bool p_forward);
    void replace(const QString &p_text, uint p_options,
                 const QString &p_replaceText, bool p_findNext);
    void replaceAll(const QString &p_text, uint p_options,
                    const QString &p_replaceText);

protected:
    void keyPressEvent(QKeyEvent *event) Q_DECL_OVERRIDE;

public slots:
    void closeDialog();
    void openDialog(QString p_text = "");
    void findNext();
    void findPrevious();
    void replace();
    void replaceFind();
    void replaceAll();

private slots:
    void handleFindTextChanged(const QString &p_text);
    void advancedBtnToggled(bool p_checked);
    void optionBoxToggled(int p_state);

private:
    void setupUI();
    // Bit OR of FindOption
    uint m_options;
    bool m_replaceAvailable;

    QLineEdit *m_findEdit;
    QLineEdit *m_replaceEdit;
    QPushButton *m_findNextBtn;
    QPushButton *m_findPrevBtn;
    QPushButton *m_replaceBtn;
    QPushButton *m_replaceFindBtn;
    QPushButton *m_replaceAllBtn;
    QPushButton *m_advancedBtn;
    QPushButton *m_closeBtn;
    QCheckBox *m_caseSensitiveCheck;
    QCheckBox *m_wholeWordOnlyCheck;
    QCheckBox *m_regularExpressionCheck;
    QCheckBox *m_incrementalSearchCheck;
};

#endif // VFINDREPLACEDIALOG_H
