#include "settingsmigration.h"

#include <QMap>
#include <QSettings>
#include <QString>
#include <QVariant>

namespace {
constexpr auto MigrationMarker = "migration/notepadPlusPlusLinuxSettingsImported";
}

bool migrateLegacyQSettings()
{
    QSettings current;
    if (current.value(QString::fromLatin1(MigrationMarker), false).toBool())
        return current.status() == QSettings::NoError;
    if (current.status() != QSettings::NoError)
        return false;

    QSettings legacy(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("Notepad++"), QStringLiteral("Notepad++ Linux"));
    QMap<QString, QVariant> legacyValues;
    const QStringList keys = legacy.allKeys();
    for (const QString &key : keys)
        legacyValues.insert(key, legacy.value(key));
    if (legacy.status() != QSettings::NoError)
        return false;

    for (auto it = legacyValues.cbegin(); it != legacyValues.cend(); ++it) {
        if (!current.contains(it.key()))
            current.setValue(it.key(), it.value());
    }
    current.setValue(QString::fromLatin1(MigrationMarker), true);
    current.sync();
    return current.status() == QSettings::NoError;
}
