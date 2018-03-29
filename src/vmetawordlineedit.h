#ifndef VMETAWORDLINEEDIT_H
#define VMETAWORDLINEEDIT_H

#include "vlineedit.h"


// VLineEdit with meta word support.
class VMetaWordLineEdit : public VLineEdit
{
    Q_OBJECT
public:
    explicit VMetaWordLineEdit(QWidget *p_parent = nullptr);

    VMetaWordLineEdit(const QString &p_contents, QWidget *p_parent = Q_NULLPTR);

    // Return the evaluated text.
    const QString &getEvaluatedText() const;

    QString evaluateText(const QString &p_text) const;

private slots:
    void handleTextChanged(const QString &p_text);

private:
    void init();

    // We should keep the evaluated text identical with what's displayed.
    QString m_evaluatedText;
};

#endif // VMETAWORDLINEEDIT_H
