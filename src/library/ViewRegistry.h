// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QVariantList>

// Sidebar views. Built-ins below, plus the user's own from
// ~/.config/halcyon/views.json (same format, see views.example.json).
namespace ViewRegistry {

QVariantList builtIn();
QVariantList userViews(QString *error = nullptr);
QVariantList all();
QString userFile();
void writeExampleIfMissing();

} // namespace ViewRegistry
