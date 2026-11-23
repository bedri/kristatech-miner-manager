#include "config_store.h"
#include "i18n.h"
#include "main_window.h"
#include "process_scanner.h"
#include "rpc_client.h"
#include "theme.h"
#include "worker_controller.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QSettings>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("KristaTechMinerManager"));
    app.setApplicationVersion(QStringLiteral(KMM_APP_VERSION));
    app.setOrganizationName(QStringLiteral("KristaTech"));
    QGuiApplication::setDesktopFileName(QStringLiteral("kristatech-miner-manager"));
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(Theme::appIcon());

    const bool smokeTest = app.arguments().contains(QStringLiteral("--smoke-test"));

    QSettings settings(QStringLiteral("KristaTech"), QStringLiteral("MinerManager"));
    const bool isLight = settings.value(QStringLiteral("ui/lightTheme"), false).toBool();
    const bool autoStopDeferred = settings.value(QStringLiteral("behavior/autoStopDeferred"), false).toBool();
    Theme::applyTheme(&app, isLight);

    const QString smokeDir = QDir(QDir::tempPath()).filePath(
        QStringLiteral("kmm_smoke_%1").arg(QCoreApplication::applicationPid()));
    ConfigStore store(smokeTest ? smokeDir : QString());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
    controller.setAutoStopOnDeferred(autoStopDeferred);
    controller.loadInitialState(/*autoDiscoverIfEmpty=*/!smokeTest);

    MainWindow win(&store, &rpc, &controller, isLight);
    if (smokeTest) {
        win.resize(1200, 760);
        I18n::instance().toggleLanguage();
        I18n::instance().toggleLanguage();
        (void)ProcessScanner::defaultCpuMinerPath();
        (void)ProcessScanner::defaultCcMinerPath();
        win.setLightTheme(!isLight);
        win.setLightTheme(isLight);
        QDir(smokeDir).removeRecursively();
        return 0;
    }

    win.show();
    return app.exec();
}
