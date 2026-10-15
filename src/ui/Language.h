#pragma once

#include <QString>

namespace fg {

/**
 * Installs FlyGlow's and Qt's translators for language ("en" or "ru") and makes it the default
 * QLocale. Every widget then receives QEvent::LanguageChange.
 * @note Persists nothing; the caller owns the "ui/language" setting.
 */
void installLanguage(const QString& language);

} // namespace fg
