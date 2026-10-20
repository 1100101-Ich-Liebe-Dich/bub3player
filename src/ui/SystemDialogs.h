// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QDBusContext>
#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

// The desktop's own file and folder dialogs (QML singleton "SystemDialogs"):
//  1. the XDG desktop portal (GNOME, KDE, Xfce … – the file manager's dialog),
//  2. otherwise kdialog or zenity, if installed,
//  3. otherwise none: the callback gets null and the caller opens Qt's dialog.
// The callback gets the chosen path, "" when cancelled.
//
// filters: [{ name: "Playlists", patterns: ["*.m3u", "*.m3u8"] }, …]
class SystemDialogs : public QObject, protected QDBusContext
{
    Q_OBJECT

public:
    explicit SystemDialogs(QObject *parent = nullptr);

    // startFolder: path or file URL; empty = the music folder (or home)
    Q_INVOKABLE void chooseFolder(const QString &title, const QVariant &startFolder, const QJSValue &done);
    Q_INVOKABLE void openFile(const QString &title, const QVariant &startFolder, const QVariantList &filters, const QJSValue &done);
    Q_INVOKABLE void saveFile(const QString &title, const QVariant &startFolder, const QString &name, const QVariantList &filters,
                              const QJSValue &done);

private slots:
    void portalResponse(uint response, const QVariantMap &results);

private:
    enum Kind { Folder, Open, Save };
    struct Request
    {
        Kind kind = Folder;
        QString title, folder, name;
        QVariantList filters;
        QJSValue done;
    };
    void viaPortal(const Request &r);
    void viaTool(const Request &r, int toolIndex);
    void finish(Request r, const QVariant &result); // QString path, or null: no dialog here

    QHash<QString, Request> m_waiting; // portal request object path -> request
    int m_counter = 0;
};
