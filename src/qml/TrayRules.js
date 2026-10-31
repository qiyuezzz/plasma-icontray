// SPDX-License-Identifier: GPL-2.0-or-later
.pragma library

function parse(json) {
    try {
        const value = JSON.parse(json || "{}");
        return value && typeof value === "object" && !Array.isArray(value) ? value : {};
    } catch (error) {
        return {};
    }
}

function rule(rules, id) {
    const r = Object.prototype.hasOwnProperty.call(rules, id) ? rules[id] : {};
    const fallback = id.indexOf("org.kde.plasma.") === 0 ? 2 : 0;
    const group = r && Number.isInteger(r.group) && r.group >= 0 && r.group <= 2 ? r.group : fallback;
    const scale = r && typeof r.scale === "number" && Number.isFinite(r.scale) ? Math.max(50, Math.min(150, r.scale)) : 100;
    return {group: group === 1 ? 0 : group, scale: scale, icon: r && typeof r.icon === "string" ? r.icon : ""};
}

function update(json, id, key, value) {
    const rules = parse(json);
    const next = rule(rules, id);
    next[key] = value;
    // defineProperty also supports unusual IDs such as "__proto__".
    Object.defineProperty(rules, id, {value: next, enumerable: true, configurable: true, writable: true});
    return JSON.stringify(rules);
}

function compare(a, b, order, rules, grouped) {
    const groupDelta = grouped ? rule(rules, a.itemId).group - rule(rules, b.itemId).group : 0;
    if (groupDelta !== 0) return groupDelta;
    const ai = order.indexOf(a.itemId);
    const bi = order.indexOf(b.itemId);
    const delta = (ai < 0 ? order.length : ai) - (bi < 0 ? order.length : bi);
    if (delta !== 0) return delta;
    // Preserve the source model's native category/name ordering for new entries.
    return a.sourceIndex - b.sourceIndex;
}

// Move only the visible source ID. Keep offline IDs and their relative order.
function move(order, visibleIds, from, to) {
    if (from === to || from < 0 || to < 0 || from >= visibleIds.length || to >= visibleIds.length) return order.slice();
    const result = order.filter((id, i, all) => id && all.indexOf(id) === i);
    visibleIds.forEach(id => { if (result.indexOf(id) < 0) result.push(id); });
    const moving = visibleIds[from];
    result.splice(result.indexOf(moving), 1);
    const target = result.indexOf(visibleIds[to]);
    result.splice(target + (from < to ? 1 : 0), 0, moving);
    return result;
}
