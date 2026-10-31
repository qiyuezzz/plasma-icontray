// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtTest
import org.kde.kirigami as Kirigami
import "../../src/qml" as Tray

TestCase {
    name: "IconSizing"
    when: windowShown
    width: 200
    height: 200

    Component {
        id: fixture
        Item {
            width: 48
            height: 32
            property alias surface: surface
            property alias slot: slot
            property alias icon: icon
            Item {
                id: slot
                x: 4.5
                y: 3.5
                width: 22
                height: 22
                Tray.IconSurface {
                    id: surface
                    baseSize: 22
                    scalePercent: 125
                    availableWidth: parent.parent.width
                    availableHeight: parent.parent.height
                    devicePixelRatio: 1
                    Kirigami.Icon {
                        id: icon
                        anchors.fill: parent
                        roundToIconSize: false
                        source: Qt.resolvedUrl("../fixtures/wide.svg")
                    }
                }
            }
        }
    }

    function test_renderSizeAndPixelAlignment_data() {
        const rows = [];
        for (const ratio of [1, 1.25, 1.5, 2]) {
            for (const percent of [50, 100, 125, 150]) {
                for (const vertical of [false, true]) {
                    rows.push({tag: ratio + "/" + percent + "/" + vertical, ratio: ratio, percent: percent, vertical: vertical});
                }
            }
        }
        return rows;
    }

    function test_renderSizeAndPixelAlignment(data) {
        const item = createTemporaryObject(fixture, this);
        verify(item);
        item.width = data.vertical ? 32 : 48;
        item.height = data.vertical ? 48 : 32;
        item.surface.devicePixelRatio = data.ratio;
        item.surface.scalePercent = data.percent;
        compare(item.surface.width, item.surface.height);
        verify(item.surface.width <= Math.min(item.width, item.height));
        compare(item.surface.scale, 1); // no persistent texture rescaling
        verify(Math.abs(item.surface.width * data.ratio - Math.round(item.surface.width * data.ratio)) < 0.00001);
        const x = (item.slot.x + item.surface.x) * data.ratio;
        const y = (item.slot.y + item.surface.y) * data.ratio;
        verify(Math.abs(x - Math.round(x)) < 0.00001);
        verify(Math.abs(y - Math.round(y)) < 0.00001);
        compare(item.icon.width, item.surface.width);
        compare(item.icon.height, item.surface.height);
    }

    function test_sourceAspectRatio_data() {
        return [{tag: "wide", source: "wide.svg", ratio: 2}, {tag: "tall", source: "tall.svg", ratio: 0.5}];
    }

    function test_sourceAspectRatio(data) {
        const item = createTemporaryObject(fixture, this);
        item.icon.source = Qt.resolvedUrl("../fixtures/" + data.source);
        tryCompare(item.icon, "status", Kirigami.Icon.Ready);
        tryVerify(() => item.icon.paintedWidth > 0 && item.icon.paintedHeight > 0);
        verify(Math.abs(item.icon.paintedWidth / item.icon.paintedHeight - data.ratio) < 0.15);
        item.surface.scalePercent = 75;
        tryVerify(() => Math.abs(item.icon.paintedWidth / item.icon.paintedHeight - data.ratio) < 0.15);
    }
}
