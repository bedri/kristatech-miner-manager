#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>

/**
 * Runtime bilingual translation manager supporting English (EN) and Turkish (TR).
 */
class I18n : public QObject
{
    Q_OBJECT

public:
    enum class Language {
        EN = 0,
        TR = 1
    };

    static I18n& instance();

    Language language() const { return m_language; }
    QString languageCode() const { return m_language == Language::TR ? QStringLiteral("tr") : QStringLiteral("en"); }
    void setLanguage(Language lang);
    void toggleLanguage();

    QString translate(const QString& key) const;

Q_SIGNALS:
    void languageChanged(I18n::Language lang);

private:
    explicit I18n(QObject* parent = nullptr);
    void initTable();

    Language m_language{Language::EN};
    QHash<QString, QPair<QString, QString>> m_table;
};

/** Convenience lookup helper for I18n::instance().translate(key). */
inline QString KTr(const QString& key)
{
    return I18n::instance().translate(key);
}
