// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

// Shared policy: stable IDs, never DBus service names or process IDs.
class TrayPreferences
{
public:
    void set(const QStringList &order, const QString &json, bool grouped)
    {
        m_order.clear();
        for (const auto &id : order) {
            if (!id.isEmpty() && !m_order.contains(id)) {
                m_order.insert(id, m_order.size());
            }
        }
        m_rules = QJsonDocument::fromJson(json.toUtf8()).object();
        m_grouped = grouped;
    }

    int group(const QString &id) const
    {
        const int fallback = id.startsWith(QLatin1String("org.kde.plasma.")) ? 2 : 0;
        const auto value = m_rules.value(id).toObject().value(QStringLiteral("group"));
        const int group = value.isDouble() && value.toInt(-1) >= 0 && value.toInt(-1) <= 2 ? value.toInt() : fallback;
        // Legacy monochrome entries now belong to the application group.
        return group == 1 ? 0 : group;
    }

    // Returns -1, 0, +1. Unconfigured entries are appended within their group.
    int compare(const QString &left, const QString &right) const
    {
        if (m_grouped && group(left) != group(right)) {
            return group(left) < group(right) ? -1 : 1;
        }
        const int l = m_order.value(left, m_order.size());
        const int r = m_order.value(right, m_order.size());
        return l == r ? 0 : (l < r ? -1 : 1);
    }

private:
    QHash<QString, int> m_order;
    QJsonObject m_rules;
    bool m_grouped = true;
};
