#include "config_store.h"
#include "i18n.h"
#include "main_window.h"
#include "process_scanner.h"
#include "rpc_client.h"
#include "theme.h"
#include "worker_controller.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QSettings>

#include <unistd.h>

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("KristaTechMinerManager"));
    app.setOrganizationName(QStringLiteral("KristaTech"));
    app.setQuitOnLastWindowClosed(false);
    app.setWindowIcon(QIcon(QStringLiteral(":/logo.png")));

    const bool smokeTest = app.arguments().contains(QStringLiteral("--smoke-test"));

    QSettings settings(QStringLiteral("KristaTech"), QStringLiteral("MinerManager"));
    const bool isLight = settings.value(QStringLiteral("ui/lightTheme"), false).toBool();
    app.setStyleSheet(Theme::buildStylesheet(isLight));

    const QString smokeDir = QStringLiteral("/tmp/kmm_smoke_%1").arg(::getpid());
    ConfigStore store(smokeTest ? smokeDir : QString());
    RpcClient rpc;
    WorkerController controller(&store, &rpc);
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
