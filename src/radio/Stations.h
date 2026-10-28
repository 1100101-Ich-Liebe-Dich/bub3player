// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/Http.h"

#include <QObject>
#include <QThreadPool>
#include <QVariantList>

#include <atomic>

class Player;
class PlayQueue;

// Internet radio (QML singleton "Stations"): the user's stations and, only when
// switched on ("online"), the station directory radio-browser.info for searching
// and for the stations' logos. A station plays through the queue like a track
// (negative id, see External): all stations of the list in a row, so Next and
// Previous change the station.
//
// Adding by address: a list of streams (.pls, .m3u, .xspf, .asx – what radio
// websites hand out) is opened and its first stream taken; the station's own
// answer gives name, format and bit rate (ICY headers). Nothing is sent anywhere
// but to the station and, when switched on, radio-browser.info.
class Stations : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList list READ list NOTIFY listChanged)
    Q_PROPERTY(bool online READ online WRITE setOnline NOTIFY settingsChanged) // radio-browser.info
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)                       // adding or searching
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)      // search in the directory
    Q_PROPERTY(QString resultsText READ resultsText NOTIFY resultsChanged)

public:
    Stations(Player *player, PlayQueue *queue, QObject *parent = nullptr);
    ~Stations() override;

    QVariantList list() const { return m_list; }
    bool online() const;
    void setOnline(bool on);
    bool busy() const { return m_busy > 0; }
    QVariantList results() const { return m_results; }
    QString resultsText() const { return m_resultsText; }

    // `playNow`: play it right away (opened from the file manager or dropped on the window)
    Q_INVOKABLE void add(const QString &address, const QString &name = {}, bool playNow = false);
    Q_INVOKABLE void importFile(const QVariant &pathOrUrl, bool playNow = false);
    // A file from outside (opened, dropped): stations are added and the first plays;
    // an .m3u with anything but internet addresses is a playlist and comes back
    // through notStations().
    Q_INVOKABLE void openList(const QVariant &pathOrUrl);
    // A .m3u with only internet addresses is a list of stations, not a playlist.
    static bool isStationList(const QVariant &pathOrUrl);
    Q_INVOKABLE void rename(qint64 id, const QString &name);
    Q_INVOKABLE void remove(qint64 id);
    Q_INVOKABLE void play(qint64 id);
    Q_INVOKABLE void search(const QString &text);
    Q_INVOKABLE void addResult(int index);
    Q_INVOKABLE void clearResults();

signals:
    void listChanged();
    void settingsChanged();
    void busyChanged();
    void resultsChanged();
    void message(const QString &text);
    void notStations(const QString &path);

private:
    struct Station
    {
        QString name, url, stream, homepage, logo, tags, country, codec, uuid;
        int bitrate = 0;
    };
    void reload();
    void readList(const QString &path, bool playNow, bool orPlaylist);
    static bool isAudio(const Http::Probe &p); // a stream, not a web page or a picture
    void insert(QVector<Station> stations, bool announce, bool playFirst = false);
    void fetchLogo(qint64 id, const QString &address);
    void onTrackChanged();
    void run(std::function<void()> work); // network and database writes, one after the other
    QString directory() const;            // radio-browser.info, or a stand-in for tests
    std::function<bool()> cancelled() const; // for requests: true once the player quits

    Player *m_player;
    PlayQueue *m_queue;
    QThreadPool m_pool;
    std::atomic<int> m_searchGen{0};
    std::atomic<bool> m_closing{false}; // quitting: requests end at once
    QVariantList m_list, m_results;
    QString m_resultsText;
    int m_busy = 0;
    qint64 m_lastPlayed = 0;
};
