#include "core/CoverProvider.h"
#include "core/Database.h"
#include "core/IconProvider.h"
#include "core/Scanner.h"
#include "library/FolderModel.h"
#include "library/GroupModel.h"
#include "library/Library.h"
#include "library/TrackListModel.h"
#include "player/Mpris.h"
#include "player/PlayQueue.h"
#include "ui/Memory.h"
#include "player/Player.h"
#include "ui/Theme.h"

#include <QCommandLineParser>
#include <QDir>
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

} // namespace

int main(int argc, char *argv[])
{
    Memory::configureAllocator();
    for (int i = 1; i < argc; ++i)
        if (qstrcmp(argv[i], "--scan") == 0)
            return runHeadlessScan(argc, argv);

    gst_init(&argc, &argv);

    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("halcyon"));
    QGuiApplication::setOrganizationName(QStringLiteral("halcyon"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("BuB3Player"));
    QGuiApplication::setDesktopFileName(QStringLiteral("halcyon"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("audio-x-generic")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("BuB3Player – Musikplayer für große Bibliotheken. Projekt präsentiert von www.BAUENUNDBOOTEN.de"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("db"), QStringLiteral("Andere Datenbankdatei verwenden."), QStringLiteral("file")});
    parser.addOption({QStringLiteral("add-folder"), QStringLiteral("Musikordner zur Bibliothek hinzufügen."), QStringLiteral("dir")});
    parser.addOption({QStringLiteral("view"), QStringLiteral("Mit dieser Ansicht starten (z. B. albums)."), QStringLiteral("id")});
    parser.addOption({QStringLiteral("skin"), QStringLiteral("Theme wählen: klassik, aurora, synthwave, studio."), QStringLiteral("id")});
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
    parser.process(app);

    if (parser.isSet(QStringLiteral("db")))
        Database::setPath(parser.value(QStringLiteral("db")));
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

    PlayQueue queue;
    Player player(&queue);
    Library library(&player, &queue);
    Theme theme;
    Memory memory;
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
    queue.restore();
    player.restoreSession();

    if (parser.isSet(QStringLiteral("add-folder")))
        library.addRoot(parser.value(QStringLiteral("add-folder")));
    else if (!parser.isSet(QStringLiteral("no-scan")))
        QTimer::singleShot(800, &library, &Library::startupScan);

    if (parser.isSet(QStringLiteral("screenshot")) && window) {
        const QString file = parser.value(QStringLiteral("screenshot"));
        QTimer::singleShot(parser.value(QStringLiteral("screenshot-delay")).toInt(), window, [window, file] {
            window->grabWindow().save(file);
            QCoreApplication::quit();
        });
    }

    QObject::connect(&app, &QGuiApplication::aboutToQuit, [&] {
        library.scanner()->cancel();
        queue.save();
        player.saveSession();
    });
    return app.exec();
}
