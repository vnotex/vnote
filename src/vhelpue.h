#ifndef VHELPUE_H
#define VHELPUE_H

#include "iuniversalentry.h"

class VListWidget;

class VHelpUE : public IUniversalEntry
{
    Q_OBJECT
public:
    explicit VHelpUE(QObject *p_parent = nullptr);

    QString description(int p_id) const Q_DECL_OVERRIDE;

    QWidget *widget(int p_id) Q_DECL_OVERRIDE;

    void processCommand(int p_id, const QString &p_cmd) Q_DECL_OVERRIDE;

    void clear(int p_id) Q_DECL_OVERRIDE;

protected:
    void init() Q_DECL_OVERRIDE;

private:
    bool initListWidget();

    VListWidget *m_listWidget;
};

#endif // VHELPUE_H
