/*
 * Crossfire -- cooperative multi-player graphical RPG and adventure game
 *
 * Copyright (c) 2022 the Crossfire Development Team
 *
 * Crossfire is free software and comes with ABSOLUTELY NO WARRANTY. You are
 * welcome to redistribute it under certain conditions. For details, please
 * see COPYING and LICENSE.
 *
 * The authors can be reached via e-mail at <crossfire@metalforge.org>.
 */

#include <Qt>
#include <QtWidgets>
#include <memory>

#include <CREMainWindow.h>
#include <CREResourcesWindow.h>
#include "CREMapInformationManager.h"
#include "CREExperienceWindow.h"
#include "MessageManager.h"
#include "CREReportDisplay.h"
#include "CREPixmap.h"
#include "CRESmoothFaceMaker.h"
#include "CREHPBarMaker.h"
#include "ResourcesManager.h"
#include "CRECombatSimulator.h"
#include "CREHPBarMaker.h"
#include "scripts/ScriptFileManager.h"
#include "FaceMakerDialog.h"
#include "EditMonstersDialog.h"
#include "random_maps/RandomMap.h"

#include "global.h"
#include "sproto.h"
#include "image.h"
#include "assets.h"
#include "AssetsManager.h"
#include "CRESettings.h"
#include "LicenseManager.h"
#include "AllAssets.h"
#include "assets/AssetModel.h"
#include "ChangesDock.h"
#include "HelpManager.h"
#include "sounds/SoundsDialog.h"

const char *AssetWrapper::tipProperty = "_cre_internal";

CREMainWindow::CREMainWindow(const QString &helpRoot)
{
    myArea = new QMdiArea();
    setCentralWidget(myArea);
    myArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    myArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    myResourcesManager = new ResourcesManager();
    myResourcesManager->load();

    myMessageManager = new MessageManager(nullptr);
    myMessageManager->loadMessages();
    myScriptManager = new ScriptFileManager(nullptr);

    myMapManager = new CREMapInformationManager(this, myMessageManager, myScriptManager);
    connect(myMapManager, SIGNAL(browsingMap(const QString&)), this, SLOT(browsingMap(const QString&)));
    connect(myMapManager, SIGNAL(finished()), this, SLOT(browsingFinished()));
    connect(myMapManager, SIGNAL(mapAdded(CREMapInformation *)), this, SLOT(mapAdded(CREMapInformation *)));
    myResourcesManager->setMapInformationManager(myMapManager);

    CREPixmap::init();

    myAssets = new AllAssets(myResourcesManager, myScriptManager, myMessageManager);
    myModel = new AssetModel(myAssets, this);
    myMessageManager->setDisplayParent(myAssets);
    myScriptManager->setDisplayParent(myAssets);

    myHelpManager = new HelpManager(helpRoot);

    createActions();
    createMenus();

    statusBar()->showMessage(tr("Ready"));
    myMapBrowseStatus = new QLabel(tr("Browsing maps..."));
    statusBar()->addPermanentWidget(myMapBrowseStatus);

    setWindowTitle(tr("Crossfire Resource Editor"));

    fillFacesets();

    myChanges = new ChangesDock(myHelpManager, this);
    addDockWidget(Qt::RightDockWidgetArea, myChanges);

    myMapManager->start();
    myHelpManager->setupData();

    CRESettings settings;
    if (settings.storeWindowsState()) {
        restoreGeometry(settings.mainWindowGeometry());

        int count = settings.subWindowCount();
        for (int idx = 0; idx < count; idx++) {
            int type = settings.subWindowType(idx);
            if (type == -2) {
                onOpenExperience(settings.subWindowPosition(idx));
            } else {
                doResourceWindow(type, settings.subWindowPosition(idx));
            }
        }
    }
}

void CREMainWindow::closeEvent(QCloseEvent* event)
{
    if (myResourcesManager->hasPendingChanges()) {
        if (QMessageBox::question(this, tr("Discard changes?"), tr("You have unsaved changes, really discard them?")) != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }

    CRESettings settings;
    if (settings.storeWindowsState()) {
        auto windows = myArea->subWindowList();
        settings.setSubWindowCount(windows.size());
        for (int idx = 0; idx < windows.size(); idx++) {
            settings.setSubWindowPosition(idx, windows[idx]->saveGeometry());
            auto widget = windows[idx]->widget();
            auto crew = dynamic_cast<CREResourcesWindow *>(widget);
            if (crew != nullptr) {
                settings.setSubWindowType(idx, crew->rootIndex());
            }
            auto ew = dynamic_cast<CREExperienceWindow *>(widget);
            if (ew) {
                settings.setSubWindowType(idx, -2);
            }
        }
        settings.setMainWindowGeometry(saveGeometry());
    }

    myArea->closeAllSubWindows();
    delete myArea;
    QMainWindow::closeEvent(event);

    myMapManager->cancel();
    delete myMapManager;
    delete myMessageManager;
    delete myResourcesManager;
    cleanup();
}

template <typename F>
QAction *CREMainWindow::createAction(const QString &title, const QString &statusTip, F functor, bool waitMaps) {
    auto action = createAction(title, statusTip);
    if (waitMaps) {
        connect(action, &QAction::triggered, [this, functor] {
            if (!myMapManager->browseFinished()) {
                QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
                QProgressDialog prog(this);
                prog.setLabelText(tr("Waiting for maps browing to finish..."));
                prog.setWindowModality(Qt::ApplicationModal);
                prog.setRange(0, 0);
                prog.setVisible(true);
                prog.setValue(0);
                while (!myMapManager->browseFinished())
                {
                    if (prog.wasCanceled()) {
                        QApplication::restoreOverrideCursor();
                        return;
                    }
                    QApplication::processEvents();
                    QThread::msleep(1);
                }
                QApplication::restoreOverrideCursor();
            }
            functor();
        });
    } else {
        connect(action, &QAction::triggered, functor);
    }
    return action;
}

QAction *CREMainWindow::createAction(const QString &title, const QString &statusTip) {
    auto action = new QAction(title, this);
    action->setStatusTip(statusTip);
    return action;
}

void CREMainWindow::createActions()
{
    mySaveFormulae = new QAction(tr("Formulae"), this);
    mySaveFormulae->setEnabled(false);
    connect(mySaveFormulae, SIGNAL(triggered()), this, SLOT(onSaveFormulae()));

    myClearMapCache = new QAction(tr("Clear map cache"), this);
    myClearMapCache->setStatusTip(tr("Force a refresh of all map information at next start."));
    connect(myClearMapCache, SIGNAL(triggered()), this, SLOT(onClearCache()));
    /* can't clear map cache while collecting information */
    myClearMapCache->setEnabled(false);

    myToolFacesetUseFallback = new QAction(tr("Use set fallback for missing faces"), this);
    connect(myToolFacesetUseFallback, SIGNAL(triggered()), this, SLOT(onToolFacesetUseFallback()));
    myToolFacesetUseFallback->setCheckable(true);
    myToolFacesetUseFallback->setChecked(true);
}

void CREMainWindow::createMenus()
{
    myOpenMenu = menuBar()->addMenu(tr("&Open"));

    auto add = [this] (int index, const QString &name, const QString &tip) {
        QAction* action = new QAction(name, this);
        action->setStatusTip(tip);
        action->setData(static_cast<int>(index));
        connect(action, &QAction::triggered, [this, index] () { doResourceWindow(index); });
        myOpenMenu->addAction(action);
    };
    add(-1, tr("Assets"), tr("Display all defined assets, except the experience table."));
    for (int asset = 0; asset < myAssets->childrenCount(); asset++) {
        add(asset, myAssets->child(asset)->displayName(), myAssets->child(asset)->property(AssetWrapper::tipProperty).toString());
    }

    myOpenMenu->addAction(createAction(tr("Experience"), tr("Display the experience table."), [this] { onOpenExperience(); }));

    myOpenMenu->addSeparator();
    QAction* exit = myOpenMenu->addAction(tr("&Exit"));
    exit->setStatusTip(tr("Close the application."));
    connect(exit, SIGNAL(triggered()), this, SLOT(close()));

    mySaveMenu = menuBar()->addMenu(tr("&Save"));
    mySaveMenu->addAction(mySaveFormulae);
    mySaveMenu->addAction(createAction(tr("Quests"), tr("Save all modified quests to disk."), [this] { onSaveQuests(); }));
    mySaveMenu->addAction(createAction(tr("Dialogs"), tr("Save all modified NPC dialogs."), [this] { onSaveMessages(); }));
    mySaveMenu->addAction(createAction(tr("Archetypes"), tr("Save all modified archetypes."), [this] { myResourcesManager->saveArchetypes(); }));
    mySaveMenu->addAction(createAction(tr("Treasures"), tr("Save all modified treasures."), [this] { myResourcesManager->saveTreasures(); }));
    mySaveMenu->addAction(createAction(tr("General messages"), tr("Save all modified general messages."), [this] { myResourcesManager->saveGeneralMessages(); }));
    mySaveMenu->addAction(createAction(tr("Artifacts"), tr("Save all modified artifacts."), [this] { myResourcesManager->saveArtifacts(); }));

    QMenu* reportMenu = menuBar()->addMenu(tr("&Reports"));
    reportMenu->addAction(createAction(tr("Faces and animations report"), tr("Show faces and animations which are used by multiple archetypes, or not used."), [this] { onReportDuplicate(); }));
    reportMenu->addAction(createAction(tr("Spell damage"), tr("Display damage by level for some spells."), [this] () { onReportSpellDamage(); }, true));
    reportMenu->addAction(createAction(tr("Alchemy"), tr("Display alchemy formulae, in a table."), [this] { onReportAlchemy(); }));
    reportMenu->addAction(createAction(tr("Alchemy graph"), tr("Export alchemy relationship as a DOT file."), [this] { onReportAlchemyGraph(); }));
    reportMenu->addAction(createAction(tr("Spells"), tr("Display all spells, in a table."), [this] { onReportSpells(); }));
    reportMenu->addAction(createAction(tr("Player vs monsters"), tr("Compute statistics related to player vs monster combat."), [=] { onReportPlayer(); }, true));
    reportMenu->addAction(createAction(tr("Summoned pets statistics"), tr("Display wc, hp, speed and other statistics for summoned pets."), [=] { onReportSummon(); }));
    reportMenu->addAction(createAction(tr("Shop specialization"), tr("Display the list of shops and their specialization for items."), [=] { onReportShops(); }, true));
    reportMenu->addAction(createAction(tr("Quest solved by players"), tr("Display quests the players have solved."), [=] { onReportQuests(); }, true));
    reportMenu->addAction(createAction(tr("Materials"), tr("Display all materials with their properties."), [this] { onReportMaterials(); }));
    reportMenu->addAction(createAction(tr("Unused archetypes"), tr("Display all archetypes which seem unused."), [=] { onReportArchetypes(); }, true));
    reportMenu->addAction(createAction(tr("Licenses checks"), tr("Check for licenses inconsistencies."), [this] { onReportLicenses(); }));
    reportMenu->addAction(createAction(tr("Map reset groups"), tr("List map reset groups."), [this] { onReportResetGroups(); }, true));

    myToolsMenu = menuBar()->addMenu(tr("&Tools"));
    myToolsMenu->addAction(createAction(tr("Edit monsters"), tr("Edit monsters in a table."), [this] { onToolEditMonsters(); }));
    myToolsMenu->addAction(createAction(tr("Generate smooth face base"), tr("Generate the basic smoothed picture for a face."), [this] { onToolSmooth(); }));
    myToolsMenu->addAction(createAction(tr("Generate HP bar"), tr("Generate faces for a HP bar."), [this] { onToolBarMaker(); }));
    myToolsMenu->addAction(createAction(tr("Combat simulator"), tr("Simulate fighting between two objects."), [this] { onToolCombatSimulator(); }));
    myToolsMenu->addAction(createAction(tr("Generate face variants"), tr("Generate faces by changing colors of existing faces."), [this] { onToolFaceMaker(); }));
    myToolsMenu->addAction(myClearMapCache);
    myToolsMenu->addAction(createAction(tr("Reload assets"), tr("Reload all assets from the data directory."), [this] { onToolReloadAssets(); }));
    myToolsMenu->addAction(createAction(tr("Sounds"), tr("Display defined sounds and associated files."), [this] { onToolSounds(); }));

    CRESettings set;

    myWindows = menuBar()->addMenu(tr("&Windows"));
    connect(myWindows, SIGNAL(aboutToShow()), this, SLOT(onWindowsShowing()));
    auto store = createAction(tr("Restore windows positions at launch"), tr("If enabled then opened windows are automatically opened again when the application starts"));
    store->setCheckable(true);
    store->setChecked(set.storeWindowsState());
    myWindows->addAction(store);
    connect(store, &QAction::triggered, [store] {
        CRESettings set;
        set.setStoreWindowState(store->isChecked());
    } );
    
    myWindows->addAction(createAction(tr("Close current window"), tr("Close the currently focused window"), [this] { myArea->closeActiveSubWindow(); }));
    myWindows->addAction(createAction(tr("Close all windows"), tr("Close all opened windows"), [=] { myArea->closeAllSubWindows(); }));
    myWindows->addAction(createAction(tr("Tile windows"), tr("Tile all windows"), [=] { myArea->tileSubWindows(); }));
    myWindows->addAction(createAction(tr("Cascade windows"), tr("Cascade all windows"), [=] { myArea->cascadeSubWindows(); }));

    auto sep = new QAction(this);
    sep->setSeparator(true);
    myWindows->addAction(sep);

    auto helpMenu = menuBar()->addMenu(tr("&Help"));
    auto help = createAction(tr("Help"), tr("CRE Help"), [=] { myHelpManager->displayHelp(); });
    help->setShortcut(Qt::Key_F1);
    helpMenu->addAction(help);

    helpMenu->addAction(createAction(tr("About"), tr("About CRE"), [=] () { QMessageBox::about(this, tr("About CRE"), tr("Crossfire Resource Editor")); }));

    CRESettings settings;
    auto show = createAction(tr("Show changes after updating"), tr("If checked, then show latest changes at first startup after an update"));
    show->setCheckable(true);
    show->setChecked(settings.showChanges());
    helpMenu->addAction(show);
    connect(show, &QAction::triggered, [&] (bool checked) {
        CRESettings settings;
        settings.setShowChanges(checked);
    });

    helpMenu->addAction(createAction(tr("Changes"), tr("Display CRE changes"), [=] () { myChanges->setVisible(true); }));
}

void CREMainWindow::doResourceWindow(int assets, const QByteArray& position)
{
    QModelIndex root;
    if (assets != -1) {
        root = myModel->index(assets, 0, QModelIndex());
    }

    QWidget* resources = new CREResourcesWindow(myMapManager, myMessageManager, myResourcesManager, myScriptManager, myModel, root, this);
    resources->setAttribute(Qt::WA_DeleteOnClose);
    connect(this, SIGNAL(updateFilters()), resources, SLOT(updateFilters()));
    connect(resources, SIGNAL(filtersModified()), this, SLOT(onFiltersModified()));
    auto widget = myArea->addSubWindow(resources);
    widget->setAttribute(Qt::WA_DeleteOnClose);
    if (position.isEmpty()) {
        if (myArea->subWindowList().size() == 1) {
            widget->setWindowState(Qt::WindowMaximized);
        }
    } else {
        widget->restoreGeometry(position);
    }
    resources->show();
}

void CREMainWindow::onOpenExperience(const QByteArray& position)
{
    QWidget* experience = new CREExperienceWindow();
    auto widget = myArea->addSubWindow(experience);
    if (!position.isEmpty()) {
        widget->restoreGeometry(position);
    }
    experience->show();
}

void CREMainWindow::fillFacesets()
{
    CRESettings settings;
    const QString select = settings.facesetToDisplay();
    const bool use = settings.facesetUseFallback();

    QMenu *fs = myToolsMenu->addMenu(tr("Facesets"));
    myFacesetsGroup = new QActionGroup(this);
    connect(myFacesetsGroup, SIGNAL(triggered(QAction*)), this, SLOT(onToolFaceset(QAction*)));
    getManager()->facesets()->each([&fs, &select, this] (face_sets *f)
    {
        QAction *a = new QAction(f->fullname, fs);
        a->setCheckable(true);
        a->setData(f->prefix);
        fs->addAction(a);
        myFacesetsGroup->addAction(a);
        if (select == f->prefix)
            a->setChecked(true);
    });
    fs->addSeparator();
    fs->addAction(myToolFacesetUseFallback);
    myToolFacesetUseFallback->setChecked(use);
}

void CREMainWindow::onSaveFormulae()
{
}

void CREMainWindow::onSaveQuests()
{
    myResourcesManager->saveQuests();
}

void CREMainWindow::onSaveMessages()
{
    myMessageManager->saveMessages();
}

void CREMainWindow::browsingMap(const QString& path)
{
    myMapBrowseStatus->setText(tr("Browsing map %1").arg(path));
}

void CREMainWindow::browsingFinished()
{
    statusBar()->showMessage(tr("Finished browsing maps."), 5000);
    myMapBrowseStatus->setVisible(false);
    myClearMapCache->setEnabled(true);
}

void CREMainWindow::onFiltersModified()
{
    emit updateFilters();
}

/**
 * @todo
 * - list animations and faces for artifacts using the 'animation_suffix' and allowed types
 * - list use for skill-related actions
 * - list things with classes and such
 */
void CREMainWindow::onReportDuplicate()
{
    QHash<QString, QStringList> faces, anims;

    // browse all archetypes
    getManager()->archetypes()->each([&faces, &anims] (const auto arch)
    {
        if (arch->head)
        {
            return;
        }
        // if there is an animation, don't consider the face, since it's part of the animation anyway (hopefully, see lower for report on that)
        if (arch->clone.animation == NULL)
        {
            if (arch->clone.face) {
                faces[QString::fromLatin1(arch->clone.face->name)].append(QString(arch->name) + " (arch)");
                sstring key = object_get_value(&arch->clone, "identified_face");
                if (key)
                {
                    faces[QString(key)].append(QString(arch->name) + " (arch)");
                }
            }
        }
        else
        {
            anims[arch->clone.animation->name].append(QString(arch->name) + " (arch)");
            sstring key = object_get_value(&arch->clone, "identified_animation");
            if (key)
            {
                anims[QString(key)].append(QString(arch->name) + " (arch)");
            }
        }
    });

    // list faces in animations
    getManager()->animations()->each([&faces] (const auto anim)
    {
        QStringList done;
        for (int i = 0; i < anim->num_animations; i++)
        {
            // don't list animation twice if they use the same face
            if (!done.contains(QString::fromLatin1(anim->faces[i]->name)))
            {
                faces[QString::fromLatin1(anim->faces[i]->name)].append(QString(anim->name) + " (animation)");
                done.append(QString::fromLatin1(anim->faces[i]->name));
            }
        }
    });

    // list faces and animations for artifacts
    artifactlist* list;
    for (list = first_artifactlist; list != NULL; list = list->next)
    {
        for (auto art : list->items)
        {
          if (art->item->animation == 0)
          {
              if (art->item->face) {
                faces[QString::fromLatin1(art->item->face->name)].append(QString(art->item->name) + " (art)");
                sstring key = object_get_value(art->item, "identified_face");
                if (key)
                {
                    faces[QString(key)].append(QString(art->item->name) + " (art)");
                }
              }
          }
          else
          {
              anims[art->item->animation->name].append(QString(art->item->name) + " (art)");
              sstring key = object_get_value(art->item, "identified_animation");
              if (key)
              {
                  anims[QString(key)].append(QString(art->item->name) + " (arch)");
              }
          }
        }
    }

    getManager()->quests()->each([&] (auto quest) {
        if (quest->face != nullptr)
        {
            faces[quest->face->name].append(QString(quest->quest_code) + " (quest)");
        }
    });

    getManager()->messages()->each([&faces] (const GeneralMessage *message)
    {
        if (message->face != nullptr)
        {
            faces[message->face->name].append(QString(message->identifier) + " (message)");
        }
    });

    for (const auto map : myMapManager->allMaps())
    {
        for (const auto face : map->faces())
        {
            faces[face].append(QString(map->path()) + " (map)");
        }
        for (const auto animation : map->animations())
        {
            anims[animation].append(map->path() + " (map)");
        }
    }

    QString report("<p><strong>Warning:</strong> this list doesn't take into account faces for all artifacts, especially the 'animation_suffix' ones.</p><h1>Faces used multiple times:</h1><ul>");

    QStringList keys = faces.keys();
    keys.sort();
    foreach(QString name, keys)
    {
        if (faces[name].size() <= 1 || name.compare("blank.111") == 0)
            continue;

        faces[name].sort();
        report += "<li>" + name + ": ";
        report += faces[name].join(", ");
        report += "</li>";
    }

    report += "</ul>";

    report += "<h1>Unused faces:</h1><ul>";
    getManager()->faces()->each([&faces, &report] (const auto face)
    {
        if (faces[face->name].size() > 0)
            return;
        report += QString("<li>") + face->name + "</li>";
    });
    report += "</ul>";

    report += "<h1>Animations used multiple times:</h1><ul>";
    keys = anims.keys();
    keys.sort();
    foreach(QString name, keys)
    {
        if (anims[name].size() <= 1)
            continue;

        anims[name].sort();
        report += "<li>" + name + ": ";
        report += anims[name].join(", ");
        report += "</li>";
    }
    report += "</ul>";

    report += "<h1>Unused animations:</h1><ul>";
    getManager()->animations()->each([&anims, &report] (const auto anim)
    {
        if (anims[anim->name].size() > 0 || !strcmp(anim->name, "###none"))
            return;
        report += QString("<li>") + anim->name + "</li>";
    });
    report += "</ul>";

    // Find faces used for an object having an animation not including this face
    report += "<h1>Objects having a face not part of their animation:</h1><ul>";

    getManager()->archetypes()->each([&report] (archetype *arch) {
        // if there is an animation, don't consider the face, since it's part of the animation anyway (hopefully)
        if (arch->clone.animation == NULL || arch->clone.face == NULL) {
            return;
        }
        bool included = false;
        for (int f = 0; f < arch->clone.animation->num_animations && !included; f++) {
            if (arch->clone.animation->faces[f] == arch->clone.face) {
                included = true;
            }
        }
        if (!included) {
            report += QString("<li>%1 (%2) has face %3 not in animation %4</li>\n").arg(arch->name, arch->clone.name, arch->clone.face->name, arch->clone.animation->name);
        }
    });

    CREReportDisplay show(report, "Faces and animations report");
    show.exec();
}

void CREMainWindow::onReportSpellDamage()
{
    QStringList spell;
    QList<QStringList> damage;

    object* caster = create_archetype("orc");

    getManager()->archetypes()->each([&caster, &spell, &damage] (archetype *arch)
    {
        if (arch->clone.type == SPELL && arch->clone.subtype == SP_BULLET)
        {
            spell.append(arch->clone.name);
            QStringList dam;
            for (int l = 0; l < settings.max_level; l++)
            {
                caster->level = l;
                int dm = arch->clone.stats.dam + SP_level_dam_adjust(caster, &arch->clone);
                int cost = SP_level_spellpoint_cost(caster, &arch->clone, SPELL_HIGHEST);
                dam.append(tr("%1 [%2]").arg(dm).arg(cost));
            }
            damage.append(dam);
        }
    });

    object_free_drop_inventory(caster);

    QString report("<table><thead><tr><th>level</th>");

    for (int i = 0; i < spell.size(); i++)
    {
        report += "<th>" + spell[i] + "</th>";
    }

    report += "</tr></thead><tbody>";

    for (int l = 0; l < settings.max_level; l++)
    {
        report += "<tr><td>" + QString::number(l) + "</td>";
        for (int s = 0; s < spell.size(); s++)
            report += "<td>" + damage[s][l] + "</td>";
        report += "</tr>";
    }

    report += "</tbody></table>";

    CREReportDisplay show(report, "Spell damage");
    show.exec();
}

static QString alchemyTable(const QString& skill, QStringList& noChance, std::vector<std::pair<QString, int>> &allIngredients)
{
    int count = 0;

    QHash<int, QStringList> recipes;

    const recipelist* list;
    const recipe* recipe;

    for (int ing = 1; ; ing++)
    {
        list = get_formulalist(ing);
        if (!list)
            break;

        for (recipe = list->items; recipe; recipe = recipe->next)
        {
            if (skill == recipe->skill)
            {
                if (recipe->arch_names == 0)
                    // hu?
                    continue;

                archetype* arch = find_archetype(recipe->arch_name[0]);
                if (arch == NULL) {
                    continue;
                }

                QString name;
                if (strcmp(recipe->title, "NONE") == 0)
                {
                    if (arch->clone.title == NULL)
                        name = arch->clone.name;
                    else
                        name = QString("%1 %2").arg(arch->clone.name, arch->clone.title);
                }
                else
                {
                    name = QString("%1 of %2").arg(arch->clone.name, recipe->title);
                }

                QStringList ingredients;
                for (const linked_char* ingred = recipe->ingred; ingred != NULL; ingred = ingred->next)
                {
                    ingredients.append(ingred->name);
                    const char* name = ingred->name;
                    if (isdigit(ingred->name[0])) {
                        name = strchr(ingred->name, ' ') + 1;
                    }
                    auto ing = std::find_if(allIngredients.begin(), allIngredients.end(), [&] (auto i) { return i.first == name; } );
                    if (ing != allIngredients.end()) {
                        (*ing).second++;
                    } else {
                        allIngredients.push_back(std::make_pair(name, 1));;
                    }
                }

                recipes[recipe->diff].append(QString("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>").arg(name).arg(recipe->diff).arg(recipe->ingred_count).arg(recipe->exp).arg(ingredients.join(", ")));
                count++;

                if (recipe->chance == 0) {
                    noChance.append(name);
                }
            }
        }
    }

    if (count == 0)
        return QString();

    QString report = QString("<h2>%1 (%2 recipes)</h2><table><thead><tr><th>product</th><th>difficulty</th><th>ingredients count</th><th>experience</th><th>Ingredients</th>").arg(skill).arg(count);
    report += "</tr></thead><tbody>";

    QList<int> difficulties = recipes.keys();
    qSort(difficulties);
    foreach(int difficulty, difficulties)
    {
        QStringList line = recipes[difficulty];
        qSort(line);
        report += line.join("\n");
    }

    report += "</tbody></table>";

    return report;
}

static void doIngredients(const std::vector<std::pair<QString, int>> &allIngredients, const QString &criteria, QString &report) {
    report += QString("<h1>All items used as ingredients (%1)</h1>").arg(criteria);
    report += "<ul>";
    for (auto ing : allIngredients) {
        report += QString("<li>%1 (%2 recipes)</li>").arg(ing.first).arg(ing.second);
    }
    report += "</ul>";
}

void CREMainWindow::onReportAlchemy()
{
    QStringList skills;

    getManager()->archetypes()->each([&skills] (const auto arch)
    {
        if (arch->clone.type == SKILL)
            skills.append(arch->clone.name);
    });
    skills.sort();

    QString report("<h1>Alchemy formulae</h1>");
    QStringList noChance;
    std::vector<std::pair<QString, int>> allIngredients;

    foreach(const QString skill, skills)
    {
        report += alchemyTable(skill, noChance, allIngredients);
    }

    qSort(noChance);
    report += tr("<h1>Formulae with chance of 0</h1>");
    report += "<table><th>";
    foreach(const QString& name, noChance) {
        report += "<tr><td>" + name + "</td></tr>";
    }
    report += "</th></table>";

    std::sort(allIngredients.begin(), allIngredients.end(), [] (auto i1, auto i2) {
        return i1.first.toLower() < i2.first.toLower();
    });
    doIngredients(allIngredients, "alphabetical order", report);

    std::sort(allIngredients.begin(), allIngredients.end(), [] (auto i1, auto i2) {
        if (i1.second == i2.second) {
            return i1.first.toLower() < i2.first.toLower();
        }
        return i1.second > i2.second;
    });
    doIngredients(allIngredients, "count of uses", report);

    CREReportDisplay show(report, "Alchemy formulae");
    show.exec();
}

void CREMainWindow::onReportAlchemyGraph()
{
    QString output = QFileDialog::getSaveFileName(this, tr("Destination file"), "", tr("Dot files (*.dot);;All files (*.*)"));
    if (output.isEmpty()) {
        return;
    }

    QFile file(output);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Write error", tr("Unable to write to %1").arg(output));
        return;
    }
    QTextStream out(&file);

    out << "digraph alchemy {\n";

    QVector<const recipe*> recipes;
    QHash<const recipe*, QString> names;
    QHash<QString, QVector<const recipe*> > products;

    for (int ing = 1; ; ing++)
    {
        const recipelist* list = get_formulalist(ing);
        if (!list)
            break;

        for (const recipe* recipe = list->items; recipe; recipe = recipe->next)
        {
            QString product("???");
            for (size_t idx = 0; idx < recipe->arch_names; idx++) {
                auto arch = getManager()->archetypes()->find(recipe->arch_name[idx]);
                if (!arch) {
                    continue;
                }
                if (recipe->title && strcmp(recipe->title, "NONE")) {
                    product = tr("%1 of %2").arg(arch->clone.name, recipe->title);
                } else {
                    product = arch->clone.name;
                }
                products[product].append(recipe);
            }
            names[recipe] = product;
            recipes.append(recipe);
        }
    }

    QHash<const recipe*, bool> added;

    foreach (const recipe* rec, recipes) {
        for (linked_char* ing = rec->ingred; ing; ing = ing->next) {
            const char* name = ing->name;
            if (isdigit(name[0]) && strchr(name, ' ')) {
                name = strchr(name, ' ') + 1;
            }
            QHash<QString, QVector<const recipe*> >::iterator item = products.find(name);
            if (item != products.end()) {
                if (!added[rec]) {
                    out << tr("alchemy_%1 [label=\"%2\"]\n").arg(recipes.indexOf(rec)).arg(names[rec]);
                    added[rec] = true;
                }
                for (auto r = item->begin(); r != item->end(); r++) {
                    if (!added[*r]) {
                        out << tr("alchemy_%1 [label=\"%2\"]\n").arg(recipes.indexOf(*r)).arg(names[*r]);
                        added[*r] = true;
                    }
                    out << tr("alchemy_%1 -> alchemy_%2\n").arg(recipes.indexOf(*r)).arg(recipes.indexOf(rec));
                }
            }
        }
    }

    int ignored = 0;
    foreach (const recipe* rec, recipes) {
        if (!added[rec]) {
            ignored++;
        }
    }
    out << "graph [labelloc=\"b\" labeljust=\"c\" label=\"Alchemy graph, formulae producing ingredients of other formulae";
    if (ignored) {
        out << tr(" (%1 formulae not displayed)").arg(ignored);
    }
    out << "\"]\n";

    out << "}\n";
}

static QString spellsTable(const QString& skill)
{
    bool one = false;

    QString report = QString("<h2>%1</h2><table><thead><tr><th>Spell</th><th>Level</th><th>Path</th>").arg(skill);
    report += "</tr></thead><tbody>";

    QHash<int, QStringList> spells;

    getManager()->archetypes()->each([&skill, &spells, &one] (const archetype *spell) {
        if (spell->clone.type == SPELL && spell->clone.skill == skill)
        {
            StringBuffer sb;
            describe_spellpath_attenuation(nullptr, spell->clone.path_attuned, &sb);
            spells[spell->clone.level].append(QString("<tr><td>%1</td><td>%2</td><td>%3</td></tr>").arg(spell->clone.name).arg(spell->clone.level).arg(QString::fromStdString(sb)));
            one = true;
        }
    });

    if (!one)
        return QString();

    QList<int> levels = spells.keys();
    qSort(levels);
    foreach(int level, levels)
    {
        spells[level].sort();
        report += spells[level].join("\n");
    }

    report += "</tbody></table>";

    return report;
}

void CREMainWindow::onReportSpells()
{
    QStringList skills;

    getManager()->archetypes()->each([&skills] (const archetype *arch)
    {
        if (arch->clone.type == SKILL)
            skills.append(arch->clone.name);
    });

    skills.sort();

    QString report("<h1>Spell list</h1>");

    foreach(const QString skill, skills)
    {
        report += spellsTable(skill);
    }

    CREReportDisplay show(report, "Spell list");
    show.exec();
}

/**
 * Simulates a fight between a player and a monster.
 * Player is a dwarf, with low statistics, no equipment.
 * A maximum of 50 rounds are fighted (can be changed by modifying 'limit').
 * @param monster evil guy being fighted.
 * @param skill what the player attacks the monster with.
 * @param level what skill level to use.
 * @return 1 if the player could kill the monster, 0 else.
 */
static int monsterFight(archetype* monster, archetype* skill, int level)
{
    int limit = 50, result = 1;
    player pl;
    socket_struct sock;
    memset(&pl, 0, sizeof(player));
    memset(&sock, 0, sizeof(sock));
    strncpy(pl.savebed_map, "/HallOfSelection", MAX_BUF);
    pl.bed_x = 5;
    pl.bed_y = 5;
    pl.socket = &sock;
    std::unique_ptr<uint8_t[]> faces(new uint8_t[get_faces_count()]);
    sock.faces_sent = faces.get();

    archetype *dwarf_player_arch = find_archetype("dwarf_player");
    if (dwarf_player_arch == NULL) {
        return 0;
    }
    object* obfirst = object_create_arch(dwarf_player_arch);
    obfirst->level = level;
    obfirst->contr = &pl;
    pl.ob = obfirst;
    object* obskill = object_create_arch(skill);
    obskill->level = level;
    SET_FLAG(obskill, FLAG_APPLIED);
    object_insert_in_ob(obskill, obfirst);
    archetype *skill_arch = find_archetype((skill->clone.subtype == SK_TWO_HANDED_WEAPON) ? "sword_3" : "sword");
    if (skill_arch == NULL) {
        return 0;
    }
    object* sword = object_create_arch(skill_arch);
    SET_FLAG(sword, FLAG_APPLIED);
    object_insert_in_ob(sword, obfirst);
    fix_object(obfirst);
    obfirst->stats.hp = obfirst->stats.maxhp;

    object* obsecond = object_create_arch(monster);
    tag_t tagfirst = obfirst->count;
    tag_t tagsecond = obsecond->count;

    // make a big map so large monsters are ok in map
    mapstruct* test_map = get_empty_map(50, 50);

    obfirst = object_insert_in_map_at(obfirst, test_map, NULL, 0, 0, 0);
    obsecond = object_insert_in_map_at(obsecond, test_map, NULL, 0, 1 + monster->tail_x, monster->tail_y);

    if (!obsecond || object_was_destroyed(obsecond, tagsecond))
    {
        qDebug() << "second removed??";
    }

    while (limit-- > 0 && obfirst->stats.hp >= 0 && obsecond->stats.hp >= 0)
    {
        if (obfirst->weapon_speed_left > 0) {
            --obfirst->weapon_speed_left;
            do_some_living(obfirst);

            move_player(obfirst, 3);
            if (object_was_destroyed(obsecond, tagsecond))
                break;

            /* the player may have been killed (acid for instance), so check here */
            if (object_was_destroyed(obfirst, tagfirst) || (obfirst->map != test_map))
            {
                result = 0;
                break;
            }
        }

        if (obsecond->speed_left > 0) {
            --obsecond->speed_left;
            monster_do_living(obsecond);

            attack_ob(obfirst, obsecond);
            /* when player is killed, she is teleported to bed of reality -> check map */
            if (object_was_destroyed(obfirst, tagfirst) || (obfirst->map != test_map))
            {
                result = 0;
                break;
            }
        }

        obfirst->weapon_speed_left += obfirst->weapon_speed;
        if (obfirst->weapon_speed_left > 1.0)
            obfirst->weapon_speed_left = 1.0;
        if (obsecond->speed_left <= 0)
            obsecond->speed_left += FABS(obsecond->speed);
    }

    if (!object_was_destroyed(obfirst, tagfirst))
    {
        object_remove(obfirst);
        object_free(obfirst, FREE_OBJ_FREE_INVENTORY);
    }
    if (!object_was_destroyed(obsecond, tagsecond))
    {
        object_remove(obsecond);
        object_free(obsecond, FREE_OBJ_FREE_INVENTORY);
    }
    delete_map(test_map);

    return result;
}

/**
 * Simulates a fight between a player and a monster.
 * Player is a dwarf, with low statistics, no equipment.
 * A maximum of 50 rounds are fighted per round.
 * @param monster evil guy being fighted.
 * @param skill what the player attacks the monster with.
 * @param level what skill level to use.
 * @param count how many fights to simulate.
 * @return how many fights, on count, the player could kill the monster.
 */
static int monsterFight(archetype* monster, archetype* skill, int level, int count)
{
    int victory = 0;
    while (count-- > 0)
        victory += monsterFight(monster, skill, level);

    return victory;
}

/**
 * Generate a report cell for player versus monster fight.
 * Cell will contain the first level the player could defeat the monster.
 * This level is determined via a kind of dichotomic search, trying levels and
 * using the middle ground for next iteration.
 * @param monster monster being fighted.
 * @param skill what the player uses to fight the monster.
 * @return full HTML table line for the statistics.
 */
static QString monsterFight(archetype* monster, archetype* skill)
{
    qDebug() << "monsterFight:" << monster->clone.name << skill->clone.name;
    int ret, min = settings.max_level + 1, half = settings.max_level + 1, count = 5, level;
    int first = 1, max = settings.max_level;

    while (first != max)
    {
        level = (max + first) / 2;
        if (level < first)
            level = first;
        if (first > max)
            first = max;

        ret = monsterFight(monster, skill, level, count);
        if (ret > 0)
        {
            if (level < min)
                min = level;
            if (ret > (count / 2) && (level < half))
                half = level;

            max = level;
        }
        else
        {
            if (first == level)
                break;
            first = level;
        }
    }

    //qDebug() << "   result:" << min << half;

    // if player was killed, then HallOfSelection was loaded, so clean  it now.
    // This speeds up various checks, like in free_all_objects().
    mapstruct* hos = has_been_loaded("/HallOfSelection");
    if (hos)
    {
        hos->reset_time = 1;
        hos->in_memory = MAP_IN_MEMORY;
        delete_map(hos);
    }
    /*
    extern int nroffreeobjects;
    extern int nrofallocobjects;
    qDebug() << "free: " << nroffreeobjects << ", all: " << nrofallocobjects;
     */

    if (min == settings.max_level + 1)
        return "<td colspan=\"2\">-</td>";
    return "<td>" + QString::number(min) + "</td><td>" + ((half != 0) ? QString::number(half) : "") + "</td>";
}

/**
 * Generate an HTML table line for a player versus monster fight statistics.
 * @param monster what is being attacked.
 * @param skills list of skills to fight with.
 * @return HTML line for the monster and skills.
 */
static QString monsterTable(archetype* monster, QList<archetype*> skills)
{
    QString line = "<tr>";

    line += "<td>" + QString(monster->clone.name) + "</td>";
    line += "<td>" + QString::number(monster->clone.level) + "</td>";
    line += "<td>" + QString::number(monster->clone.speed) + "</td>";
    line += "<td>" + QString::number(monster->clone.stats.wc) + "</td>";
    line += "<td>" + QString::number(monster->clone.stats.dam) + "</td>";
    line += "<td>" + QString::number(monster->clone.stats.ac) + "</td>";
    line += "<td>" + QString::number(monster->clone.stats.hp) + "</td>";
    line += "<td>" + QString::number(monster->clone.stats.Con) + "</td>";

    foreach(archetype* skill, skills)
    {
        line += monsterFight(monster, skill);
    }
    line += "</tr>\n";

    return line;
}

/**
 * Generate and display a table reporting for each monster and skill at what
 * level approximately the player could kill the monster.
 */
void CREMainWindow::onReportPlayer()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    QStringList names;
    QMap<QString, archetype*> monsters;
    QList<archetype*> skills;

    getManager()->archetypes()->each([&names, &monsters, &skills] (archetype *arch)
    {
        if (QUERY_FLAG(&arch->clone, FLAG_MONSTER) && arch->clone.stats.hp > 0 && arch->head == NULL)
        {
            QString name(QString(arch->clone.name).toLower());
            if (monsters.contains(name))
            {
                int suffix = 1;
                do
                {
                    name = QString(arch->clone.name).toLower() + "_" + QString::number(suffix);
                    suffix++;
                } while (monsters.contains(name));
            }

            monsters[name] = arch;
        }
        if (arch->clone.type == SKILL && IS_COMBAT_SKILL(arch->clone.subtype))
        {
            if (strcmp(arch->name, "skill_missile_weapon") == 0 || strcmp(arch->name, "skill_throwing") == 0)
                return;
            skills.append(arch);
        }
    });

    names = monsters.keys();
    names.sort();

    QString report(tr("<h1>Player vs monsters</h1><p><strong>fv</strong> is the level at which the first victory happened, <strong>hv</strong> is the level at which at least 50% of fights were victorious.</p>\n")), line;
    report += "<table border=\"1\"><tbody>\n";
    report += "<tr>";
    report += "<th rowspan=\"2\">Monster</th>";
    report += "<th rowspan=\"2\">level</th>";
    report += "<th rowspan=\"2\">speed</th>";
    report += "<th rowspan=\"2\">wc</th>";
    report += "<th rowspan=\"2\">dam</th>";
    report += "<th rowspan=\"2\">ac</th>";
    report += "<th rowspan=\"2\">hp</th>";
    report += "<th rowspan=\"2\">regen</th>";

    line = "<tr>";
    foreach(archetype* skill, skills)
    {
        report += "<th colspan=\"2\">" + QString(skill->clone.name) + "</th>";
        line += "<th>fv</th><th>hv</th>";
    }
    report += "</tr>\n" + line + "</tr>\n";

    int limit = 500;
    foreach(const QString name, names)
    {
        report += monsterTable(monsters[name], skills);
        if (limit-- <= 0)
            break;
    }

    report += "</tbody></table>\n";

    CREReportDisplay show(report, "Player vs monsters (hand to hand)");
    QApplication::restoreOverrideCursor();
    show.exec();
}

static QString reportSummon(const archetype* summon, const object* other, QString name)
{
    QString report;
    int level, wc_adj = 0;

    const object* spell = &summon->clone;
    sstring rate = object_get_value(spell, "wc_increase_rate");
    if (rate != NULL) {
        wc_adj = atoi(rate);
    }

    // hp, dam, speed, wc

    QString ac("<tr><td>ac</td>");
    QString hp("<tr><td>hp</td>");
    QString dam("<tr><td>dam</td>");
    QString speed("<tr><td>speed</td>");
    QString wc("<tr><td>wc</td>");
    int ihp, idam, iwc, diff;
    float fspeed;

    for (level = 1; level < 120; level += 10)
    {
        if (level < spell->level)
        {
            ac += "<td></td>";
            hp += "<td></td>";
            dam += "<td></td>";
            speed += "<td></td>";
            wc += "<td></td>";
            continue;
        }

        diff = level - spell->level;

        ihp = other->stats.hp + spell->duration + (spell->duration_modifier != 0 ? (diff / spell->duration_modifier) : 0);
        idam = (spell->stats.dam ? spell->stats.dam : other->stats.dam) + (spell->dam_modifier != 0 ? (diff / spell->dam_modifier) : 0);
        fspeed = MIN(1.0, FABS(other->speed) + .02 * (spell->range_modifier != 0 ? (diff / spell->range_modifier) : 0));
        iwc = other->stats.wc;
        if (wc_adj > 0)
            iwc -= (diff / wc_adj);

        ac += "<td>" + QString::number(other->stats.ac) + "</td>";
        hp += "<td>" + QString::number(ihp) + "</td>";
        dam += "<td>" + QString::number(idam) + "</td>";
        speed += "<td>" + QString::number(fspeed) + "</td>";
        wc += "<td>" + QString::number(iwc) + "</td>";
    }

    report += "<tr><td colspan=\"13\"><strong>" + name + "</strong></td></tr>\n";

    report += ac + "</tr>\n" + hp + "</tr>\n" + dam + "</tr>\n" + speed + "</tr>\n" + wc + "</tr>\n\n";

    return report;
}

void CREMainWindow::onReportSummon()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    int level;

    QString report(tr("<h1>Summoned pet statistics</h1>\n")), line;
    report += "<table border=\"1\">\n<thead>\n";
    report += "<tr>";
    report += "<th rowspan=\"2\">Spell</th>";
    report += "<th colspan=\"12\">Level</th>";
    report += "</tr>\n";
    report += "<tr>";

    for (level = 1; level < 120; level += 10)
    {
        report += "<th>" + QString::number(level) + "</th>";
    }
    report += "</tr>\n</thead>\n<tbody>\n";

    QMap<QString, QString> spells;

    getManager()->archetypes()->each([&spells] (archetype *summon)
    {
        if (summon->clone.type != SPELL || summon->clone.subtype != SP_SUMMON_GOLEM)
            return;
        if (summon->clone.other_arch != NULL)
        {
            spells[summon->clone.name] = reportSummon(summon, &summon->clone.other_arch->clone, QString(summon->clone.name));
            return;
        }

        // god-based summoning
        getManager()->archetypes()->each([&summon, &spells] (archetype *god)
        {
            if (god->clone.type != GOD)
                return;

            QString name(QString(summon->clone.name) + " (" + QString(god->name) + ")");
            archetype* holy = determine_holy_arch(&god->clone, summon->clone.race);
            if (holy == NULL)
                return;

            spells[name] = reportSummon(summon, &holy->clone, name);
        });
    });

    QStringList keys = spells.keys();
    keys.sort();
    foreach(QString key, keys)
    {
        report += spells[key];
    }

    report += "</tbody>\n</table>\n";

    CREReportDisplay show(report, "Summoned pet statistics");
    QApplication::restoreOverrideCursor();
    show.exec();
}

static QString buildShopReport(const QString& title, const QStringList& types, const QList<CREMapInformation*>& maps, QStringList& items)
{
  QString report("<h2>" + title + "</h2>");
  report += "<table border=\"1\">\n<thead>\n";
  report += "<tr>";
  report += "<th>Shop</th>";
  report += "<th>Greed</th>";
  report += "<th>Race</th>";
  report += "<th>Min</th>";
  report += "<th>Max</th>";
  foreach (QString item, types)
  {
    report += "<th>" + item + "</th>";
    items.removeAll(item);
  }
  report += "</tr>\n</thead><tbody>";

  foreach(const CREMapInformation* map, maps)
  {
    QString line;
    bool keep = false;

    if (map->shopItems().size() == 0)
        continue;

    line += "<tr>";

    line += "<td>" + map->name() + " " + map->path() + "</td>";
    line += "<td>" + QString::number(map->shopGreed()) + "</td>";
    line += "<td>" + map->shopRace() + "</td>";
    line += "<td>" + (map->shopMin() != 0 ? QString::number(map->shopMin()) : "") + "</td>";
    line += "<td>" + (map->shopMax() != 0 ? QString::number(map->shopMax()) : "") + "</td>";

    foreach(const QString item, types)
    {
        if (map->shopItems()[item] == 0)
        {
          if (map->shopItems()["*"] == 0)
            line += "<td></td>";
          else
            line += "<td>" + QString::number(map->shopItems()["*"]) + "</td>";
          continue;
        }
        keep = true;
        line += "<td>" + QString::number(map->shopItems()[item]) + "</td>";
    }

    line += "</tr>";
    if (keep)
      report += line;
  }

  report += "</tbody></table>";
  return report;
}

void CREMainWindow::onReportShops()
{
  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    QString report(tr("<h1>Shop information</h1>\n"));

    QList<CREMapInformation*> maps = myMapManager->allMaps();
    QStringList items;
    foreach(const CREMapInformation* map, maps)
    {
        QStringList add = map->shopItems().keys();
        foreach(const QString item, add)
        {
            if (!items.contains(item))
                items.append(item);
        }
    }
    qSort(items);

    QStringList part;

    part << "weapon" << "weapon improver" << "bow" << "arrow";
    report += buildShopReport("Weapons", part, maps, items);

    part.clear();
    part << "armour" << "armour improver" << "boots" << "bracers" << "cloak" << "girdle" << "gloves" << "helmet" << "shield";
    report += buildShopReport("Armour", part, maps, items);

    part.clear();
    part << "amulet" << "potion" << "power_crystal" << "ring" << "rod" << "scroll" << "skillscroll" << "spellbook" << "wand";
    report += buildShopReport("Magical", part, maps, items);

    part.clear();
    part << "container" << "food" << "key" << "lamp" << "skill tool" << "special key";
    report += buildShopReport("Equipment", part, maps, items);

    if (!items.isEmpty())
    {

      part = items;
      report += buildShopReport("Others", part, maps, items);
    }

    CREReportDisplay show(report, "Shop information");
    QApplication::restoreOverrideCursor();
    show.exec();
}

void readDirectory(const QString& path, QHash<QString, QHash<QString, bool> >& states)
{
  QDir dir(path);
  QStringList subdirs = dir.entryList(QStringList("*"), QDir::Dirs | QDir::NoDotAndDotDot);
  foreach(QString subdir, subdirs)
  {
    readDirectory(path + QDir::separator() + subdir, states);
  }

  QStringList quests = dir.entryList(QStringList("*.quest"), QDir::Files);
  foreach(QString file, quests)
  {
    qDebug() << "read quest:" << path << file;
    QString name = file.left(file.length() - 6);
    QFile read(path + QDir::separator() + file);
    read.open(QFile::ReadOnly);
    QTextStream stream(&read);
    QString line, code;
    bool completed;
    while (!(line = stream.readLine(0)).isNull())
    {
      if (line.startsWith("quest "))
      {
        code = line.mid(6);
        completed = false;
        continue;
      }
      if (code.isEmpty())
        continue;
      if (line == "end_quest")
      {
        states[code][name] = completed;
        code.clear();
        continue;
      }
      if (line.startsWith("state "))
        continue;
      if (line == "completed 1")
        completed = true;
    }
  }
}

void CREMainWindow::onReportQuests()
{
  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

  QHash<QString, QHash<QString, bool> > states;
  QString directory(settings.localdir);
  directory += QDir::separator();
  directory += settings.playerdir;
  readDirectory(directory, states);

  QStringList codes;
  getManager()->quests()->each([&] (const auto quest) {
    codes.append(quest->quest_code);
  });

  QString report("<html><body>\n<h1>Quests</h1>\n");

  QStringList keys = states.keys();
  keys.sort();

  foreach(QString key, keys)
  {
    codes.removeAll(key);
    const auto quest = getManager()->quests()->get(key.toStdString());
    report += "<h2>Quest: " + (quest != NULL ? quest->quest_title : (key + " ???")) + "</h2>\n";
    report += "<p>";
    QHash<QString, bool> done = states[key];
    QStringList players = done.keys();
    players.sort();
    int completed = 0;
    QString sep;
    foreach(QString player, players)
    {
      report += sep;
      sep = ", ";
      if (done[player])
      {
        completed++;
        report += "<strong>" + player + "</strong>";
      }
      else
      {
        report += player;
      }
    }
    report += "</p>\n";
    report += "<p>" + tr("%1 completed out of %2 (%3%)").arg(completed).arg(players.size()).arg(completed * 100 / players.size()) + "</p>\n";
  }

  if (codes.length() > 0)
  {
    codes.sort();
    QString sep;
    report += "<h2>Quests never done</h2>\n<p>\n";
    foreach(QString code, codes)
    {
      report += sep;
      sep = ", ";
      const auto quest = getManager()->quests()->find(code.toStdString());
      report += (quest != NULL ? quest->quest_title : (code + " ???"));
    }
    report += "</p>\n";
  }

  report += "</body>\n</html>\n";

  CREReportDisplay show(report, "Quests report");
  QApplication::restoreOverrideCursor();
  show.exec();
}

void CREMainWindow::onReportMaterials()
{
  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

  QString report;
  report += "<html>";

  report += "<h1>Materials</h1>";
  report += "<table><tr><th>Name</th><th>Description</th></tr>";
  for (const auto &mat : materials) {
      report += tr("<tr><td>%1</td><td>%2</td></tr>").arg(mat->name, mat->description);
  }
  report += "</table>";

  for (int s = 0; s < 2; s++) {
      QString name(s == 0 ? "Saves" : "Resistances");
        report += tr("<h1>%1</h1>").arg(name);
        report += tr("<tr><th rowspan='2'>Name</th><th colspan='%1'>%2</th></tr>").arg(NROFATTACKS).arg(name);
        report += "<tr>";
        for (int r = 0; r < NROFATTACKS; r++) {
            report += "<th>" + QString(attacktype_desc[r]) + "</th>";
        }
        report += "</tr>";

        for (auto const &mat : materials) {
            report += tr("<tr><td>%1</td>").arg(mat->name);
              for (int r = 0; r < NROFATTACKS; r++) {
                  int8_t val = (s == 0 ? mat->save[r] : mat->mod[r]);
                  report += tr("<td>%1</td>").arg(val == 0 ? QString() : QString::number(val));
              }
            report += "</tr>";
        }
      report += "</table>";
  }

  report += "</html>";

  CREReportDisplay show(report, "Materials report");
  QApplication::restoreOverrideCursor();
  show.exec();
}

void CREMainWindow::onReportArchetypes()
{
  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

  QString report;
  report += "<html>";

  report += "<h1>Apparently unused archetypes</h1>";
  report += "<h3>Warning: this list contains skills, items on style maps, and other things which are actually used.</h3>";
  report += "<table>";
  report += "<tr><th>Image</th><th>Archetype name</th><th>Item name</th><th>Type</th></tr>";

  getManager()->archetypes()->each([this, &report] (const archetype* arch)
  {
      if (arch->head || arch->clone.type == PLAYER || arch->clone.type == MAP || arch->clone.type == EVENT_CONNECTOR)
          return;
      if (strstr(arch->name, "hpbar") != nullptr)
          return;

      bool used = false;
      ResourcesManager::archetypeUse(arch, myMapManager, [&used]
        (ArchetypeUse, const archetype*, const treasurelist*, const CREMapInformation*, const recipe*) -> bool
      {
          used = true;
          return false;
      });

      if (!used)
      {
        QImage image(CREPixmap::getIcon(arch->clone.face->number).pixmap(32,32).toImage());
        QByteArray byteArray;
        QBuffer buffer(&byteArray);
        image.save(&buffer, "PNG");
        QString iconBase64 = QString::fromLatin1(byteArray.toBase64().data());
        auto td = get_typedata(arch->clone.type);
        report += tr("<tr><td><img src='data:image/png;base64,%1'></td><td>%2</td><td>%3</td><td>%4</td></tr>")
                .arg(iconBase64, arch->name, arch->clone.name, td ? td->name : tr("unknown: %1").arg(arch->clone.type));
      }
  });

  report += "</table>";
  report += "</html>";

  CREReportDisplay show(report, "Unused archetypes report");
  QApplication::restoreOverrideCursor();
  show.exec();
}

void CREMainWindow::onReportLicenses()
{
  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

  QString report;
  report += "<html>";

  auto all = myResourcesManager->licenseManager()->getAll();
  std::set<std::string> faces, facesets, fields;

  for (auto item : all)
  {
    faces.insert(item.first);
    for (auto fs : item.second)
    {
      facesets.insert(fs.first);
      for (auto items : fs.second)
      {
          fields.insert(items.first);
      }
    }
  }

  getManager()->facesets()->each([&facesets] (const face_sets *fs)
  {
    facesets.erase(fs->prefix);
  });
  getManager()->faces()->each([&faces] (const Face *face)
  {
    faces.erase(LicenseManager::licenseNameFromFaceName(face->name, false));
    faces.erase(LicenseManager::licenseNameFromFaceName(face->name, true));
  });

  if (facesets.empty())
  {
    report += "<h1>No invalid faceset</h1>\n";
  }
  else
  {
    report += "<h1>Invalid faceset found</h1>\n";
    report += "<p>The faceset of the license file doesn't match any defined facesets.</p>";
    report += "<ul>\n";
    for (auto fs : facesets)
    {
      report += "<li>" + QString(fs.c_str()) + "</li>\n";
    }
    report += "</ul>\n";
  }

  if (faces.empty())
  {
    report += "<h1>No invalid face name</h1>\n";
  }
  else
  {
    report += "<h1>Invalid face names found</h1>\n";
    report += "<p>The face name from the license file doesn't match any defined face.</p>";
    report += "<ul>\n";
    for (auto f : faces)
    {
      report += "<li>" + QString(f.c_str()) + "</li>\n";
    }
    report += "</ul>\n";
  }

  report += "<h1>All fields used in license descriptions</h1>\n";
  report += "<ul>\n";
  for (auto f : fields)
  {
    report += "<li>" + QString(f.c_str()) + "</li>\n";
  }
  report += "</ul>\n";

  report += "</html>";
  CREReportDisplay show(report, "Licenses checks");
  QApplication::restoreOverrideCursor();
  show.exec();
}

void CREMainWindow::onReportResetGroups()
{
  QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

  QString report;
  report += "<html>";

  auto maps = myResourcesManager->getMapInformationManager()->allMaps();
  auto groupCmp = [] (const std::string &left, const std::string &right) { return left < right; };
  auto mapCmp = [] (const CREMapInformation *left, const CREMapInformation *right)
  {
      int name = left->name().compare(right->name());
      if (name != 0)
      {
          return name < 0;
      }
      return left->path().compare(right->path()) < 0;
  };
  std::map<std::string, std::vector<CREMapInformation *>, decltype(groupCmp)> groups(groupCmp);

  for (auto map : maps)
  {
    if (!map->resetGroup().isEmpty())
    {
      groups[map->resetGroup().toStdString()].push_back(map);
    }
  }

  if (groups.empty())
  {
    report += "<h1>No reset group defined</h1>\n";
  }
  else
  {
    for (auto group : groups)
    {
      report += "<h1>" + QString(group.first.c_str()) + " (" + QString::number(group.second.size()) + " maps)</h1>\n";
      report += "<ul>\n";
      std::sort(group.second.begin(), group.second.end(), mapCmp);
      for (auto map : group.second)
      {
          report += tr("<li>%1 (%2)</li>").arg(map->name(), map->path());
      }
      report += "</ul>\n";
    }
  }

  report += "</html>";
  CREReportDisplay show(report, "Map reset groups");
  QApplication::restoreOverrideCursor();
  show.exec();
}

void CREMainWindow::onToolEditMonsters()
{
    EditMonstersDialog edit(myResourcesManager);
    edit.exec();
}

void CREMainWindow::onToolSmooth()
{
    CRESmoothFaceMaker smooth;
    smooth.exec();
}

void CREMainWindow::onToolCombatSimulator()
{
    CRECombatSimulator simulator;
    simulator.exec();
}

void CREMainWindow::onToolBarMaker()
{
    CREHPBarMaker maker;
    maker.exec();
}

void CREMainWindow::onToolFaceMaker()
{
    FaceMakerDialog maker(this, myResourcesManager);
    maker.exec();
}

void CREMainWindow::onClearCache()
{
    QMessageBox confirm;
    confirm.setWindowTitle(tr("Crossfire Resource Editor"));
    confirm.setText(tr("Really clear map cache?"));
    confirm.setInformativeText(tr("This will force cache rebuild at next application start."));
    confirm.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    confirm.setDefaultButton(QMessageBox::No);
    confirm.setIcon(QMessageBox::Question);
    if (confirm.exec() == QMessageBox::Yes)
    {
        myMapManager->clearCache();
    }
}

void CREMainWindow::onToolFaceset(QAction* action)
{
    CREPixmap::setFaceset(action->data().toString());
}

void CREMainWindow::onToolFacesetUseFallback()
{
    CREPixmap::setUseFacesetFallback(myToolFacesetUseFallback->isChecked());
}

void CREMainWindow::onToolReloadAssets()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    myResourcesManager->licenseManager()->reset();
    assets_collect(settings.datadir, ASSETS_ALL);
    CREPixmap::clearFaceCache();
    QApplication::restoreOverrideCursor();
    QMessageBox::information(this, "Reload complete", "Assets reload complete, you may need to change the selected item to see updated versions.");
}

void CREMainWindow::onToolSounds()
{
    CRESettings settings;
    QString dir = QFileDialog::getExistingDirectory(this, tr("Please select the 'sounds' directory"), settings.soundsDirectory());
    if (dir.isEmpty())
        return;
    settings.setSoundsDirectory(dir);
    SoundsDialog dlg(dir, this);
    dlg.exec();
}

void CREMainWindow::onWindowsShowing() {
    auto windows = myArea->subWindowList();
    bool hasWindows = !windows.empty();

    while (myWindows->actions().size() > 6) {
        myWindows->removeAction(myWindows->actions()[6]);
    }
    for (auto a : myWindows->actions()) {
        if (a->isSeparator()) {
            a->setVisible(hasWindows);
        } else if (a != myWindows->actions()[0]) {
            a->setEnabled(hasWindows);
        }
    }

    for (int i = 0; i < windows.size(); ++i) {
        QMdiSubWindow *mdiSubWindow = windows.at(i);

        QString title(mdiSubWindow->widget()->windowTitle());
        if (i < 9) {
            title = tr("&%1 %2").arg(i + 1).arg(title);
        } else {
            title = tr("%1 %2").arg(i + 1).arg(title);
        }
        QAction *action = myWindows->addAction(title, mdiSubWindow, [this, mdiSubWindow] () {
            myArea->setActiveSubWindow(mdiSubWindow);
        });
        action->setCheckable(true);
        action ->setChecked(mdiSubWindow == myArea->activeSubWindow());
    }
}

void CREMainWindow::mapAdded(CREMapInformation *map) {
    auto reg = get_region_by_name(map->region().toLocal8Bit().data());
    map->setDisplayParent(myResourcesManager->wrap(reg, myAssets->regions()));
    for (auto rm : map->randomMaps()) {
        rm->setDisplayParent(myAssets->randomMaps());
    }
}
