#include "canvas/vpcanvas.h"
#include "testhelpers.h"
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QKeyEvent>
#include <QClipboard>
#include <QWheelEvent>
#include <memory>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>
#include <qqml.h>

class QmlIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qmlRegisterUncreatableMetaObject(VpTools::staticMetaObject,"VanishingPoint",1,0,"VpTools","Enums only");
        qmlRegisterSingletonType(QUrl(QStringLiteral("qrc:/src/qml/VpTheme.qml")),
                                 "VanishingPoint", 1, 0, "VpTheme");
        qmlRegisterType<VpCanvas>("VanishingPoint",1,0,"VpCanvas");
    }

    void redoShortcutCallsRedo() {
        VpController controller;
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        engine.rootContext()->setContextProperty("vpController", &controller);
        engine.load(QUrl("qrc:/src/qml/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *root=engine.rootObjects().first(); root->setProperty("visible",false);
        auto *canvas=root->findChild<VpCanvas*>(); QVERIFY(canvas);
        auto *saveButton=root->findChild<QObject *>("saveButton");
        auto *saveShortcut=root->findChild<QObject *>("saveShortcut");
        auto *saveDialog=root->findChild<QObject *>("saveDialog");
        QVERIFY(saveButton && saveShortcut && saveDialog);
        QVERIFY(!saveButton->property("enabled").toBool());
        QVERIFY(!saveShortcut->property("enabled").toBool());
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
                          VpTools::Brush, VpTools::Transform, VpTools::Eyedropper, VpTools::Hand, VpTools::Zoom})
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
        QVERIFY(saveButton->property("enabled").toBool());
        QVERIFY(saveShortcut->property("enabled").toBool());
        auto *toolbar = root->findChild<QObject *>("toolBar");
        QVERIFY(toolbar);
        const auto selectedButton = [window](int tool) -> QQuickItem * {
            const auto find = [tool](auto &&self, QQuickItem *item) -> QQuickItem * {
                if (item->property("toolId").isValid() && item->property("toolId").toInt() == tool)
                    return item;
                for (auto *child : item->childItems()) if (auto *found = self(self, child)) return found;
                return nullptr;
            };
            return find(find, window->contentItem());
        };
        QKeyEvent spaceDown(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &spaceDown);
        QVERIFY(canvas->temporaryHandActive());
        QCOMPARE(toolbar->property("currentTool").toInt(), int(VpTools::Hand));
        QCOMPARE(controller.tool(), VpTools::CreatePlane);
        QVERIFY(selectedButton(VpTools::Hand)->property("selected").toBool());
        QKeyEvent spaceUp(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
        QCoreApplication::sendEvent(canvas, &spaceUp);
        QVERIFY(!canvas->temporaryHandActive());
        QCOMPARE(toolbar->property("currentTool").toInt(), int(VpTools::CreatePlane));
        QVERIFY(selectedButton(VpTools::CreatePlane)->property("selected").toBool());
        image=QImage(8,8,QImage::Format_ARGB32); image.fill(Qt::red);
        auto &doc=canvas->controller()->document(); doc.addFloatingSelection(makeTestSelection(image)); canvas->controller()->undo();
        QVERIFY(doc.floatingSelections().isEmpty()); QVERIFY(canvas->controller()->canRedo());
        QObject *redo=nullptr;
        for(auto *object:root->findChildren<QObject*>())
            if(object->property("sequences").toStringList().contains("Ctrl+Y")) redo=object;
        QVERIFY(redo); QVERIFY(QMetaObject::invokeMethod(redo,"activated",Qt::DirectConnection));
        QCOMPARE(doc.floatingSelections().size(),1); QVERIFY(!canvas->controller()->canRedo());
        QVERIFY(canvas->controller()->canUndo());
        QVERIFY(transform->property("enabled").toBool());
        doc.removeFloatingSelection(0);
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
        controller.setBrushHealMode(1);
        QCOMPARE(strokeOptions->property("healMode").toInt(), 1);
        QVERIFY(QMetaObject::invokeMethod(strokeOptions, "healModeEdited", Q_ARG(int, 2)));
        QCOMPARE(controller.brushHealMode(), 2);
        controller.setTool(VpTools::Marquee);
        QVERIFY(brushOptions->property("marqueeOptionsVisible").toBool());
        controller.pointerPress({20,20}, 1, Qt::NoModifier);
        controller.pointerRelease({40,40}, Qt::NoModifier);
        QVERIFY(!controller.selectionRect().isEmpty());
        const auto shortcut = [root](const QString &sequence) -> QObject * {
            for (auto *object : root->findChildren<QObject *>())
                if (object->property("sequence").toString() == sequence) return object;
            return nullptr;
        };
        auto *copy = shortcut("Ctrl+C");
        auto *deselect = shortcut("Ctrl+D");
        QVERIFY(copy && deselect);
        QGuiApplication::clipboard()->clear();
        QVERIFY(QMetaObject::invokeMethod(copy, "activated"));
        const int planeCount = doc.planes().size();
        QVERIFY(QMetaObject::invokeMethod(deselect, "activated"));
        QVERIFY(controller.selectionRect().isEmpty());
        QCOMPARE(doc.planes().size(), planeCount);
        // Copy preserves an internal snapshot, rather than writing a system bitmap.
        auto *paste = shortcut("Ctrl+V");
        QVERIFY(paste);
        const int floatingCount = doc.floatingSelections().size();
        QVERIFY(QMetaObject::invokeMethod(paste, "activated"));
        QCOMPARE(doc.floatingSelections().size(), floatingCount + 1);
        QVERIFY(!doc.floatingSelection(floatingCount).bitmap.isNull());
        QVERIFY(QMetaObject::invokeMethod(deselect, "activated"));
        QCOMPARE(doc.floatingSelections().size(), floatingCount + 1);
        auto *moveMode = root->findChild<QObject *>("marqueeMoveMode");
        QVERIFY(moveMode);
        QCOMPARE(moveMode->property("enabled").toBool(), controller.marqueeMoveModeEnabled());
        doc.addFloatingSelection(makeTestSelection(image));
        controller.setTool(VpTools::Transform);
        QVERIFY(brushOptions->property("transformOptionsVisible").toBool());
        auto *flip = root->findChild<QObject *>("flipHorizontal");
        QVERIFY(flip && flip->property("enabled").toBool());
        controller.setFlippedHorizontally(true);
        QCOMPARE(flip->property("checked").toBool(), true);
        flip->setProperty("checked", false);
        QVERIFY(QMetaObject::invokeMethod(flip, "clicked"));
        QVERIFY(!controller.flippedHorizontally());
        controller.setTool(VpTools::Eyedropper);
        QVERIFY(brushOptions->property("eyedropperOptionsVisible").toBool());
        const QUrl savedUrl=QUrl::fromLocalFile(dir.filePath("ui-save.png"));
        QVERIFY(saveDialog->setProperty("selectedFile",savedUrl));
        QVERIFY(QMetaObject::invokeMethod(saveDialog,"accepted"));
        QVERIFY(!QImage(savedUrl.toLocalFile()).isNull());
        QCOMPARE(controller.savedImageUrl(),savedUrl);
        doc.paintLayer().fill(Qt::green);
        QVERIFY(QMetaObject::invokeMethod(saveShortcut,"activated"));
        QCOMPARE(QImage(savedUrl.toLocalFile()).pixelColor(60,60),QColor(Qt::green));
        QVERIFY(!saveDialog->property("visible").toBool());
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
        QSignalSpy recreatedWarnings(&recreated, &QQmlEngine::warnings);
        recreated.rootContext()->setContextProperty("vpController", &controller);
        recreated.load(QUrl("qrc:/src/qml/main.qml"));
        QVERIFY(!recreated.rootObjects().isEmpty());
        auto *newRoot = recreated.rootObjects().first(); newRoot->setProperty("visible", false);
        auto *newCanvas = newRoot->findChild<VpCanvas*>(); QVERIFY(newCanvas);
        QCOMPARE(newCanvas->controller(), &controller);
        QCOMPARE(QQmlEngine::objectOwnership(&controller), QQmlEngine::CppOwnership);
        QCOMPARE(controller.document().planes().size(), 1);
        newCanvas->undo(); QVERIFY(controller.document().planes().isEmpty());
        QVERIFY(warnings.isEmpty());
        QVERIFY(recreatedWarnings.isEmpty());
    }
    void numericOptionsPreviewAndPreserveBindings() {
        QQmlEngine engine;
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQuick
            import "qrc:/src/qml"
            Item {
                width: 400; height: 160
                property int controllerValue: 42
                property bool rejectChanges: false
                OptionInput {
                    objectName: "option"; width: 200; height: 30
                    label: "Value"; from: 1; to: 100
                    value: parent.controllerValue
                    onEdited: (value) => { if (!parent.rejectChanges) parent.controllerValue = value }
                }
            }
        )QML", QUrl("qrc:/src/qml/OptionTest.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QQuickWindow window;
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(root.get()); QVERIFY(item);
        item->setParentItem(window.contentItem());
        window.resize(400, 160); window.show();
        auto *option = root->findChild<QQuickItem *>("option");
        auto *input = root->findChild<QQuickItem *>("valueInput");
        auto *label = root->findChild<QQuickItem *>("labelDrag");
        auto *button = root->findChild<QQuickItem *>("sliderButton");
        auto *slider = root->findChild<QQuickItem *>("valueSlider");
        auto *popup = root->findChild<QObject *>("sliderPopup");
        QVERIFY(option && input && label && button && slider && popup);
        const auto center = [](QQuickItem *target) {
            return target->mapToScene(QPointF(target->width()/2, target->height()/2)).toPoint();
        };
        QCoreApplication::processEvents();
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(button));
        QTRY_VERIFY(popup->property("visible").toBool());
        QTRY_VERIFY(slider->width() > 50);
        const QPoint start = slider->mapToScene(QPointF(20, slider->height()/2)).toPoint();
        const QPoint finish = slider->mapToScene(QPointF(slider->width()-20, slider->height()/2)).toPoint();
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(&window, finish);
        QTRY_VERIFY(root->property("controllerValue").toInt() > 60);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, finish);
        // External updates must still reach the text and slider after live edits.
        root->setProperty("controllerValue", 57);
        QCOMPARE(option->property("value").toInt(), 57);
        QCOMPARE(input->property("text").toString(), QString("57"));
        QCOMPARE(slider->property("value").toInt(), 57);
        option->setVisible(false);
        QTRY_VERIFY(!popup->property("visible").toBool());
        option->setVisible(true);
        QCoreApplication::processEvents();
        const QPoint labelStart = center(label);
        QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, labelStart);
        QTest::mouseMove(&window, labelStart + QPoint(9,0));
        QTRY_COMPARE(root->property("controllerValue").toInt(), 66);
        QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, labelStart + QPoint(9,0));
        const QPoint inputCenter = center(input);
        QWheelEvent wheel(inputCenter, window.mapToGlobal(inputCenter), {}, {0,120},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&window, &wheel);
        QCOMPARE(root->property("controllerValue").toInt(), 67);
        input->forceActiveFocus(); input->setProperty("text", "999");
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(root->property("controllerValue").toInt(), 100);
        input->forceActiveFocus(); input->setProperty("text", "bad");
        QTest::keyClick(&window, Qt::Key_Return);
        QCOMPARE(input->property("text").toString(), QString("100"));
        input->forceActiveFocus(); input->setProperty("text", "22");
        QTest::keyClick(&window, Qt::Key_Escape);
        QCOMPARE(root->property("controllerValue").toInt(), 100);
        input->forceActiveFocus(); input->setProperty("text", "28"); item->forceActiveFocus();
        QCOMPARE(root->property("controllerValue").toInt(), 28);
        root->setProperty("rejectChanges", true);
        QVERIFY(QMetaObject::invokeMethod(option, "preview", Q_ARG(QVariant, QVariant(80))));
        QCOMPARE(root->property("controllerValue").toInt(), 28);
        QCOMPARE(input->property("text").toString(), QString("28"));
        root->setProperty("rejectChanges", false);
        QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center(button));
        QTRY_VERIFY(popup->property("visible").toBool());
        option->setEnabled(false);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QVERIFY(QMetaObject::invokeMethod(option, "adjustValueByWheel", Q_ARG(QVariant, QVariant(120))));
        QCOMPARE(root->property("controllerValue").toInt(), 28);
        QVERIFY(warnings.isEmpty());
    }
};
QTEST_MAIN(QmlIntegrationTest)
#include "tst_qmlintegration.moc"
