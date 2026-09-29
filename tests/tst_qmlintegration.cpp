#include "canvas/vpcanvas.h"
#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QtTest>
#include <qqml.h>

class QmlIntegrationTest : public QObject {
    Q_OBJECT
private slots:
    void redoShortcutCallsRedo() {
        qmlRegisterType<VpCanvas>("VanishingPoint",1,0,"VpCanvas");
        qmlRegisterUncreatableType<VpController>("VanishingPoint",1,0,"VpController","Canvas owns controller");
        QQmlApplicationEngine engine;
        engine.load(QUrl("qrc:/src/qml/main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *root=engine.rootObjects().first(); root->setProperty("visible",false);
        auto *canvas=root->findChild<VpCanvas*>(); QVERIFY(canvas);
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
    }
};
QTEST_MAIN(QmlIntegrationTest)
#include "tst_qmlintegration.moc"
