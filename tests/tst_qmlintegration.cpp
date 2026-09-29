#include "canvas/vpcanvas.h"
#include "testhelpers.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <qqml.h>

class QmlIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void redoShortcutCallsRedo() {
        qmlRegisterUncreatableMetaObject(VpTools::staticMetaObject,"VanishingPoint",1,0,"VpTools","Enums only");
        qmlRegisterType<VpCanvas>("VanishingPoint",1,0,"VpCanvas");
        VpController controller;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("vpController", &controller);
        engine.load(QUrl("qrc:/src/qml/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *root=engine.rootObjects().first(); root->setProperty("visible",false);
        auto *canvas=root->findChild<VpCanvas*>(); QVERIFY(canvas);
        QCOMPARE(canvas->controller(), &controller);
        QCOMPARE(QQmlEngine::objectOwnership(&controller), QQmlEngine::CppOwnership);
        auto *window = qobject_cast<QQuickWindow *>(root); QVERIFY(window);
        const auto toolButton = [window](VpTools::Tool tool) -> QObject * {
            const auto find = [tool](auto &&self, QQuickItem *item) -> QQuickItem * {
                if (item->property("toolId").isValid() && item->property("toolId").toInt() == tool)
                    return item;
                for (auto *child : item->childItems())
                    if (auto *found = self(self, child))
                        return found;
                return nullptr;
            };
            return find(find, window->contentItem());
        };
        for (auto tool : {VpTools::CreatePlane, VpTools::EditPlane, VpTools::Marquee, VpTools::CloneStamp,
                          VpTools::Brush, VpTools::Transform, VpTools::Hand, VpTools::Zoom})
            QVERIFY(toolButton(tool));
        auto *brush = toolButton(VpTools::Brush);
        auto *transform = toolButton(VpTools::Transform);
        QVERIFY(brush); QVERIFY(transform);
        QObject *brushShortcut = nullptr;
        for (auto *object : brush->findChildren<QObject*>())
            if (object->property("sequence").toString() == "B")
                brushShortcut = object;
        QVERIFY(brushShortcut);
        QVERIFY(!brushShortcut->property("enabled").toBool());
        QVERIFY(!brush->property("enabled").toBool());
        QVERIFY(!transform->property("enabled").toBool());
        QTemporaryDir dir; auto path=dir.filePath("background.png");
        QImage image(64,64,QImage::Format_ARGB32); image.fill(Qt::white); QVERIFY(image.save(path));
        QVERIFY(canvas->controller()->openImage(QUrl::fromLocalFile(path)));
        image=QImage(8,8,QImage::Format_ARGB32); image.fill(Qt::red);
        auto &doc=canvas->controller()->document(); doc.addFloatingImage(image); canvas->controller()->undo();
        QVERIFY(doc.floatingImages().isEmpty()); QVERIFY(canvas->controller()->canRedo());
        QObject *redo=nullptr;
        for(auto *object:root->findChildren<QObject*>())
            if(object->property("sequences").toStringList().contains("Ctrl+Y")) redo=object;
        QVERIFY(redo); QVERIFY(QMetaObject::invokeMethod(redo,"activated",Qt::DirectConnection));
        QCOMPARE(doc.floatingImages().size(),1); QVERIFY(!canvas->controller()->canRedo());
        QVERIFY(canvas->controller()->canUndo());
        QVERIFY(transform->property("enabled").toBool());
        doc.removeFloatingImage(0);
        QVERIFY(!transform->property("enabled").toBool());
        doc.beginEdit(); doc.appendPlane(makeTestPlane()); doc.commitEdit(true);
        QVERIFY(brush->property("enabled").toBool());
        QVERIFY(brushShortcut->property("enabled").toBool());
        canvas->controller()->undo();
        QVERIFY(!brush->property("enabled").toBool());
        QVERIFY(!brushShortcut->property("enabled").toBool());
        canvas->controller()->redo();
        QVERIFY(brush->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(brushShortcut, "activated", Qt::DirectConnection));
        QCOMPARE(controller.tool(), VpTools::Brush);
        QVERIFY(brush->property("selected").toBool());
        QObject *brushOptions = nullptr;
        for (auto *object : root->findChildren<QObject *>()) {
            if (object->property("brushOptionsVisible").isValid()) {
                brushOptions = object;
                break;
            }
        }
        QVERIFY(brushOptions);
        QVERIFY(brushOptions->property("brushOptionsVisible").toBool());
        QObject *strokeOptions = nullptr;
        for (auto *object : brushOptions->findChildren<QObject *>()) {
            if (object->metaObject()->indexOfSignal("diameterEdited(int)") >= 0) {
                strokeOptions = object;
                break;
            }
        }
        QVERIFY(strokeOptions);
        controller.setBrushDiameter(37);
        QCOMPARE(strokeOptions->property("diameter").toInt(), 37);
        QVERIFY(QMetaObject::invokeMethod(strokeOptions, "diameterEdited", Qt::DirectConnection,
                                         Q_ARG(int, 51)));
        QCOMPARE(controller.brushDiameter(), 51);
        engine.collectGarbage();
        QCOMPARE(QQmlEngine::objectOwnership(&controller), QQmlEngine::CppOwnership);
        QVERIFY(canvas->controller()->openImage(QUrl::fromLocalFile(path)));
        QCOMPARE(canvas->controller()->tool(), VpTools::CreatePlane);
        QVERIFY(!brush->property("enabled").toBool());
        // 重建视图与引擎，不清空应用层控制器的文档和历史。
        controller.document().beginEdit(); controller.document().appendPlane(makeTestPlane());
        controller.document().commitEdit(true);
        delete root;
        QCOMPARE(controller.document().planes().size(), 1);
        QVERIFY(controller.canUndo());
        QQmlApplicationEngine recreated;
        recreated.rootContext()->setContextProperty("vpController", &controller);
        recreated.load(QUrl("qrc:/src/qml/main.qml"));
        QVERIFY(!recreated.rootObjects().isEmpty());
        auto *newRoot = recreated.rootObjects().first(); newRoot->setProperty("visible", false);
        auto *newCanvas = newRoot->findChild<VpCanvas*>(); QVERIFY(newCanvas);
        QCOMPARE(newCanvas->controller(), &controller);
        QCOMPARE(QQmlEngine::objectOwnership(&controller), QQmlEngine::CppOwnership);
        QCOMPARE(controller.document().planes().size(), 1);
        newCanvas->undo(); QVERIFY(controller.document().planes().isEmpty());
    }
};
QTEST_MAIN(QmlIntegrationTest)
#include "tst_qmlintegration.moc"
