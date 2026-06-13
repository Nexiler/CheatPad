#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusError>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QWindow>
#include <QScreen>
#include "AppController.h"
#include "SheetModel.h"

using namespace Qt::StringLiterals;

int main(int argc, char *argv[])
{
    // Use native Wayland for better compatibility with Hyprland
    // XWayland can cause visual artifacts with transparent/rounded windows
    // Note: Window centering may need compositor-specific handling
    
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);

    app.setOrganizationName("CheatPad");
    app.setOrganizationDomain("cheatpad.org");
    app.setApplicationName("CheatPad");
    app.setApplicationVersion("1.0.0");

    // Ensure data directory exists
    QString dataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!QDir().mkpath(dataLocation)) {
        qCritical() << "Failed to create data directory:" << dataLocation;
    }
    QString dbPath = dataLocation + "/cheatpad.db";
    qDebug() << "📂 Data location:" << dataLocation;

    AppController controller(dbPath);

    // D-Bus registration for global hotkey integration
    if (!QDBusConnection::sessionBus().registerService("org.cheatpad.service")) {
        qWarning() << "⚠️ Could not register D-Bus service (another instance might be running)";
        // Don't exit - allow running without D-Bus for testing
    } else {
        if (!QDBusConnection::sessionBus().registerObject("/", &controller, QDBusConnection::ExportAllSlots)) {
            qWarning() << "⚠️ Could not register D-Bus object";
        } else {
            qDebug() << "✅ D-Bus service registered: org.cheatpad.service";
        }
    }

    // Register QML types
    qmlRegisterType<SheetModel>("CheatPad", 1, 0, "SheetModel");

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("appController", &controller);

    const QUrl url(u"qrc:/qt/qml/CheatPad/Main.qml"_s);
    
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
                         if (!obj && url == objUrl) {
                             qCritical() << "❌ Failed to load QML";
                             QCoreApplication::exit(-1);
                         } else if (obj && url == objUrl) {
                             qDebug() << "✅ QML loaded successfully (window hidden, use D-Bus to toggle)";
                         }
                     }, Qt::QueuedConnection);

    engine.load(url);

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "❌ No root objects created";
        return -1;
    }

    qDebug() << "🚀 CheatPad started";
    return app.exec();
}
