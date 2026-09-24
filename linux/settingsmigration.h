#pragma once

// Imports settings from the former Linux application identity once. Existing
// TextPinnacle values win. Returns false only when a settings store cannot be
// read or the migrated values cannot be persisted.
bool migrateLegacyQSettings();
