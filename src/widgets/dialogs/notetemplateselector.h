#ifndef NOTETEMPLATESELECTOR_H
#define NOTETEMPLATESELECTOR_H

#include <QWidget>

class QComboBox;
class QPlainTextEdit;

namespace vnotex
{
    class NoteTemplateSelector : public QWidget
    {
        Q_OBJECT
    public:
        explicit NoteTemplateSelector(QWidget *p_parent = nullptr);

        QString getCurrentTemplate() const;
        bool setCurrentTemplate(const QString &p_template);

        const QString& getTemplateContent() const;

    signals:
        void templateChanged();

    private:
        void setupUI();

        void setupTemplateComboBox(QWidget *p_parent);

        void updateCurrentTemplate();

        QComboBox *m_templateComboBox = nullptr;

        QPlainTextEdit *m_templateTextEdit = nullptr;

        QString m_templateContent;
    };
}

#endif // NOTETEMPLATESELECTOR_H
