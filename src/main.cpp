#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <qqml.h>
#include <QIcon>

#include "canvas/vpcanvas.h"
#include "core/tooltypes.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/icons/app.ico")));

    qmlRegisterUncreatableMetaObject(VpTools::staticMetaObject, "VanishingPoint", 1, 0, "VpTools",
                                     "VpTools 仅用于提供消失点工具枚举");
    qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/src/qml/VpTheme.qml")),
                             "VanishingPoint", 1, 0, "VpTheme");
    qmlRegisterType<VpCanvas>("VanishingPoint", 1, 0, "VpCanvas");

    // 控制器先构造、后销毁，确保 QML 引擎和画布始终引用有效对象。
    VpController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("vpController"), &controller);
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
