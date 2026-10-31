// SPDX-License-Identifier: GPL-2.0-or-later
#include <Plasma/Containment>
#include <Plasma/Corona>
#include <Plasma/PluginLoader>
#include <KConfig>
#include <KConfigGroup>
#include <KConfigPropertyMap>
#include <QQmlProperty>
#include <QQmlListReference>
#include <KLocalizedString>
#include <QAbstractItemModel>
#include <PlasmaQuick/AppletQuickItem>
#include <QApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QLineF>
#include <QStandardPaths>
#include <QTest>
#include <QTemporaryDir>
#include <clocale>

class TestCorona : public Plasma::Corona
{
public:
    using Plasma::Corona::createContainmentDelayed;
    QRect screenGeometry(uint) const override { return QRect(0, 0, 1200, 800); }
};

class UiTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void widgetExplorerAcceptsPlugin()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import org.kde.plasma.private.shell 2.0\nWidgetExplorer {}", QUrl());
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QScopedPointer<QObject> explorer(component.create());
        QVERIFY2(explorer, qPrintable(component.errorString()));
        auto *model = qobject_cast<QAbstractItemModel *>(explorer->property("widgetsModel").value<QObject *>());
        QVERIFY(model);
        const auto roles = model->roleNames();
        const int pluginRole = roles.key(QByteArrayLiteral("pluginName"), -1);
        const int supportedRole = roles.key(QByteArrayLiteral("isSupported"), -1);
        const int messageRole = roles.key(QByteArrayLiteral("unsupportedMessage"), -1);
        QVERIFY(pluginRole >= 0 && supportedRole >= 0);
        bool found = false;
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto index = model->index(row, 0);
            if (index.data(pluginRole).toString() == QStringLiteral("org.kde.plasma.icontray")) {
                found = true;
                QVERIFY2(index.data(supportedRole).toBool(), qPrintable(index.data(messageRole).toString()));
            }
        }
        QVERIFY2(found, "IconTray must appear in KDE's actual widget explorer model");
    }

    void appletAndConfigurationLoad()
    {
        QTemporaryDir temporary;
        // Register all available IDs as known, leaving native applets disabled in
        // offscreen tests (Klipper needs a real Wayland/X11 display).
        QStringList known;
        for (const auto &metadata : Plasma::PluginLoader::self()->listAppletMetaData(QString())) {
            known.append(metadata.pluginId());
        }
        TestCorona corona;
        corona.loadLayout(temporary.filePath(QStringLiteral("layout")));
        auto *tray = corona.createContainmentDelayed(QStringLiteral("org.kde.plasma.icontray"));
        QVERIFY(tray);
        auto config = tray->config();
        config.group(QStringLiteral("General")).writeEntry("knownItems", known);
        config.group(QStringLiteral("General")).writeEntry("extraItems", QStringList());
        tray->init();
        tray->restore(config);
        QVERIFY(tray);
        QCOMPARE(tray->pluginMetaData().pluginId(), QStringLiteral("org.kde.plasma.icontray"));
        tray->setFormFactor(Plasma::Types::Horizontal);
        auto *item = PlasmaQuick::AppletQuickItem::itemForApplet(tray);
        QVERIFY(item);
        QQuickWindow window;
        window.resize(800, 48);
        item->setParentItem(window.contentItem());
        item->setSize(QSizeF(800, 48));
        window.show();
        QTest::qWait(100);
        auto *context = QQmlEngine::contextForObject(item);
        QVERIFY(context);
        // Exercise the actual tray delegate, including QtQuick.Layouts constraints.
        QVERIFY(QQmlProperty::write(tray->configuration(), QStringLiteral("itemRules"), QStringLiteral(R"({"test-icon":{"scale":125}})")));
        QQmlComponent iconComponent(context->engine(), QUrl(QStringLiteral("qrc:/qt/qml/plasma/applet/org/kde/plasma/icontray/StatusNotifierItem.qml")));
        const QVariantMap iconModel{{QStringLiteral("itemId"), QStringLiteral("test-icon")},
                                    {QStringLiteral("Id"), QStringLiteral("test-icon")},
                                    {QStringLiteral("IconName"), QStringLiteral("preferences-system")},
                                    {QStringLiteral("Title"), QStringLiteral("Geometry test")}};
        const QVariantMap iconProperties{{QStringLiteral("index"), 0},
                                         {QStringLiteral("status"), int(Plasma::Types::ActiveStatus)},
                                         {QStringLiteral("effectiveStatus"), int(Plasma::Types::ActiveStatus)},
                                         {QStringLiteral("model"), iconModel}};
        QScopedPointer<QObject> iconObject(iconComponent.createWithInitialProperties(iconProperties, context));
        QVERIFY2(iconObject, qPrintable(iconComponent.errorString()));
        auto *iconItem = qobject_cast<QQuickItem *>(iconObject.data());
        QVERIFY(iconItem);
        iconItem->setParentItem(window.contentItem());
        auto *surface = iconItem->findChild<QQuickItem *>(QStringLiteral("iconTraySurface"));
        QVERIFY(surface);
        for (const QSizeF size : {QSizeF(40, 32), QSizeF(14, 40), QSizeF(40, 14)}) {
            iconItem->setSize(size);
            QTest::qWait(20);
            QVERIFY(surface->width() > 0);
            QCOMPARE(surface->width(), surface->height());
            QVERIFY(surface->width() <= qMin(size.width(), size.height()));
            QCOMPARE(surface->scale(), 1.0);
        }
        iconItem->setSize(QSizeF(40, 32));
        QTest::qWait(20);
        const qreal enlargedSize = surface->width();
        QVERIFY(QQmlProperty::write(tray->configuration(), QStringLiteral("itemRules"), QStringLiteral(R"({"test-icon":{"scale":50}})")));
        QTRY_VERIFY(surface->width() < enlargedSize);
        QCOMPARE(surface->width(), surface->height());
        iconItem->setParentItem(nullptr);
        iconObject.reset();
        // Forked QML must keep using the native translation catalog explicitly.
        // The applet ID otherwise selects an absent IconTray catalog by default.
        // GNU gettext deliberately ignores LANGUAGE under a C message locale.
        const QByteArray previousLocale(std::setlocale(LC_MESSAGES, nullptr));
        const bool canTranslate = std::setlocale(LC_MESSAGES, "en_US.UTF-8") || std::setlocale(LC_MESSAGES, "zh_CN.UTF-8");
        for (const auto &language : {QStringLiteral("en_US"), QStringLiteral("zh_CN")}) {
            if (language == QStringLiteral("zh_CN") && (!canTranslate || !KLocalizedString::availableDomainTranslations("plasma_applet_org.kde.plasma.systemtray").contains(language))) {
                continue; // Chinese KDE language pack is optional on other machines.
            }
            KLocalizedString::setLanguages({language});
            context->engine()->setUiLanguage(language);
            context->engine()->retranslate();
            QCOMPARE(i18nd("plasma_applet_org.kde.plasma.systemtray", "General"), language == QStringLiteral("zh_CN") ? QStringLiteral("常规") : QStringLiteral("General"));
            QQmlComponent categoriesComponent(context->engine(), QUrl(QStringLiteral("qrc:/qt/qml/plasma/applet/org/kde/plasma/icontray/config.qml")));
            QScopedPointer<QObject> categoriesObject(categoriesComponent.create(context));
            QVERIFY2(categoriesObject, qPrintable(categoriesComponent.errorString()));
            QQmlListReference categories(categoriesObject.data(), "categories");
            QVERIFY(categories.isValid());
            QCOMPARE(categories.count(), 2);
            QCOMPARE(categories.at(1)->property("name").toString(), language == QStringLiteral("zh_CN") ? QStringLiteral("常规") : QStringLiteral("General"));
            QQmlComponent generalComponent(context->engine(), QUrl(QStringLiteral("qrc:/qt/qml/plasma/applet/org/kde/plasma/icontray/ConfigGeneral.qml")));
            QScopedPointer<QObject> general(generalComponent.createWithInitialProperties({{QStringLiteral("cfg_iconSpacing"), 2}}, context));
            QVERIFY2(general, qPrintable(generalComponent.errorString()));
            const auto choices = general->property("comboBoxModel").value<QJSValue>().toVariant().toList();
            QVERIFY(!choices.isEmpty());
            QCOMPARE(choices.first().toMap().value(QStringLiteral("text")).toString(), language == QStringLiteral("zh_CN") ? QStringLiteral("有作用时显示") : QStringLiteral("Show when relevant"));
        }
        KLocalizedString::clearLanguages();
        std::setlocale(LC_MESSAGES, previousLocale.constData());
        const QUrl url(QStringLiteral("qrc:/qt/qml/plasma/applet/org/kde/plasma/icontray/ConfigAppearance.qml"));
        QQmlComponent component(context->engine(), url);
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QScopedPointer<QObject> page(component.create(context));
        QVERIFY2(page, qPrintable(component.errorString()));
        page->setProperty("width", 800);
        page->setProperty("height", 700);
        auto *pageItem = qobject_cast<QQuickItem *>(page.data());
        QVERIFY(pageItem);
        QQuickWindow settingsWindow;
        settingsWindow.resize(800, 700);
        pageItem->setParentItem(settingsWindow.contentItem());
        settingsWindow.show();
        QTest::qWait(100);
        QVERIFY(QMetaObject::invokeMethod(page.data(), "refresh"));
        page->setProperty("selectedGroup", 2);
        QTest::qWait(20);
        const auto entries = page->property("visibleEntries").value<QJSValue>().toVariant().toList();
        QVERIFY2(entries.size() > 1, "Expected native tray entries in the configuration page");
        const auto movedId = entries.first().toMap().value(QStringLiteral("itemId"));
        QVERIFY(QMetaObject::invokeMethod(page.data(), "setRule", Q_ARG(QVariant, movedId), Q_ARG(QVariant, QStringLiteral("group")), Q_ARG(QVariant, 0)));
        QTest::qWait(20);
        page->setProperty("selectedGroup", 0);
        QTest::qWait(20);
        const auto applicationEntries = page->property("visibleEntries").value<QJSValue>().toVariant().toList();
        QVERIFY(std::any_of(applicationEntries.cbegin(), applicationEntries.cend(), [&movedId](const QVariant &entry) {
            return entry.toMap().value(QStringLiteral("itemId")) == movedId;
        }));
        QVERIFY(QMetaObject::invokeMethod(page.data(), "setRule", Q_ARG(QVariant, movedId), Q_ARG(QVariant, QStringLiteral("group")), Q_ARG(QVariant, 2)));
        page->setProperty("selectedGroup", 2);
        QTest::qWait(20);
        page->setProperty("cfg_groupItems", false);
        QTest::qWait(10);
        auto *list = page->findChild<QQuickItem *>(QStringLiteral("systemEntries"));
        QVERIFY(list);
        // Follow the moved item until the real list boundaries, without blank space.
        if (list->property("contentHeight").toReal() > list->height() && entries.size() > 3) {
            int targetIndex = entries.size() - 2;
            QVERIFY(QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, targetIndex), Q_ARG(int, 4)));
            QTest::qWait(50);
            const auto movingId = page->property("visibleEntries").value<QJSValue>().toVariant().toList().at(targetIndex).toMap().value(QStringLiteral("itemId"));
            for (const auto &direction : {QStringLiteral("moveEntryUp"), QStringLiteral("moveEntryDown")}) {
                QQuickItem *row = nullptr;
                QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, targetIndex)));
                QVERIFY(row);
                auto *button = row->findChild<QQuickItem *>(direction);
                QVERIFY(button);
                auto anchor = button->mapToScene(QPointF(button->width() / 2, button->height() / 2));
                const int step = direction == QStringLiteral("moveEntryUp") ? -1 : 1;
                const int end = step < 0 ? 0 : entries.size() - 1;
                while (targetIndex != end) {
                    QTest::mouseClick(&settingsWindow, Qt::LeftButton, Qt::NoModifier, anchor.toPoint());
                    QTest::qWait(30);
                    targetIndex += step;
                    QCOMPARE(page->property("visibleEntries").value<QJSValue>().toVariant().toList().at(targetIndex).toMap().value(QStringLiteral("itemId")), movingId);
                    QQuickItem *moved = nullptr;
                    QVERIFY(QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, moved), Q_ARG(int, targetIndex)));
                    QVERIFY(moved);
                    auto *nextButton = moved->findChild<QQuickItem *>(direction);
                    QVERIFY(nextButton);
                    const auto nextPosition = nextButton->mapToScene(QPointF(nextButton->width() / 2, nextButton->height() / 2));
                    const qreal minimumY = list->property("originY").toReal();
                    const qreal maximumY = qMax(minimumY, minimumY + list->property("contentHeight").toReal() - list->height());
                    const qreal contentY = list->property("contentY").toReal();
                    QVERIFY(contentY >= minimumY - 1 && contentY <= maximumY + 1);
                    QCOMPARE(list->property("topMargin").toReal(), 0.0);
                    QCOMPARE(list->property("bottomMargin").toReal(), 0.0);
                    if (targetIndex == 0) {
                        QVERIFY2(qAbs(moved->y() - contentY) < 1.5, "The first item must meet the top of the list");
                    }
                    if (QLineF(anchor, nextPosition).length() >= 1.5) {
                        QVERIFY2(qAbs(contentY - minimumY) < 1 || qAbs(contentY - maximumY) < 1,
                                 "The button may move only when the list reaches an edge");
                        anchor = nextPosition;
                    }
                }
            }
        }
        QVERIFY(QMetaObject::invokeMethod(page.data(), "moveEntry", Q_ARG(QVariant, 1), Q_ARG(QVariant, 0), Q_ARG(QVariant, false)));
        QTest::qWait(10);
        const auto order = page->property("cfg_itemOrder").toStringList();
        QVERIFY(!order.isEmpty());
        // Apply through the same property map that Plasma's settings dialog uses.
        QVERIFY(QQmlProperty::write(tray->configuration(), QStringLiteral("groupItems"), false));
        QVERIFY(QQmlProperty::write(tray->configuration(), QStringLiteral("itemOrder"), order));
        tray->configuration()->writeConfig();
        auto *model = tray->property("systemTrayModel").value<QAbstractItemModel *>();
        QVERIFY(model);
        QTRY_COMPARE(model->index(0, 0).data(Qt::UserRole + 2).toString(), order.first());
        config.config()->sync();
        KConfig reloaded(config.config()->name(), KConfig::SimpleConfig);
        const auto persisted = reloaded.group(QStringLiteral("Applets")).group(QString::number(tray->id())).group(QStringLiteral("General"));
        QCOMPARE(persisted.readEntry("itemOrder", QStringList()), order);
        pageItem->setParentItem(nullptr);
        item->setParentItem(nullptr);
    }
};
int main(int argc, char **argv)
{
    QTemporaryDir environment;
    qputenv("XDG_CONFIG_HOME", environment.filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_CACHE_HOME", environment.filePath(QStringLiteral("cache")).toUtf8());
    // A plain QApplication does not initialize libc's message locale for gettext.
    std::setlocale(LC_ALL, "");
    QApplication application(argc, argv);
    UiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_ui.moc"
