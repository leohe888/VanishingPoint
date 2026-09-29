#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <qqml.h>
#include <QIcon>

#include "canvas/vpcanvas.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/icons/app.ico")));

    qmlRegisterType<VpCanvas>("VanishingPoint", 1, 0, "VpCanvas");
    qmlRegisterUncreatableType<VpController>("VanishingPoint", 1, 0, "VpController",
                                              "由 VpCanvas 提供控制器实例");

    QQmlApplicationEngine engine;
    const QUrl url(QStringLiteral("qrc:/src/qml/main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl)
                QCoreApplication::exit(-1);
        },
        Qt::QueuedConnection);
    engine.load(url);

    return QGuiApplication::exec();
}
