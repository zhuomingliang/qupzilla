/* ============================================================
* QupZilla - WebKit based browser
* Copyright (C) 2010-2013  David Rosca <nowrep@gmail.com>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
* ============================================================ */
#include "pluginsmanager.h"
#include "ui_pluginslist.h"
#include "pluginproxy.h"
#include "mainapplication.h"
#include "plugininterface.h"
#include "pluginlistdelegate.h"
#include "qztools.h"
#include "settings.h"

#include <QInputDialog>
#include <QMessageBox>
#include <QTimer>

PluginsManager::PluginsManager(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::PluginsList)
    , m_loaded(false)
{
    ui->setupUi(this);

    //Application Extensions
    Settings settings;
    settings.beginGroup("Plugin-Settings");
    bool appPluginsEnabled = settings.value("EnablePlugins", DEFAULT_ENABLE_PLUGINS).toBool();
    settings.endGroup();

    ui->allowAppPlugins->setChecked(appPluginsEnabled);
    ui->list->setEnabled(appPluginsEnabled);

    connect(ui->butSettings, SIGNAL(clicked()), this, SLOT(settingsClicked()));
    connect(ui->list, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)), this, SLOT(currentChanged(QListWidgetItem*)));
    connect(ui->list, SIGNAL(itemChanged(QListWidgetItem*)), this, SLOT(itemChanged(QListWidgetItem*)));
    connect(ui->allowAppPlugins, SIGNAL(clicked(bool)), this, SLOT(allowAppPluginsChanged(bool)));

    ui->list->setItemDelegate(new PluginListDelegate(ui->list));

    //WebKit Plugins
    connect(ui->add, SIGNAL(clicked()), this, SLOT(addWhitelist()));
    connect(ui->remove, SIGNAL(clicked()), this, SLOT(removeWhitelist()));
    connect(ui->allowClick2Flash, SIGNAL(clicked(bool)), this, SLOT(allowC2FChanged(bool)));

    ui->whitelist->sortByColumn(-1);

    settings.beginGroup("ClickToFlash");
    QStringList whitelist = mApp->plugins()->c2f_getWhiteList();
    ui->allowClick2Flash->setChecked(settings.value("Enable", true).toBool());
    settings.endGroup();
    foreach (const QString &site, whitelist) {
        QTreeWidgetItem* item = new QTreeWidgetItem(ui->whitelist);
        item->setText(0, site);
    }

    allowC2FChanged(ui->allowClick2Flash->isChecked());
}

void PluginsManager::load()
{
    if (!m_loaded) {
        refresh();
        m_loaded = true;
    }
}

void PluginsManager::addWhitelist()
{
    QString site = QInputDialog::getText(this, tr("Add site to whitelist"), tr("Server without http:// (ex. youtube.com)"));
    if (site.isEmpty()) {
        return;
    }

    mApp->plugins()->c2f_addWhitelist(site);
    ui->whitelist->insertTopLevelItem(0, new QTreeWidgetItem(QStringList(site)));
}

void PluginsManager::removeWhitelist()
{
    QTreeWidgetItem* item = ui->whitelist->currentItem();
    if (!item) {
        return;
    }

    mApp->plugins()->c2f_removeWhitelist(item->text(0));
    delete item;
}

void PluginsManager::save()
{
    if (!m_loaded) {
        return;
    }

    QStringList allowedPlugins;
    for (int i = 0; i < ui->list->count(); i++) {
        QListWidgetItem* item = ui->list->item(i);

        if (item->checkState() == Qt::Checked) {
            const Plugins::Plugin plugin = item->data(Qt::UserRole + 10).value<Plugins::Plugin>();

            allowedPlugins.append(plugin.fullPath);
        }
    }

    Settings settings;
    settings.beginGroup("Plugin-Settings");
    settings.setValue("EnablePlugins", ui->allowAppPlugins->isChecked());
    settings.setValue("AllowedPlugins", allowedPlugins);
    settings.endGroup();
}

void PluginsManager::allowAppPluginsChanged(bool state)
{
    ui->list->setEnabled(state);

    if (!state) {
        for (int i = 0; i < ui->list->count(); i++) {
            QListWidgetItem* item = ui->list->item(i);

            if (item->checkState() == Qt::Checked) {
                item->setCheckState(Qt::Unchecked);
            }
        }
    }

    refresh();
}

void PluginsManager::allowC2FChanged(bool state)
{
    Settings settings;
    settings.beginGroup("ClickToFlash");
    settings.setValue("Enable", state);
    settings.endGroup();

    ui->whitelist->setEnabled(state);
    ui->add->setEnabled(state);
    ui->remove->setEnabled(state);

    mApp->plugins()->c2f_setEnabled(state);
}

void PluginsManager::refresh()
{
    if (!ui->allowAppPlugins->isChecked()) {
        return;
    }

    ui->list->clear();
    ui->butSettings->setEnabled(false);
    disconnect(ui->list, SIGNAL(itemChanged(QListWidgetItem*)), this, SLOT(itemChanged(QListWidgetItem*)));

    const QList<Plugins::Plugin> &allPlugins = mApp->plugins()->getAvailablePlugins();

    foreach (const Plugins::Plugin &plugin, allPlugins) {
        PluginSpec spec = plugin.pluginSpec;

        QListWidgetItem* item = new QListWidgetItem(ui->list);
        QIcon icon = QIcon(spec.icon);
        if (icon.isNull()) {
            icon = QIcon(":/icons/preferences/extension.png");
        }
        item->setIcon(icon);

        QString pluginInfo = QString("<b>%1</b> %2<br/><i>%3</i><br/>%4").arg(spec.name, spec.version, QzTools::escape(spec.author), spec.info);
        item->setToolTip(pluginInfo);

        item->setText(spec.name);
        item->setData(Qt::UserRole, spec.version);
        item->setData(Qt::UserRole + 1, spec.info);
        item->setData(Qt::UserRole + 2, spec.description);

        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(plugin.isLoaded() ? Qt::Checked : Qt::Unchecked);
        item->setData(Qt::UserRole + 10, QVariant::fromValue(plugin));

        ui->list->addItem(item);
    }

    sortItems();

    connect(ui->list, SIGNAL(itemChanged(QListWidgetItem*)), this, SLOT(itemChanged(QListWidgetItem*)));
}

void PluginsManager::sortItems()
{
    ui->list->sortItems();

    bool itemMoved;
    do {
        itemMoved = false;
        for (int i = 0; i < ui->list->count(); ++i) {
            QListWidgetItem* topItem = ui->list->item(i);
            QListWidgetItem* bottomItem = ui->list->item(i + 1);
            if (!topItem || !bottomItem) {
                continue;
            }

            if (topItem->checkState() == Qt::Unchecked && bottomItem->checkState() == Qt::Checked) {
                QListWidgetItem* item = ui->list->takeItem(i + 1);
                ui->list->insertItem(i, item);
                itemMoved = true;
            }
        }
    }
    while (itemMoved);
}

void PluginsManager::currentChanged(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

    const Plugins::Plugin plugin = item->data(Qt::UserRole + 10).value<Plugins::Plugin>();
    bool showSettings = plugin.pluginSpec.hasSettings;

    if (!plugin.isLoaded()) {
        showSettings = false;
    }

    ui->butSettings->setEnabled(showSettings);
}

void PluginsManager::itemChanged(QListWidgetItem* item)
{
    if (!item) {
        return;
    }

    Plugins::Plugin plugin = item->data(Qt::UserRole + 10).value<Plugins::Plugin>();

    if (item->checkState() == Qt::Checked) {
        mApp->plugins()->loadPlugin(&plugin);
    }
    else {
        mApp->plugins()->unloadPlugin(&plugin);
    }

    disconnect(ui->list, SIGNAL(itemChanged(QListWidgetItem*)), this, SLOT(itemChanged(QListWidgetItem*)));

    if (item->checkState() == Qt::Checked && !plugin.isLoaded()) {
        item->setCheckState(Qt::Unchecked);
        QMessageBox::critical(this, tr("Error!"), tr("Cannot load extension!"));
    }

    item->setData(Qt::UserRole + 10, QVariant::fromValue(plugin));

    connect(ui->list, SIGNAL(itemChanged(QListWidgetItem*)), this, SLOT(itemChanged(QListWidgetItem*)));


    currentChanged(ui->list->currentItem());
}

void PluginsManager::settingsClicked()
{
    QListWidgetItem* item = ui->list->currentItem();
    if (!item || item->checkState() == Qt::Unchecked) {
        return;
    }

    Plugins::Plugin plugin = item->data(Qt::UserRole + 10).value<Plugins::Plugin>();

    if (!plugin.isLoaded()) {
        mApp->plugins()->loadPlugin(&plugin);

        item->setData(Qt::UserRole + 10, QVariant::fromValue(plugin));
    }

    if (plugin.isLoaded() && plugin.pluginSpec.hasSettings) {
        plugin.instance->showSettings(this);
    }
}

PluginsManager::~PluginsManager()
{
    delete ui;
}
