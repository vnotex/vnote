#ifndef VTEXTEDITCOMPLETER_H
#define VTEXTEDITCOMPLETER_H

#include <QCompleter>
#include <QAbstractItemView>

class QStringListModel;
class VEditor;

class VTextEditCompleter : public QCompleter
{
    Q_OBJECT
public:
    explicit VTextEditCompleter(QObject *p_parent = nullptr);

    bool isPopupVisible() const;

    void performCompletion(const QStringList &p_words,
                           const QString &p_prefix,
                           Qt::CaseSensitivity p_cs,
                           bool p_reversed,
                           const QRect &p_rect,
                           VEditor *p_editor);

protected:
    bool eventFilter(QObject *p_obj, QEvent *p_eve) Q_DECL_OVERRIDE;

private:
    void init();

    bool selectRow(int p_row);

    void setCurrentIndex(QModelIndex p_index, bool p_select = true);

    void selectNextCompletion(bool p_reversed);

    void insertCurrentCompletion();

    void insertCompletion(const QString &p_completion);

    void finishCompletion();

    // Revert inserted completion to prefix and finish completion.
    void cancelCompletion();

    void cleanUp();

    void updatePrefix(const QString &p_prefix);

    bool m_initialized;

    QStringListModel *m_model;

    VEditor *m_editor;

    QString m_insertedCompletion;
};

inline bool VTextEditCompleter::isPopupVisible() const
{
    return popup()->isVisible();
}

inline void VTextEditCompleter::finishCompletion()
{
    popup()->hide();
}
#endif // VTEXTEDITCOMPLETER_H
