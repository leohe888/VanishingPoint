#include "canvas/vpcanvas.h"
#include "testhelpers.h"
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QDir>
#include <QtTest>
#include <memory>

// Exercise the real demo navigation and hint components in a small host.
class QmlBoundaryTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qmlRegisterUncreatableMetaObject(VpTools::staticMetaObject, "VanishingPoint", 1, 0,
                                         "VpTools", "Enums only");
        qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/src/qml/VpTheme.qml")), "VanishingPoint", 1, 0, "VpTheme");
        qmlRegisterType<VpCanvas>("VanishingPoint", 1, 0, "VpCanvas");
    }

    void canvasBindingsNavigationAndRecreation()
    {
        QTemporaryDir skin;
        QVERIFY(skin.isValid());
        QVERIFY(QDir(skin.path()).mkdir("icon"));
        QImage icon(2, 2, QImage::Format_ARGB32); icon.fill(Qt::white);
        QVERIFY(icon.save(skin.filePath("icon/downlist.png")));
        VpController controller;
        QImage image(1000, 800, QImage::Format_ARGB32); image.fill(Qt::white);
        QVERIFY(controller.document().setImage(image));
        QQmlEngine engine;
        qRegisterMetaType<QList<QQmlError>>("QList<QQmlError>");
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.rootContext()->setContextProperty("vpController", &controller);
        QQmlComponent component(&engine);
        component.loadUrl(QUrl::fromLocalFile(QStringLiteral(VP_TEST_DIR) + "/qml/IntegrationHarness.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        auto *canvas = root->findChild<VpCanvas *>("canvas");
        QVERIFY(canvas);
        QCOMPARE(canvas->controller(), &controller);
        QCOMPARE(QQmlEngine::objectOwnership(&controller), QQmlEngine::CppOwnership);
        canvas->setZoom(1);
        auto *navigation = root->findChild<QObject *>("navigation");
        QVERIFY(navigation);
        QCOMPARE(navigation->property("size").toReal(), canvas->horizontalSize());
        QVERIFY(QMetaObject::invokeMethod(navigation, "moved", Q_ARG(qreal, .25)));
        QCOMPARE(canvas->horizontalPosition(), .25);
        QCOMPARE(navigation->property("position").toReal(), .25);
        controller.document().addFloatingSelection(makeTestSelection(icon));
        canvas->undo();
        QVERIFY(controller.document().floatingSelections().isEmpty());
        auto *redo = root->findChild<QObject *>("redo");
        QVERIFY(redo);
        QVERIFY(QMetaObject::invokeMethod(redo, "activated"));
        QCOMPARE(controller.document().floatingSelections().size(), 1);
        root.reset();
        QCOMPARE(controller.document().floatingSelections().size(), 1);
        root.reset(component.create());
        QVERIFY(root != nullptr);
        canvas = root->findChild<VpCanvas *>("canvas");
        QVERIFY(canvas);
        QCOMPARE(canvas->controller(), &controller);
        canvas->undo();
        QVERIFY(controller.document().floatingSelections().isEmpty());
        engine.collectGarbage();
        QCOMPARE(QQmlEngine::objectOwnership(&controller), QQmlEngine::CppOwnership);
        QVERIFY(warnings.isEmpty());
    }

    void hintBarTemporarilyReplacesInstructionsAndRestoresCurrentHint()
    {
        VpController controller;
        QImage image(400, 300, QImage::Format_ARGB32); image.fill(Qt::white);
        QVERIFY(controller.document().setImage(image));
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.rootContext()->setContextProperty("vpController", &controller);
        QQmlComponent component(&engine);
        component.loadUrl(QUrl::fromLocalFile(QStringLiteral(VP_TEST_DIR) + "/qml/HintBarHarness.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window;
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        auto *rootItem = qobject_cast<QQuickItem *>(root.get());
        QVERIFY(rootItem);
        rootItem->setParentItem(window.contentItem());
        window.resize(900, 40);
        window.show();
        auto *bar = root->findChild<QObject *>("hintBar");
        auto *caption = root->findChild<QObject *>("vanishingPointHintText");
        QVERIFY(bar && caption);
        QCOMPARE(caption->property("text").toString(), controller.toolHint());

        controller.copySelection();
        QVERIFY(!bar->property("temporaryMessage").toString().isEmpty());
        QVERIFY(bar->property("temporaryError").toBool());
        QCOMPARE(caption->property("color").value<QColor>(), QColor("#E60012"));
        QCOMPARE(caption->property("text").toString(), bar->property("temporaryMessage").toString());
        QVERIFY(caption->property("text").toString() != controller.toolHint());
        rootItem->setWidth(320);
        QTRY_VERIFY(caption->property("width").toReal() > 0);
        QTRY_VERIFY(caption->property("x").toReal() + caption->property("width").toReal() <= 310.5);
        rootItem->setWidth(900);
        QTRY_VERIFY(caption->property("width").toReal() > 800);
        controller.undo();
        QVERIFY(!bar->property("temporaryError").toBool());
        QCOMPARE(caption->property("color").value<QColor>(), QColor("#dddddd"));
        QCOMPARE(caption->property("text").toString(), bar->property("temporaryMessage").toString());
        QTest::qWait(1600);
        controller.undo(); // The same text must also restart its three-second lifetime.
        QTest::qWait(1600);
        QVERIFY(!bar->property("temporaryMessage").toString().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("temporaryMessage").toString().isEmpty(), 2500);
        QCOMPARE(caption->property("text").toString(), controller.toolHint());

        controller.pointerPress(QPointF(20, 20), 1, Qt::NoModifier);
        const QString progress = controller.interactionHint();
        QVERIFY(!progress.isEmpty());
        QCOMPARE(caption->property("text").toString(), progress);
        controller.copySelection(); // An error replaces progress, then restores it.
        QCOMPARE(caption->property("text").toString(), bar->property("temporaryMessage").toString());
        QTRY_VERIFY_WITH_TIMEOUT(bar->property("temporaryMessage").toString().isEmpty(), 4000);
        QCOMPARE(caption->property("text").toString(), progress);
        controller.undo();
        controller.setTool(VpTools::Hand);
        QVERIFY(controller.interactionHint().isEmpty());
        QVERIFY(bar->property("temporaryMessage").toString().isEmpty());
        QCOMPARE(caption->property("text").toString(), controller.toolHint());
        QTest::qWait(1600); // An old timer must not clear a message from the new tool.
        controller.copySelection();
        QTest::qWait(1600);
        QVERIFY(!bar->property("temporaryMessage").toString().isEmpty());
        QVERIFY(controller.document().setImage(image));
        QVERIFY(bar->property("temporaryMessage").toString().isEmpty());
        QVERIFY(warnings.isEmpty());
    }
};
QTEST_MAIN(QmlBoundaryTest)
#include "tst_qmlboundaries.moc"
