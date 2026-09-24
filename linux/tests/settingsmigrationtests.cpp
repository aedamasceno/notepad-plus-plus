#include "settingsmigration.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

namespace {
int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void testLegacySettingsAreImportedWithoutOverwritingCurrentValues()
{
    QSettings legacy(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("Notepad++"), QStringLiteral("Notepad++ Linux"));
    legacy.setValue(QStringLiteral("mainWindow/geometry"), QByteArray("legacy-geometry"));
    legacy.setValue(QStringLiteral("search/findHistory"), QStringList{QStringLiteral("needle")});
    legacy.sync();

    QSettings current;
    current.setValue(QStringLiteral("mainWindow/geometry"), QByteArray("current-geometry"));
    current.sync();

    expect(migrateLegacyQSettings(), "legacy settings migration should succeed");

    QSettings migrated;
    expect(migrated.value(QStringLiteral("mainWindow/geometry")).toByteArray()
               == QByteArray("current-geometry"),
           "migration must not overwrite settings already saved under the new identity");
    expect(migrated.value(QStringLiteral("search/findHistory")).toStringList()
               == QStringList{QStringLiteral("needle")},
           "migration should copy settings absent from the new identity");
    expect(migrated.value(QStringLiteral("migration/notepadPlusPlusLinuxSettingsImported")).toBool(),
           "successful migration should record its one-time marker");
}

void testCompletedMigrationDoesNotReadLegacySettingsAgain()
{
    QSettings legacy(QSettings::NativeFormat, QSettings::UserScope,
                     QStringLiteral("Notepad++"), QStringLiteral("Notepad++ Linux"));
    legacy.setValue(QStringLiteral("search/replaceHistory"),
                    QStringList{QStringLiteral("added-later")});
    legacy.sync();

    expect(migrateLegacyQSettings(), "repeat migration check should succeed");

    QSettings current;
    expect(!current.contains(QStringLiteral("search/replaceHistory")),
           "completed migration must not import settings added to the legacy store later");
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir settingsRoot;
    expect(settingsRoot.isValid(), "temporary settings directory should be available");

    QSettings::setDefaultFormat(QSettings::NativeFormat);
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settingsRoot.path());
    QCoreApplication::setOrganizationName(QStringLiteral("TextPinnacle"));
    QCoreApplication::setApplicationName(QStringLiteral("TextPinnacle"));

    testLegacySettingsAreImportedWithoutOverwritingCurrentValues();
    testCompletedMigrationDoesNotReadLegacySettingsAgain();

    if (failures == 0)
        std::puts("All Linux settings migration tests passed");
    return failures == 0 ? 0 : 1;
}
