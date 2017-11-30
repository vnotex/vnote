#ifndef VPALETTE_H
#define VPALETTE_H

#include <QString>
#include <QHash>

class QSettings;


class VPalette
{
public:
    explicit VPalette(const QString &p_file);

    QString color(const QString &p_name) const;

    // Read QSS file.
    QString fetchQtStyleSheet() const;

private:
    void init(const QString &p_file);

    void initPaleteFromSettings(QSettings *p_settings, const QString &p_group);

    void initMetaData(QSettings *p_settings, const QString &p_group);

    void fillStyle(QString &p_style) const;

    void fillAbsoluteUrl(QString &p_style) const;

    // File path of the palette file.
    QString m_file;

    QHash<QString, QString> m_palette;

    QString m_qssFile;
};

#endif // VPALETTE_H
