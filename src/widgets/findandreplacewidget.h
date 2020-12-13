#ifndef FINDANDREPLACEWIDGET_H
#define FINDANDREPLACEWIDGET_H

#include <QWidget>

#include <QVector>

#include <core/global.h>

class QLineEdit;
class QCheckBox;
class QTimer;

namespace vnotex
{
    class FindAndReplaceWidget : public QWidget
    {
        Q_OBJECT
    public:
        explicit FindAndReplaceWidget(QWidget *p_parent = nullptr);

        void setReplaceEnabled(bool p_enabled);

        void open(const QString &p_text);

        void close();

        QString getFindText() const;

        FindOptions getOptions() const;

    signals:
        void findTextChanged(const QString &p_text, FindOptions p_options);

        void findNextRequested(const QString &p_text, FindOptions p_options);

        void replaceRequested(const QString &p_text, FindOptions p_options, const QString &p_replaceText);

        void replaceAllRequested(const QString &p_text, FindOptions p_options, const QString &p_replaceText);

        void closed();

        void opened();

    protected:
        void keyPressEvent(QKeyEvent *p_event) Q_DECL_OVERRIDE;

    private slots:
        void findNext();

        void findPrevious();

        void updateFindOptions();

        void replace();

        void replaceAndFind();

        void replaceAll();

    private:
        void setupUI();

        void setFindOptions(FindOptions p_options);

        QLineEdit *m_findLineEdit = nullptr;

        QLineEdit *m_replaceLineEdit = nullptr;

        QVector<QWidget *> m_replaceRelatedWidgets;

        QCheckBox *m_caseSensitiveCheckBox = nullptr;

        QCheckBox *m_wholeWordOnlyCheckBox = nullptr;

        QCheckBox *m_regularExpressionCheckBox = nullptr;

        QCheckBox *m_incrementalSearchCheckBox = nullptr;

        FindOptions m_options = FindOption::None;

        QTimer *m_findTextTimer = nullptr;

        bool m_optionCheckBoxMuted = false;
    };
}

#endif // FINDANDREPLACEWIDGET_H
