/*
    SPDX-FileCopyrightText: 2015 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include <optional>

#include "debug.h"
#include "systemtray.h"

#include "plasmoidregistry.h"
#include "sortedsystemtraymodel.h"
#include "statusnotifieritemhost.h"
#include "statusnotifieritemsource.h"
#include "systemtraymodel.h"
#include "systemtraysettings.h"

#include <QGuiApplication>
#include <QMenu>
#include <QMetaMethod>
#include <QMetaObject>
#include <QQueue>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QTimer>
#include <qpa/qplatformscreen.h>

#include <Plasma/Applet>
#include <Plasma/Corona>
#include <Plasma/PluginLoader>

#include <KAcceleratorManager>
#include <KActionCollection>
#include <KSharedConfig>
#include <KConfigLoader>
#include <KWaylandExtras>
#include <KWindowSystem>

using namespace Qt::StringLiterals;

static void showSystemTrayMenuWayland(QMenu *menu, QQuickItem *trayItem, Plasma::Types::Location location)
{
    QWindow *trayWindow = trayItem->window();
    if (!trayWindow) {
        qCWarning(SYSTEM_TRAY) << menu << "has no parent window, this should never happen";
        return;
    }

    menu->winId();
    QWindow *menuWindow = menu->windowHandle();

    const QRect anchorRect = trayItem->mapRectToScene(QRectF(QPoint(0, 0), trayItem->size())).toRect();

    Qt::Edges anchor;
    Qt::Edges gravity;
    switch (location) {
    case Plasma::Types::Location::TopEdge:
        anchor = Qt::BottomEdge;
        gravity = Qt::BottomEdge;

        if (qGuiApp->isLeftToRight()) {
            anchor |= Qt::LeftEdge;
            gravity |= Qt::RightEdge;
        } else {
            anchor |= Qt::RightEdge;
            gravity |= Qt::LeftEdge;
        }
        break;

    case Plasma::Types::Location::BottomEdge:
        anchor = Qt::TopEdge;
        gravity = Qt::TopEdge;

        if (qGuiApp->isLeftToRight()) {
            anchor |= Qt::LeftEdge;
            gravity |= Qt::RightEdge;
        } else {
            anchor |= Qt::RightEdge;
            gravity |= Qt::LeftEdge;
        }
        break;

    case Plasma::Types::Location::LeftEdge:
        anchor = Qt::RightEdge | Qt::TopEdge;
        gravity = Qt::RightEdge | Qt::BottomEdge;
        break;

    case Plasma::Types::Location::RightEdge:
        anchor = Qt::LeftEdge | Qt::TopEdge;
        gravity = Qt::LeftEdge | Qt::BottomEdge;
        break;

    default:
        anchor = Qt::LeftEdge | Qt::BottomEdge;
        gravity = Qt::RightEdge | Qt::BottomEdge;
        break;
    }

    menuWindow->setTransientParent(trayWindow);

    menuWindow->setProperty("_q_waylandPopupAnchorRect", anchorRect);
    menuWindow->setProperty("_q_waylandPopupAnchor", QVariant::fromValue(anchor));
    menuWindow->setProperty("_q_waylandPopupGravity", QVariant::fromValue(gravity));

    menu->popup(trayWindow->screen()->geometry().topLeft());
}

IconTray::IconTray(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
    : Plasma::Containment(parent, data, args)
{
    setHasConfigurationInterface(true);
    setContainmentDisplayHints(Plasma::Types::ContainmentDrawsPlasmoidHeading | Plasma::Types::ContainmentForcesSquarePlasmoids);
}

IconTray::~IconTray()
{
    // When the applet is about to be deleted, delete now to avoid calling loadConfig()
    delete m_settings;
}

void IconTray::init()
{
    // run this before doing any config restore
    // IconTray owns its independent configuration.

    Containment::init();

    initSettingsAndRegistry();

    // we don't want to automatically propagate the activated signal from the Applet to the Containment
    // even if IconTray is of type Containment, it is de facto Applet and should act like one
    connect(this, &Containment::appletAdded, this, [this](Plasma::Applet *applet) {
        disconnect(applet, &Applet::activated, this, &Applet::activated);
    });

    if (KWindowSystem::isPlatformWayland()) {
        auto config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"), KConfig::NoGlobals);
        KConfigGroup kscreenGroup = config->group(QStringLiteral("KScreen"));
        m_xwaylandClientsScale = kscreenGroup.readEntry("XwaylandClientsScale", true);

        m_configWatcher = KConfigWatcher::create(config);
        connect(m_configWatcher.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group, const QByteArrayList &names) {
            if (group.name() == u"KScreen" && names.contains(QByteArrayLiteral("XwaylandClientsScale"))) {
                m_xwaylandClientsScale = group.readEntry("XwaylandClientsScale", true);
            }
        });
    }
}

void IconTray::initSettingsAndRegistry()
{
    if (!m_settings) {
        m_settings = new SystemTraySettings(configScheme(), this);
        connect(m_settings, &SystemTraySettings::enabledPluginsChanged, this, &IconTray::onEnabledAppletsChanged);
    }

    if (!m_plasmoidRegistry) {
        m_plasmoidRegistry = new PlasmoidRegistry(m_settings, this);
        connect(m_plasmoidRegistry, &PlasmoidRegistry::plasmoidEnabled, this, &IconTray::startApplet);
        connect(m_plasmoidRegistry, &PlasmoidRegistry::plasmoidStopped, this, &IconTray::stopApplet);
        connect(m_plasmoidRegistry, &PlasmoidRegistry::plasmoidDisabled, this, &IconTray::stopApplet);
    }
}

void IconTray::initRegistry()
{
    if (m_plasmoidRegistry && !m_registryInitialized) {
        m_plasmoidRegistry->init();
        m_registryInitialized = true;
    }
}

void IconTray::restoreContents(KConfigGroup &group)
{
    if (!isContainment()) {
        qCWarning(SYSTEM_TRAY) << "Loaded as an applet, this shouldn't have happened";
        return;
    }

    KConfigGroup shortcutConfig(&group, u"Shortcuts"_s);
    QString shortcutText = shortcutConfig.readEntryUntranslated("global", QString());
    if (!shortcutText.isEmpty()) {
        setGlobalShortcut(QKeySequence(shortcutText));
    }

    // cache known config group ids for applets
    KConfigGroup cg = group.group(u"Applets"_s);
    for (const QString &group : cg.groupList()) {
        KConfigGroup appletConfig(&cg, group);
        QString plugin = appletConfig.readEntry("plugin");
        if (!plugin.isEmpty()) {
            m_configGroupIds[plugin] = group.toInt();
        }
    }

    initSettingsAndRegistry();
    initRegistry();
}

void IconTray::showPlasmoidMenu(QQuickItem *appletInterface, int x, int y)
{
    if (!appletInterface) {
        return;
    }

    auto *applet = appletInterface->property("_plasma_applet").value<Plasma::Applet *>();

    QPointF pos = appletInterface->mapToScene(QPointF(x, y));

    if (appletInterface->window() && appletInterface->window()->screen()) {
        pos = appletInterface->window()->mapToGlobal(pos.toPoint());
    } else {
        pos = QPoint();
    }

    auto *desktopMenu = new QMenu;
    // Breeze and Oxygen have rounded corners on menus. They set this attribute
    // in polish() but at that time the underlying surface has already been
    // created so setting this flag makes no difference anymore (Bug 385311)
    desktopMenu->setAttribute(Qt::WA_TranslucentBackground);
    connect(this, &QObject::destroyed, desktopMenu, &QMenu::close);
    desktopMenu->setAttribute(Qt::WA_DeleteOnClose);

    // this is a workaround where Qt will fail to realize a mouse has been released

    // this happens if a window which does not accept focus spawns a new window that takes focus and X grab
    // whilst the mouse is depressed
    // https://bugreports.qt.io/browse/QTBUG-59044
    // this causes the next click to go missing

    // by releasing manually we avoid that situation
    auto ungrabMouseHack = [appletInterface]() {
        if (appletInterface->window() && appletInterface->window()->mouseGrabberItem()) {
            appletInterface->window()->mouseGrabberItem()->ungrabMouse();
        }
    };

    QTimer::singleShot(0, appletInterface, ungrabMouseHack);
    // end workaround

    Q_EMIT applet->contextualActionsAboutToShow();
    const auto contextActions = applet->contextualActions();
    for (QAction *action : contextActions) {
        if (action) {
            desktopMenu->addAction(action);
        }
    }

    if (applet->internalAction(QStringLiteral("configure"))) {
        desktopMenu->addAction(applet->internalAction(QStringLiteral("configure")));
    }

    if (desktopMenu->isEmpty()) {
        delete desktopMenu;
        return;
    }

    KAcceleratorManager::manage(desktopMenu);

    showSystemTrayMenuWayland(desktopMenu, appletInterface, location());
}

QPointF IconTray::popupPosition(QQuickItem *visualParent, int x, int y)
{
    if (!visualParent) {
        return {0, 0};
    }

    QPointF pos = visualParent->mapToScene(QPointF(x, y));

    QQuickWindow *const window = visualParent->window();
    if (window && window->screen()) {
        pos = window->mapToGlobal(pos.toPoint());

        if (KWindowSystem::isPlatformWayland()) {
            if (!m_xwaylandClientsScale) {
                return pos;
            }

            const qreal devicePixelRatio = window->devicePixelRatio();

            if (QGuiApplication::screens().size() == 1) {
                return pos * devicePixelRatio;
            }

            const QRect geometry = window->screen()->geometry();
            const QRect nativeGeometry = window->screen()->handle()->geometry();
            const QPointF nativeGlobalPosOnCurrentScreen = (pos - geometry.topLeft()) * devicePixelRatio;

            return nativeGeometry.topLeft() + nativeGlobalPosOnCurrentScreen;
        }
    }

    return QPoint();
}

bool IconTray::isSystemTrayApplet(const QString &appletId)
{
    if (m_plasmoidRegistry) {
        return m_plasmoidRegistry->isSystemTrayApplet(appletId);
    }
    return false;
}

SystemTrayModel *IconTray::systemTrayModel()
{
    if (!m_systemTrayModel) {
        // Ensure settings and registry are initialized before creating the model.
        // This can be called from QML property binding before init() completes.
        initSettingsAndRegistry();

        m_systemTrayModel = new SystemTrayModel(this);

        m_plasmoidModel = new PlasmoidModel(m_settings, m_plasmoidRegistry, m_systemTrayModel);
        connect(this, &IconTray::appletAdded, m_plasmoidModel, &PlasmoidModel::addApplet);
        connect(this, &IconTray::appletRemoved, m_plasmoidModel, &PlasmoidModel::removeApplet);

        // Initialize registry after model is created so signals are properly connected
        initRegistry();

        // Add any applets that already exist
        for (auto applet : applets()) {
            m_plasmoidModel->addApplet(applet);
        }

        m_statusNotifierModel = new StatusNotifierModel(m_settings, m_systemTrayModel);

        m_systemTrayModel->addSourceModel(m_plasmoidModel);
        m_systemTrayModel->addSourceModel(m_statusNotifierModel);
        m_systemTrayModel->addSourceModel(new BackgroundAppsFilteredModel(new BackgroundAppsModel(m_settings, m_systemTrayModel), m_systemTrayModel));
    }

    return m_systemTrayModel;
}

QAbstractItemModel *IconTray::sortedSystemTrayModel()
{
    if (!m_sortedSystemTrayModel) {
        m_sortedSystemTrayModel = new SortedSystemTrayModel(SortedSystemTrayModel::SortingType::IconTray, this);
        m_sortedSystemTrayModel->setSourceModel(systemTrayModel());
        const auto updatePreferences = [this]() {
            m_sortedSystemTrayModel->setPreferences(configScheme()->property(QStringLiteral("itemOrder")).toStringList(),
                                                   configScheme()->property(QStringLiteral("itemRules")).toString(),
                                                   configScheme()->property(QStringLiteral("groupItems")).toBool());
        };
        connect(m_settings, &SystemTraySettings::configurationChanged, m_sortedSystemTrayModel, updatePreferences);
        updatePreferences();
    }
    return m_sortedSystemTrayModel;
}

QAbstractItemModel *IconTray::configSystemTrayModel()
{
    if (!m_configSystemTrayModel) {
        m_configSystemTrayModel = new SortedSystemTrayModel(SortedSystemTrayModel::SortingType::ConfigurationPage, this);
        m_configSystemTrayModel->setSourceModel(systemTrayModel());
    }
    return m_configSystemTrayModel;
}

void IconTray::onEnabledAppletsChanged()
{
    // remove all that are not allowed anymore
    const auto appletsList = applets();
    for (Plasma::Applet *applet : appletsList) {
        // Here it should always be valid.
        // for some reason it not always is.
        if (!applet->pluginMetaData().isValid()) {
            applet->config().parent().deleteGroup();
            delete applet;
        } else {
            const QString task = applet->pluginMetaData().pluginId();
            if (!m_settings->isEnabledPlugin(task)) {
                // in those cases we do delete the applet config completely
                // as they were explicitly disabled by the user
                applet->config().parent().deleteGroup();
                delete applet;
                m_configGroupIds.remove(task);
            }
        }
    }
}

void IconTray::startApplet(const QString &pluginId)
{
    const auto appletsList = applets();
    for (Plasma::Applet *applet : appletsList) {
        if (!applet->pluginMetaData().isValid()) {
            continue;
        }

        // only allow one instance per applet
        if (pluginId == applet->pluginMetaData().pluginId()) {
            // Applet::destroy doesn't delete the applet from Containment::applets in the same event
            // potentially a dbus activated service being restarted can be added in this time.
            if (!applet->destroyed()) {
                return;
            }
        }
    }

    qCDebug(SYSTEM_TRAY) << "Adding applet:" << pluginId;

    // known one, recycle the id to reuse old config
    if (m_configGroupIds.contains(pluginId)) {
        auto applet = Plasma::PluginLoader::self()->loadApplet(pluginId, m_configGroupIds.value(pluginId), QVariantList());
        // this should never happen unless explicitly wrong config is hand-written or
        //(more likely) a previously added applet is uninstalled
        if (!applet) {
            qCWarning(SYSTEM_TRAY) << "Unable to find applet" << pluginId;
            return;
        }
        applet->setProperty("org.kde.plasma:force-create", true);
        addApplet(std::move(applet));
        // create a new one automatic id, new config group
    } else {
        auto applet = createApplet(pluginId, QVariantList() << u"org.kde.plasma:force-create"_s);
        if (applet) {
            m_configGroupIds[pluginId] = applet->id();
        }
    }
}

void IconTray::stopApplet(const QString &pluginId)
{
    const auto appletsList = applets();
    for (Plasma::Applet *applet : appletsList) {
        if (applet->pluginMetaData().isValid() && pluginId == applet->pluginMetaData().pluginId()) {
            delete applet;
        }
    }
}

void IconTray::stackItemBefore(QQuickItem *newItem, QQuickItem *beforeItem)
{
    if (!newItem || !beforeItem) {
        return;
    }
    newItem->stackBefore(beforeItem);
}

void IconTray::stackItemAfter(QQuickItem *newItem, QQuickItem *afterItem)
{
    if (!newItem || !afterItem) {
        return;
    }
    newItem->stackAfter(afterItem);
}

void IconTray::activate(const QString &service, QPoint pos, QQuickItem *statusNotifierIcon)
{
    const auto source = StatusNotifierItemHost::self()->itemForService(service);

    if (!source) {
        qCWarning(SYSTEM_TRAY) << "activate: Could not find item for service" << service;
        return;
    }

    connect(
        source,
        &StatusNotifierItemSource::activateResult,
        this,
        [this, service, pos, statusNotifierIcon](bool res) {
            if (!res) {
                // On error try to invoke the context menu.
                // Workaround primarily for apps using libappindicator.
                openContextMenu(service, pos, statusNotifierIcon);
            }
        },
        Qt::SingleShotConnection);

    QWindow *window = nullptr;

    auto tokenFuture = KWaylandExtras::xdgActivationToken(window, {});
    tokenFuture.then(source, [source, service, pos](const QString &token) {
        source->provideXdgActivationToken(token);
        source->activate(pos.x(), pos.y());
    });
}

void IconTray::secondaryActivate(const QString &service, QPoint pos)
{
    const auto source = StatusNotifierItemHost::self()->itemForService(service);

    if (!source) {
        qCWarning(SYSTEM_TRAY) << "secondaryActivate: Could not find item for service" << service;
        return;
    }

    QWindow *window = nullptr;

    auto tokenFuture = KWaylandExtras::xdgActivationToken(window, {});
    tokenFuture.then(source, [source, pos](const QString &token) {
        source->provideXdgActivationToken(token);
        source->secondaryActivate(pos.x(), pos.y());
    });
}

void IconTray::openContextMenu(const QString &service, QPoint pos, QQuickItem *statusNotifierIcon)
{
    const auto source = StatusNotifierItemHost::self()->itemForService(service);

    if (!source) {
        qCWarning(SYSTEM_TRAY) << "openContextMenu: Could not find item for service" << service;
        return;
    }

    connect(
        source,
        &StatusNotifierItemSource::contextMenuReady,
        this,
        [this, statusNotifierIcon = QPointer<QQuickItem>{statusNotifierIcon}](QMenu *menu) {
            if (statusNotifierIcon && menu && !menu->isEmpty()) {
                KAcceleratorManager::manage(menu);
                showSystemTrayMenuWayland(menu, statusNotifierIcon, location());
            }
        },
        Qt::SingleShotConnection);

    QWindow *window = nullptr;

    auto tokenFuture = KWaylandExtras::xdgActivationToken(window, {});
    tokenFuture.then(source, [source, pos](const QString &token) {
        source->provideXdgActivationToken(token);
        source->contextMenu(pos.x(), pos.y());
    });
}

void IconTray::scroll(const QString &service, int delta, const QString &direction)
{
    const auto source = StatusNotifierItemHost::self()->itemForService(service);

    if (!source) {
        qCWarning(SYSTEM_TRAY) << "scroll: Could not find item for service" << service;
        return;
    }

    source->scroll(delta, direction);
}

K_PLUGIN_CLASS_WITH_JSON(IconTray, "metadata.json")

#include "systemtray.moc"

#include "moc_systemtray.cpp"
