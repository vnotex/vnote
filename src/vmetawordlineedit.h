#ifndef VMETAWORDLINEEDIT_H
#define VMETAWORDLINEEDIT_H

#include <QLineEdit>


// QLineEdit with meta word support.
class VMetaWordLineEdit : public QLineEdit
{
    Q_OBJECT
public:
    explicit VMetaWordLineEdit(QWidget *p_parent = nullptr);

    VMetaWordLineEdit(const QString &p_contents, QWidget *p_parent = Q_NULLPTR);

    // Return the evaluated text.
    const QString &getEvaluatedText() const;

private slots:
    void handleTextChanged(const QString &p_text);

private:
    void init();

    // We should keep the evaluated text identical with what's displayed.
    QString m_evaluatedText;
};

#endif // VMETAWORDLINEEDIT_H
