// SPDX-License-Identifier: GPL-2.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kitemmodels as KItemModels
import org.kde.kirigami as Kirigami
import org.kde.plasma.plasmoid
import "TrayRules.js" as TrayRules

KCMUtils.ScrollViewKCM {
    id: page

    property list<string> cfg_itemOrder: []
    property string cfg_itemRules: "{}"
    property int cfg_iconSize: 22
    property bool cfg_singleRow: true
    property bool cfg_groupItems: true
    readonly property var rules: TrayRules.parse(cfg_itemRules)
    property var entries: []
    property int selectedGroup: 0
    readonly property var visibleEntries: entries.filter(entry => TrayRules.rule(rules, entry.itemId).group === selectedGroup)
    ListModel {
        id: entriesModel
        dynamicRoles: true
    }
    property var pendingFollow: null
    property string followedItemId: ""
    property int dragFrom: -1
    property int dragTo: -1
    readonly property var groupNames: [i18nd("icontray", "Applications"), i18nd("icontray", "System")]

    function refresh() {
        const source = Plasmoid.systemTrayModel;
        const rows = [];
        for (let i = 0; i < source.rowCount(); ++i) {
            const idx = source.index(i, 0);
            const id = idx.data(Qt.UserRole + 2);
            if (!id || rows.some(r => r.itemId === id)) continue;
            rows.push({itemId: id, name: idx.data(Qt.DisplayRole) || id,
                       decoration: idx.data(Qt.DecorationRole),
                       itemType: idx.data(Qt.UserRole + 1), sourceIndex: i, groupId: TrayRules.rule(rules, id).group});
        }
        rows.sort((a, b) => TrayRules.compare(a, b, cfg_itemOrder, rules, true));
        // Preserve delegates and text focus for scale/icon edits and status updates.
        if (rows.length !== entries.length || rows.some((r, i) => r.itemId !== entries[i].itemId || r.name !== entries[i].name || r.groupId !== entries[i].groupId)) {
            // Move existing rows instead of resetting a JS-array model. A reset
            // destroys delegates and sends ListView's viewport back to the top.
            for (let i = 0; i < rows.length; ++i) {
                let existing = -1;
                for (let j = i; j < entriesModel.count; ++j) {
                    if (entriesModel.get(j).itemId === rows[i].itemId) {
                        existing = j;
                        break;
                    }
                }
                if (existing < 0) {
                    entriesModel.insert(i, rows[i]);
                } else {
                    if (existing !== i) entriesModel.move(existing, i, 1);
                    entriesModel.set(i, rows[i]);
                }
            }
            if (entriesModel.count > rows.length) {
                entriesModel.remove(rows.length, entriesModel.count - rows.length);
            }
            entries = rows;
        }
        followMovedEntry();
    }

    function followMovedEntry() {
        if (!pendingFollow) return;
        const anchor = pendingFollow;
        pendingFollow = null;
        const index = visibleEntries.findIndex(entry => entry.itemId === anchor.itemId);
        if (index < 0) return;
        list.cancelFlick();
        list.forceLayout();
        let moved = list.itemAtIndex(index);
        if (!moved) {
            list.positionViewAtIndex(index, ListView.Contain);
            list.forceLayout();
            moved = list.itemAtIndex(index);
        }
        if (!moved) return;
        const desiredY = moved.y - anchor.offset;
        const minimumY = list.originY;
        const maximumY = Math.max(minimumY, minimumY + list.contentHeight - list.height);
        // Follow the item only within the real content bounds.
        list.contentY = Math.max(minimumY, Math.min(maximumY, desiredY));
    }

    function setRule(id, key, value) {
        cfg_itemRules = TrayRules.update(cfg_itemRules, id, key, value);
    }

    function moveEntry(from, to, follow = false) {
        if (from === to || from < 0 || to < 0 || from >= visibleEntries.length || to >= visibleEntries.length) return;
        if (follow) {
            const moving = list.itemAtIndex(from);
            followedItemId = visibleEntries[from].itemId;
            pendingFollow = moving ? {itemId: followedItemId, offset: moving.y - list.contentY} : null;
        }
        cfg_itemOrder = TrayRules.move(cfg_itemOrder, visibleEntries.map(r => r.itemId), from, to);
    }

    onCfg_itemOrderChanged: Qt.callLater(refresh)
    onCfg_itemRulesChanged: Qt.callLater(refresh)
    onCfg_groupItemsChanged: Qt.callLater(refresh)
    Component.onCompleted: Qt.callLater(refresh)
    Connections {
        target: Plasmoid.systemTrayModel
        function onRowsInserted() { Qt.callLater(page.refresh); }
        function onRowsRemoved() { Qt.callLater(page.refresh); }
        function onDataChanged() { Qt.callLater(page.refresh); }
        function onLayoutChanged() { Qt.callLater(page.refresh); }
        function onModelReset() { Qt.callLater(page.refresh); }
    }

    header: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing
        QQC2.Label {
            Layout.fillWidth: true
            text: i18nd("icontray", "Drag the handle or use the arrows to reorder. Group assignments are manual; icon style is not detected automatically. Changes take effect when you click Apply.")
            wrapMode: Text.Wrap
        }
        Kirigami.FormLayout {
            QQC2.SpinBox {
                Kirigami.FormData.label: i18nd("icontray", "Icon size (logical pixels):")
                from: 12
                to: 64
                value: page.cfg_iconSize
                onValueModified: page.cfg_iconSize = value
            }
            QQC2.CheckBox {
                text: i18nd("icontray", "Keep a single row or column")
                checked: page.cfg_singleRow
                onToggled: page.cfg_singleRow = checked
            }
            QQC2.CheckBox {
                text: i18nd("icontray", "Group application and system icons")
                checked: page.cfg_groupItems
                onToggled: page.cfg_groupItems = checked
            }
        }
        QQC2.Label {
            Layout.fillWidth: true
            text: i18nd("icontray", "Panel scaling in General overrides the fixed size. Icon replacements apply to application tray icons; attention icons keep their original appearance.")
            wrapMode: Text.Wrap
            opacity: 0.7
        }
        QQC2.TabBar {
            Layout.fillWidth: true
            currentIndex: page.selectedGroup === 0 ? 0 : 1
            onCurrentIndexChanged: page.selectedGroup = currentIndex === 0 ? 0 : 2
            QQC2.TabButton { text: page.groupNames[0] }
            QQC2.TabButton { text: page.groupNames[1] }
        }
        QQC2.Button {
            text: i18nd("icontray", "Reset order and individual adjustments")
            icon.name: "edit-undo"
            onClicked: {
                page.cfg_itemOrder = [];
                page.cfg_itemRules = "{}";
            }
        }
    }

    KItemModels.KSortFilterProxyModel {
        id: applicationModel
        sourceModel: entriesModel
        filterRoleName: "groupId"
        filterRowCallback: (sourceRow, sourceParent) => sourceModel.data(sourceModel.index(sourceRow, 0, sourceParent), filterRole) === 0
    }
    KItemModels.KSortFilterProxyModel {
        id: systemModel
        sourceModel: entriesModel
        filterRoleName: "groupId"
        filterRowCallback: (sourceRow, sourceParent) => sourceModel.data(sourceModel.index(sourceRow, 0, sourceParent), filterRole) === 2
    }
    view: ListView {
        id: list
        objectName: page.selectedGroup === 0 ? "applicationEntries" : "systemEntries"
        clip: true
        spacing: Kirigami.Units.smallSpacing
        model: page.selectedGroup === 0 ? applicationModel : systemModel
        QQC2.Label {
            anchors.centerIn: parent
            visible: list.count === 0
            text: i18nd("icontray", "No tray entries available.")
        }
        delegate: QQC2.ItemDelegate {
            id: row
            required property string itemId
            required property string name
            required property var decoration
            required property string itemType
            required property int index
            readonly property var rule: TrayRules.rule(page.rules, itemId)
            width: list.width
            highlighted: page.dragTo === index || page.followedItemId === itemId
            hoverEnabled: false
            down: false
            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                RowLayout {
                    QQC2.Label {
                        id: handle
                        text: "⠿"
                        font.pixelSize: 24
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 2
                        horizontalAlignment: Text.AlignHCenter
                        Accessible.name: i18nd("icontray", "Drag to reorder %1", row.name)
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                            preventStealing: true
                            onPressed: {
                                page.dragFrom = row.index;
                                page.dragTo = row.index;
                            }
                            onPositionChanged: mouse => {
                                if (!pressed) return;
                                const point = mapToItem(list.contentItem, mouse.x, mouse.y);
                                const target = list.indexAt(1, point.y);
                                if (target >= 0) page.dragTo = target;
                            }
                            onReleased: {
                                page.moveEntry(page.dragFrom, page.dragTo);
                                page.dragFrom = -1;
                                page.dragTo = -1;
                            }
                            onCanceled: { page.dragFrom = -1; page.dragTo = -1; }
                        }
                    }
                    Item {
                        Layout.minimumWidth: 34
                        Layout.maximumWidth: 34
                        Layout.minimumHeight: 34
                        Layout.maximumHeight: 34
                        IconSurface {
                            baseSize: 22
                            scalePercent: row.rule.scale
                            availableWidth: 34
                            availableHeight: 34
                            Kirigami.Icon {
                                anchors.fill: parent
                                roundToIconSize: false
                                source: row.rule.icon || row.decoration || "application-x-executable"
                            }
                        }
                    }
                    QQC2.Label {
                        Layout.fillWidth: true
                        text: row.name
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        QQC2.ToolTip.text: row.itemId
                        QQC2.ToolTip.visible: nameHover.hovered
                        HoverHandler { id: nameHover }
                    }
                    QQC2.ComboBox {
                        model: page.groupNames
                        currentIndex: row.rule.group === 2 ? 1 : 0
                        enabled: true
                        Accessible.name: i18nd("icontray", "Group for %1", row.name)
                        onActivated: page.setRule(row.itemId, "group", currentIndex === 1 ? 2 : 0)
                    }
                    QQC2.ToolButton {
                        objectName: "moveEntryUp"
                        icon.name: "go-up"
                        enabled: row.index > 0
                        Accessible.name: i18nd("icontray", "Move %1 up", row.name)
                        onClicked: page.moveEntry(row.index, row.index - 1, true)
                    }
                    QQC2.ToolButton {
                        objectName: "moveEntryDown"
                        icon.name: "go-down"
                        enabled: row.index < list.count - 1
                        Accessible.name: i18nd("icontray", "Move %1 down", row.name)
                        onClicked: page.moveEntry(row.index, row.index + 1, true)
                    }
                }
                RowLayout {
                    QQC2.Label { text: i18nd("icontray", "Scale:") }
                    QQC2.SpinBox {
                        from: 50
                        to: 150
                        stepSize: 5
                        value: row.rule.scale
                        Accessible.name: i18nd("icontray", "Scale percent for %1", row.name)
                        textFromValue: function(value, locale) { return value + "%"; }
                        valueFromText: function(text, locale) { return parseInt(text); }
                        onValueModified: page.setRule(row.itemId, "scale", value)
                    }
                    QQC2.TextField {
                        Layout.fillWidth: true
                        enabled: row.itemType === "StatusNotifier"
                        text: row.rule.icon
                        placeholderText: i18nd("icontray", "Theme icon name or file:///absolute/path.svg")
                        Accessible.name: i18nd("icontray", "Replacement icon for %1", row.name)
                        onEditingFinished: {
                            if (text !== row.rule.icon) page.setRule(row.itemId, "icon", text.trim());
                        }
                    }
                    QQC2.ToolButton {
                        icon.name: "edit-undo"
                        Accessible.name: i18nd("icontray", "Reset adjustments for %1", row.name)
                        onClicked: {
                            const rules = TrayRules.parse(page.cfg_itemRules);
                            delete rules[row.itemId];
                            page.cfg_itemRules = JSON.stringify(rules);
                        }
                    }
                }
            }
        }
    }
}
