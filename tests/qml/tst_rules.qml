// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import "../../src/qml/TrayRules.js" as Rules
TestCase {
    name: "TrayRules"
    function test_invalidConfiguration() {
        compare(JSON.stringify(Rules.parse("invalid")), "{}");
        compare(JSON.stringify(Rules.parse("[]")), "{}");
        compare(Rules.rule({}, "org.kde.plasma.volume").group, 2);
        compare(Rules.rule({app: {scale: 900, group: -1}}, "app").scale, 150);
        compare(Rules.rule({app: {scale: "bad", group: 0.5}}, "app").scale, 100);
        compare(Rules.rule({app: {group: 0.5}}, "app").group, 0);
        compare(Rules.rule({app: {group: 1}}, "app").group, 0); // legacy monochrome
        compare(Rules.rule({app: {group: 2}}, "app").group, 2);
    }
    function test_movePreservesOfflineEntries() {
        compare(Rules.move(["a", "offline", "b", "c"], ["a", "b", "c"], 2, 0), ["c", "a", "offline", "b"]);
        compare(Rules.move(["a", "offline", "b", "c"], ["a", "b", "c"], 0, 2), ["offline", "b", "c", "a"]);
        compare(Rules.move([], ["a", "b"], 1, 0), ["b", "a"]);
    }
    function test_independentUpdates() {
        const updated = Rules.update('{"a":{"scale":115}}', "b", "group", 2);
        compare(Rules.rule(Rules.parse(updated), "a").scale, 115);
        compare(Rules.rule(Rules.parse(updated), "b").group, 2);
        const unusual = Rules.parse(Rules.update("{}", "__proto__", "scale", 120));
        compare(Rules.rule(unusual, "__proto__").scale, 120);
    }
    function test_groupAndOrder() {
        const a = {itemId: "app", sourceIndex: 0};
        const b = {itemId: "org.kde.plasma.volume", sourceIndex: 1};
        verify(Rules.compare(a, b, [b.itemId, a.itemId], {}, true) < 0);
        verify(Rules.compare(a, b, [b.itemId, a.itemId], {}, false) > 0);
    }
}
