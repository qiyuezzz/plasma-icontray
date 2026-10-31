// SPDX-License-Identifier: GPL-2.0-or-later
#include "traypreferences.h"
#include "sortedsystemtraymodel.h"
#include "systemtraymodel.h"
#include <QStandardItemModel>
#include <QTest>

class PreferencesTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void groupingAndOrder()
    {
        TrayPreferences p;
        p.set({QStringLiteral("org.kde.plasma.volume"), QStringLiteral("chat"), QStringLiteral("music")},
              QStringLiteral(R"({"chat":{"group":1}})"), true);
        QCOMPARE(p.group(QStringLiteral("chat")), 0);
        QVERIFY(p.compare(QStringLiteral("chat"), QStringLiteral("music")) < 0);
        QVERIFY(p.compare(QStringLiteral("chat"), QStringLiteral("org.kde.plasma.volume")) < 0);
        QVERIFY(p.compare(QStringLiteral("music"), QStringLiteral("new-app")) < 0);
        p.set({QStringLiteral("org.kde.plasma.volume"), QStringLiteral("music")}, QStringLiteral("{}"), false);
        QVERIFY(p.compare(QStringLiteral("org.kde.plasma.volume"), QStringLiteral("music")) < 0);
    }

    void malformedAndDuplicateSettings()
    {
        TrayPreferences p;
        p.set({QStringLiteral("a"), QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("not JSON"), true);
        QVERIFY(p.compare(QStringLiteral("a"), QStringLiteral("b")) < 0);
        p.set({}, QStringLiteral(R"({"a":{"group":99},"b":{"group":null}})"), true);
        QCOMPARE(p.group(QStringLiteral("a")), 0);
        QCOMPARE(p.group(QStringLiteral("b")), 0);
        QCOMPARE(p.group(QStringLiteral("org.kde.plasma.volume")), 2);
    }

    void liveModelReordersAndReconnects()
    {
        QStandardItemModel source;
        auto append = [&source](const QString &id, const QString &name) {
            auto *item = new QStandardItem(name);
            item->setData(id, static_cast<int>(BaseModel::BaseRole::ItemId));
            item->setData(QStringLiteral("ApplicationStatus"), static_cast<int>(BaseModel::BaseRole::Category));
            source.appendRow(item);
        };
        append(QStringLiteral("a"), QStringLiteral("Alpha"));
        append(QStringLiteral("b"), QStringLiteral("Beta"));
        SortedSystemTrayModel proxy(SortedSystemTrayModel::SortingType::IconTray);
        proxy.setSourceModel(&source);
        const auto first = [&proxy] { return proxy.index(0, 0).data(static_cast<int>(BaseModel::BaseRole::ItemId)).toString(); };
        proxy.setPreferences({QStringLiteral("b"), QStringLiteral("a")}, QStringLiteral("{}"), false);
        QCOMPARE(first(), QStringLiteral("b"));
        source.removeRow(1);
        QCOMPARE(first(), QStringLiteral("a"));
        append(QStringLiteral("b"), QStringLiteral("Beta"));
        QCOMPARE(first(), QStringLiteral("b"));
        proxy.setPreferences({}, QStringLiteral("{}"), false);
        QCOMPARE(first(), QStringLiteral("a"));
    }
};
QTEST_GUILESS_MAIN(PreferencesTest)
#include "test_traypreferences.moc"
