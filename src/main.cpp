// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysis/AnalysisService.h"
#include "analysis/AudioAnalyzer.h"
#include "core/CoverProvider.h"
#include "core/Database.h"
#include "core/IconProvider.h"
#include "core/Scanner.h"
#include "library/FolderModel.h"
#include "library/GroupModel.h"
#include "jellyfin/Jellyfin.h"
#include "library/Library.h"
#include "library/PlaylistStore.h"
#include <QSettings>
#include "library/QueryBuilder.h"
#include "library/TrackListModel.h"
#include "player/AudioOutput.h"
#include "player/Lyrics.h"
#include "player/Mpris.h"
#include "player/PlayQueue.h"
#include "ui/Memory.h"
#include "player/Player.h"
#include "recommend/Recommender.h"
#include "ui/SystemDialogs.h"
#include "ui/SingleInstance.h"
#include "ui/Theme.h"
#include "ui/Tray.h"

#include <QCommandLineParser>
#include <QDir>
#include <atomic>
#include <QUrl>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <cstdio>
#include <gst/gst.h>

namespace {

// halcyon --scan DIR [DIR...] [--db FILE] [--threads N]
// Headless scan with timings: lets you measure the scanner on your own hardware.
int runHeadlessScan(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("halcyon"));
    QCommandLineParser p;
    p.addOption({QStringLiteral("scan"), QStringLiteral("Ordner scannen und beenden.")});
    p.addOption({QStringLiteral("db"), QStringLiteral("Datenbankdatei."), QStringLiteral("file")});
    p.addOption({QStringLiteral("threads"), QStringLiteral("Lese-Threads (0 = automatisch)."), QStringLiteral("n"), QStringLiteral("0")});
    p.addPositionalArgument(QStringLiteral("dirs"), QStringLiteral("Musikordner"));
    p.process(app);
    if (p.isSet(QStringLiteral("db")))
        Database::setPath(p.value(QStringLiteral("db")));
    QStringList roots;
    for (const QString &d : p.positionalArguments())
        roots << QDir(d).absolutePath();
    if (roots.isEmpty()) {
        std::fprintf(stderr, "Benutzung: halcyon --scan ORDNER [...] [--db DATEI] [--threads N]\n");
        return 2;
    }
    Database::migrate();
    std::printf("Datenbank: %s\n", qPrintable(Database::path()));
    Scanner scanner;
    const Scanner::Stats s = scanner.runBlocking(roots, p.value(QStringLiteral("threads")).toInt(), true);
    std::printf("Gesamt: %.2f s\n", s.totalSec);
    for (const QString &m : s.missingRoots)
        std::printf("Nicht erreichbar: %s\n", qPrintable(m));
    return 0;
}

// halcyon --analyze FILE [FILE...]
// Audio analysis without the UI: tempo, key, loudness, transitions, timing.
int runHeadlessAnalyze(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    gst_init(&argc, &argv);
    std::atomic<bool> cancel{false};
    int files = 0;
    double total = 0;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--analyze") == 0)
            continue;
        const QString path = QDir(QString::fromLocal8Bit(argv[i])).absolutePath();
        QElapsedTimer t;
        t.start();
        const AudioAnalyzer::Result r = AudioAnalyzer::analyse(QUrl::fromLocalFile(path).toEncoded(), cancel);
        const double sec = t.elapsed() / 1000.0;
        total += sec;
        ++files;
        if (!r.ok) {
            std::printf("%s\n  nicht lesbar (%.2f s)\n", qPrintable(path), sec);
            continue;
        }
        std::printf("%s\n  %.1f BPM (sicher %.2f, erster Schlag %lld ms) | %s %s (sicher %.2f)\n"
                    "  %.1f LUFS, LRA %.1f LU, Spitze %.1f dBFS | Energie %.2f, Helligkeit %.0f Hz, %.1f Anschläge/s\n"
                    "  Länge %lld ms, Klang %lld..%lld ms, Intro bis %lld ms, Outro ab %lld ms | %.2f s\n",
                    qPrintable(path), r.bpm, r.bpmConf, (long long)r.beatMs,
                    qPrintable(AudioAnalyzer::camelot(r.key, r.minor)), qPrintable(AudioAnalyzer::keyName(r.key, r.minor)), r.keyConf,
                    r.loudness, r.lra, r.peak, r.energy, r.brightness, r.onsetRate,
                    (long long)r.lengthMs, (long long)r.startMs, (long long)r.endMs, (long long)r.introMs, (long long)r.outroMs, sec);
    }
    if (files > 1)
        std::printf("%d Dateien in %.2f s (%.2f s pro Datei)\n", files, total, total / files);
    return 0;
}

// halcyon --lyrics FILE [FILE...]
// The lyrics a file brings along (.lrc next to it, tags), as the big view uses them.
int runHeadlessLyrics(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--lyrics") == 0)
            continue;
        const QString path = QDir(QString::fromLocal8Bit(argv[i])).absolutePath();
        const LyricsText t = Lyrics::forFile(path);
        if (t.isEmpty()) {
            std::printf("%s\n  kein Liedtext\n", qPrintable(path));
            continue;
        }
        const char *from = t.source == QLatin1String("lrc") ? ".lrc-Datei" : "Tags";
        std::printf("%s\n  %s, %s, %d %s\n", qPrintable(path), from, t.synced() ? "synchron" : "nicht synchron",
                    int(t.lines.size()), t.lines.size() == 1 ? "Zeile" : "Zeilen");
        for (int l = 0; l < t.lines.size(); ++l) {
            if (t.synced())
                std::printf("  [%02lld:%02lld.%02lld] %s\n", (long long)(t.times[l] / 60000), (long long)(t.times[l] / 1000 % 60),
                            (long long)(t.times[l] % 1000 / 10), qPrintable(QString(t.lines[l]).replace(QLatin1Char('\n'), QLatin1String(" / "))));
            else
                std::printf("  %s\n", qPrintable(t.lines[l]));
        }
    }
    return 0;
}

// halcyon --recommend TRACK_ID [TRACK_ID...] [--db FILE] [--count N] [--mode 0|1|2]
// What the recommendations would pick after these tracks (for tuning and bug reports).
int runHeadlessRecommend(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("halcyon"));
    QCommandLineParser p;
    p.addOption({QStringLiteral("recommend"), QStringLiteral("Empfehlungen zeigen.")});
    p.addOption({QStringLiteral("db"), QStringLiteral("Datenbankdatei."), QStringLiteral("file")});
    p.addOption({QStringLiteral("count"), QStringLiteral("Anzahl."), QStringLiteral("n"), QStringLiteral("15")});
    p.addOption({QStringLiteral("mode"), QStringLiteral("0 weiter, 1 ähnlich, 2 nur neue."), QStringLiteral("m"), QStringLiteral("0")});
    p.addOption({QStringLiteral("automix"), QStringLiteral("Tempo und Tonart stärker beachten.")});
    p.addOption({QStringLiteral("relearn"), QStringLiteral("Vorher alles neu aus dem Hörverlauf lernen.")});
    p.addPositionalArgument(QStringLiteral("ids"), QStringLiteral("Titel-IDs"));
    p.process(app);
    if (p.isSet(QStringLiteral("db")))
        Database::setPath(p.value(QStringLiteral("db")));
    Database::migrate();
    Recommender::Context c;
    for (const QString &a : p.positionalArguments())
        c.seeds << a.toLongLong();
    c.count = p.value(QStringLiteral("count")).toInt();
    c.mode = p.value(QStringLiteral("mode")).toInt();
    c.automix = p.isSet(QStringLiteral("automix"));
    DbConnection db = Database::open();
    if (p.isSet(QStringLiteral("relearn")))
        Recommender::relearn(db);
    {
        Stmt src(db, "SELECT id FROM sources WHERE kind = 'jellyfin'");
        while (src.step())
            c.online << src.i64(0); // offline here: no way to know, take them all
    }
    auto show = [&](qint64 id, const char *prefix) {
        Stmt s(db, "SELECT t.title, t.artist, t.genre, t.year, a.bpm, a.key, a.minor, a.energy FROM tracks t "
                   "LEFT JOIN analysis a ON a.track_id = t.id WHERE t.id = ?");
        s.bind(1, id);
        if (s.step())
            std::printf("%s%6lld  %-28s %-22s %-14s %4d  %5.1f BPM %-3s E %.2f\n", prefix, (long long)id, qPrintable(s.str(0).left(28)),
                        qPrintable(s.str(1).left(22)), qPrintable(s.str(2).left(14)), s.i32(3), s.dbl(4),
                        qPrintable(AudioAnalyzer::camelot(s.isNull(5) ? -1 : s.i32(5), s.i32(6))), s.dbl(7));
    };
    for (qint64 id : c.seeds)
        show(id, "Start ");
    QElapsedTimer t;
    t.start();
    const QVector<qint64> picks = Recommender::pick(c);
    const qint64 ms = t.elapsed();
    for (qint64 id : picks)
        show(id, "      ");
    std::printf("%d Titel in %lld ms\n", int(picks.size()), (long long)ms);
    return 0;
}

} // namespace

int main(int argc, char *argv[])
{
    Memory::configureAllocator();
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--scan") == 0)
            return runHeadlessScan(argc, argv);
        else if (qstrcmp(argv[i], "--analyze") == 0)
            return runHeadlessAnalyze(argc, argv);
        else if (qstrcmp(argv[i], "--recommend") == 0)
            return runHeadlessRecommend(argc, argv);
        else if (qstrcmp(argv[i], "--lyrics") == 0)
            return runHeadlessLyrics(argc, argv);

    gst_init(&argc, &argv);

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("halcyon"));
    QGuiApplication::setOrganizationName(QStringLiteral("halcyon"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("BuB3Player"));
    QGuiApplication::setApplicationVersion(QStringLiteral(HALCYON_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("de.bauenundbooten.BuB3Player"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("de.bauenundbooten.BuB3Player"),
                                                    QIcon(QStringLiteral(":/icons/de.bauenundbooten.BuB3Player.svg"))));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("BuB3Player – Musikplayer für große Bibliotheken. Projekt präsentiert von www.BAUENUNDBOOTEN.de"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("db"), QStringLiteral("Andere Datenbankdatei verwenden."), QStringLiteral("file")});
    parser.addOption({QStringLiteral("add-folder"), QStringLiteral("Musikordner zur Bibliothek hinzufügen."), QStringLiteral("dir")});
    parser.addOption({QStringLiteral("view"), QStringLiteral("Mit dieser Ansicht starten (z. B. albums)."), QStringLiteral("id")});
    parser.addOption({QStringLiteral("skin"), QStringLiteral("Theme wählen: klassik, soft, glas, aurora, vintage, synthwave, tower, terminal."), QStringLiteral("id")});
    parser.addOption({QStringLiteral("theme"), QStringLiteral("Farbschema des Themes wählen (z. B. tiefsee)."), QStringLiteral("id")});
    parser.addOption({QStringLiteral("skin-opt"), QStringLiteral("Theme-Einstellung setzen, z. B. material=carbon (mehrfach möglich)."), QStringLiteral("key=value")});
    parser.addOption({QStringLiteral("no-scan"), QStringLiteral("Beim Start nicht nach Änderungen suchen.")});
    parser.addOption({QStringLiteral("screenshot"), QStringLiteral("Fenster nach N ms als Bild speichern und beenden (Tests)."),
                      QStringLiteral("file")});
    parser.addOption({QStringLiteral("screenshot-delay"), QStringLiteral("Verzögerung für --screenshot in ms."), QStringLiteral("ms"),
                      QStringLiteral("2500")});
    parser.addOption({QStringLiteral("ui-test"), QStringLiteral("Testaktion nach dem Start (now-playing, queue, settings, album, play)."),
                      QStringLiteral("action")});
    parser.addOption({QStringLiteral("scan"), QStringLiteral("Ordner ohne Oberfläche scannen und Zeiten ausgeben.")});
    parser.addOption({QStringLiteral("analyze"), QStringLiteral("Dateien ohne Oberfläche analysieren (Tempo, Tonart, Lautheit …).")});
    parser.addOption({QStringLiteral("lyrics"), QStringLiteral("Liedtext von Dateien zeigen (.lrc daneben, Tags).")});
    parser.addPositionalArgument(QStringLiteral("dateien"), QStringLiteral("Musikdateien oder Ordner zum Abspielen."),
                                 QStringLiteral("[dateien…]"));
    parser.process(app);

    if (parser.isSet(QStringLiteral("db")))
        Database::setPath(parser.value(QStringLiteral("db")));
    // A second start (file manager, starter): the running player takes the files
    // or just comes to the front.
    const QStringList openFiles = parser.positionalArguments();
    switch (SingleInstance::forward(openFiles)) {
    case SingleInstance::NotRunning:
        break;
    case SingleInstance::NoAnswer:
        std::fprintf(stderr, "BuB3Player läuft schon, antwortet aber gerade nicht. Bitte gleich noch einmal versuchen.\n");
        return 1;
    case SingleInstance::Forwarded:
        for (const char *opt : {"add-folder", "view", "skin", "theme", "skin-opt", "ui-test", "screenshot", "no-scan"})
            if (parser.isSet(QLatin1String(opt))) {
                std::fprintf(stderr, "BuB3Player läuft schon; Optionen wirken nur beim ersten Start.\n");
                break;
            }
        return 0;
    }
    // From here on a second start comes to this one (files wait for the window).
    SingleInstance instance(openFiles);
    try {
        Database::migrate();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }

    // Bundled fonts for all skins (static weights, SIL OFL).
    for (const QString &f : QDir(QStringLiteral(":/fonts")).entryList({QStringLiteral("*.ttf")}, QDir::Files))
        QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/") + f);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Before anything counts tracks: hide Jellyfin copies of local tracks?
    QueryBuilder::setHideDuplicates(QSettings().value(QStringLiteral("jellyfin/preferLocal"), true).toBool());

    AnalysisService analysis;
    PlayQueue queue;
    AudioOutput audio;
    Player player(&queue, &audio);
    Lyrics lyrics(&player);
    SystemDialogs dialogs;
    Library library(&player, &queue);
    PlaylistStore playlists(&queue);
    QObject::connect(&playlists, &PlaylistStore::playlistsChanged, &library, &Library::reloadViews);
    QObject::connect(&playlists, &PlaylistStore::message, &library, &Library::message);
    QObject::connect(&audio, &AudioOutput::message, &library, &Library::message);
    library.reloadViews();
    Theme theme;
    Memory memory;
    analysis.setScanner(library.scanner());
    Recommender recommender(&player, &queue, &library);
    Jellyfin jellyfin(&player, &library);
    Tray tray(&player, &library);
    QObject::connect(&jellyfin, &Jellyfin::message, &library, &Library::message);
    QObject::connect(library.scanner(), &Scanner::finished, &jellyfin, &Jellyfin::localLibraryChanged);
    if (parser.isSet(QStringLiteral("skin")))
        theme.setSkin(parser.value(QStringLiteral("skin")));
    if (parser.isSet(QStringLiteral("theme")))
        theme.setTheme(parser.value(QStringLiteral("theme")));
    for (const QString &kv : parser.values(QStringLiteral("skin-opt"))) {
        const QString key = kv.section(QLatin1Char('='), 0, 0);
        const QString val = kv.section(QLatin1Char('='), 1);
        bool isNum = false;
        const double num = val.toDouble(&isNum);
        if (val == QLatin1String("true") || val == QLatin1String("false"))
            theme.setOption(key, val == QLatin1String("true"));
        else if (isNum && theme.opt().value(key).typeId() == QMetaType::Double)
            theme.setOption(key, num);
        else
            theme.setOption(key, val);
    }
    QObject::connect(&player, &Player::coverColorChanged, &theme, [&] {
        theme.setCoverColor(player.coverColor());
        theme.setCoverPalette(player.coverPalette());
    });

    QFont font(theme.uiFont());
    font.setPixelSize(14);
    QGuiApplication::setFont(font);

    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Library", &library);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Player", &player);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Queue", &queue);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Theme", &theme);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Memory", &memory);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Jellyfin", &jellyfin);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Analysis", &analysis);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Recommend", &recommender);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Playlists", &playlists);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Audio", &audio);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Lyrics", &lyrics);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "SystemDialogs", &dialogs);
    qmlRegisterSingletonInstance("Halcyon", 1, 0, "Tray", &tray);
    qmlRegisterType<TrackListModel>("Halcyon", 1, 0, "TrackListModel");
    qmlRegisterType<GroupModel>("Halcyon", 1, 0, "GroupModel");
    qmlRegisterType<FolderModel>("Halcyon", 1, 0, "FolderModel");
    qmlRegisterUncreatableType<Scanner>("Halcyon", 1, 0, "Scanner", QStringLiteral("Library.scanner verwenden"));

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("cover"), new CoverProvider);
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider);
    engine.rootContext()->setContextProperty(QStringLiteral("startView"), parser.value(QStringLiteral("view")));
    engine.rootContext()->setContextProperty(QStringLiteral("uiTest"), parser.value(QStringLiteral("ui-test")));
    engine.load(QUrl(QStringLiteral("qrc:/qml/Main.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());

    memory.setWindow(window);
    Mpris::install(&player, &queue, window);
    tray.setWindow(window);
    queue.restore();
    player.restoreSession();

    if (parser.isSet(QStringLiteral("add-folder")))
        library.addRoot(QDir(parser.value(QStringLiteral("add-folder"))).absolutePath());
    else
        QTimer::singleShot(800, &library, [&library, scan = !parser.isSet(QStringLiteral("no-scan"))] { library.startupScan(scan); });

    // after the restored queue (opened files replace it) and the added folder
    QObject::connect(&instance, &SingleInstance::openRequested, window, [window](const QStringList &paths) {
        QMetaObject::invokeMethod(window, "openPaths", Q_ARG(QVariant, QVariant(paths)));
    });
    instance.setWindow(window);

    if (parser.isSet(QStringLiteral("screenshot")) && window) {
        const QString file = parser.value(QStringLiteral("screenshot"));
        QTimer::singleShot(parser.value(QStringLiteral("screenshot-delay")).toInt(), window, [window, file] {
            window->grabWindow().save(file);
            QCoreApplication::quit();
        });
    }

    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&] {
        library.scanner()->cancel();
        library.finishWrites(); // hearts clicked just before: into the database and to Jellyfin
        jellyfin.shutdown();
        queue.save();
        player.saveSession();
    });
    return app.exec();
}
