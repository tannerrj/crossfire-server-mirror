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

#include <QString>
#include <qlist.h>
#include <qhash.h>
#include <QStringList>
#include <QMessageBox>
#include "ResourcesManager.h"
#include <locale.h>

#include "global.h"
#include "libproto.h"
#include "recipe.h"
#include "image.h"
#include "sproto.h"
#include "assets.h"
#include "logger.h"
#include "AssetsManager.h"
#include "CREMapInformationManager.h"
#include "random_maps/RandomMap.h"
#include "LicenseManager.h"
#include "ArchetypeWriter.h"
#include "QuestWriter.h"
#include "MessageWriter.h"
#include "TreasureWriter.h"
#include "ArtifactWriter.h"
#include "archetypes/ArchetypeWrapper.h"
#include "archetypes/ObjectWrapper.h"
#include "faces/FaceWrapper.h"
#include "animations/AnimationWrapper.h"
#include "assets/AssetOriginAndCreationDialog.h"
#include "sounds/SoundFiles.h"

ResourcesManager::ResourcesManager() : myMapInformationManager(nullptr), myArchetypes(new ArchetypeWriter()), myQuests(new QuestWriter()), myTreasures(new TreasureWriter()),
        myGeneralMessages(new MessageWriter()), myArtifacts(new ArtifactWriter()), myFaces(nullptr), myAnimations(nullptr)
{
}

ResourcesManager::~ResourcesManager()
{
}

static void onFatalInit(enum fatal_error) {
    QMessageBox::critical(nullptr, "Fatal error", "Error while initializing Crossfire data, make sure you have maps and archetypes correctly installed.");
}

void ResourcesManager::load()
{
    setlocale(LC_NUMERIC, "C");

    settings.assets_tracker = this;
    add_server_collect_hooks();
    assets_add_collector_hook(".LICENSE", [this] (BufferReader *reader, const char *filename) { myLicenseManager.readLicense(reader, filename); });
    assets_add_collector_hook("", [] (BufferReader *, const char *) { QCoreApplication::processEvents(); });
    settings.fatal_hook = onFatalInit;
    settings.ignore_assets_errors = 1;

    QStringList log;
    bool hasWarningOrError = false;
    auto log_callback = [&] (LogLevel logLevel, const char *format, va_list va) {
        if (logLevel > llevInfo) {
            return;
        }
        char buf[8192];
        vsnprintf(buf, sizeof(buf), format, va);
        QString l(tr("%1%2").arg(loglevel_names[logLevel], buf));
        log.append(l);
        if (logLevel == llevError) {
            hasWarningOrError = true;
        }
    };
    settings.log_callback = log_callback;

    init_globals();
    init_library();
    settings.fatal_hook = nullptr;
    init_gods();
    init_readable();
    settings.log_callback = nullptr;

    if (hasWarningOrError) {
        QString msg(tr("The following errors occurred during asset collection:\n") + log.join(""));
        QMessageBox::warning(nullptr, tr("Errors during asset collection!"), msg);
    }

    QString key;

    for (int ing = 1; ; ing++)
    {
        recipelist* list = get_formulalist(ing);
        if (!list)
            break;

        QHash<QString, recipe*> recipes;
        for (recipe* rec = list->items; rec; rec = rec->next)
        {
            key = QString("%1_%2").arg(rec->arch_name[0], rec->title);
            recipes[key] = rec;
        }
        myRecipes.append(recipes);
    }
}

int ResourcesManager::recipeMaxIngredients() const
{
    return myRecipes.size();
}

QStringList ResourcesManager::recipes(int count) const
{
    if (count < 1 || count > myRecipes.size())
        return QStringList();

    QStringList keys = myRecipes[count - 1].keys();
    keys.sort();
    return keys;
}

const recipe* ResourcesManager::getRecipe(int ingredients, const QString& name) const
{
    if (ingredients < 1 || ingredients > myRecipes.size())
        return NULL;

    return myRecipes[ingredients - 1][name];
}

void ResourcesManager::archetypeUse(const archetype* item, CREMapInformationManager* store, AssetUseCallback callback)
{
    bool goOn = true;
    getManager()->archetypes()->each([&item, &callback, &goOn] (archetype *arch) {
        if (!goOn)
            return;

        if (arch->clone.other_arch == item)
        {
            goOn = callback(OTHER_ARCH, arch, nullptr, nullptr, nullptr);
        }

        sstring death_anim = NULL;
        if (goOn && (death_anim = object_get_value(&arch->clone, "death_animation")) && strcmp(death_anim, item->name) == 0)
        {
            goOn = callback(DEATH_ANIM, arch, nullptr, nullptr, nullptr);
        }
    });

    getManager()->treasures()->each([&item, callback, &goOn] (treasurelist *list) {
        if (!goOn)
            return;
        for (auto t = list->items; t; t = t->next)
        {
            if (t->item == item)
            {
                goOn = callback(TREASURE_USE, nullptr, list, nullptr, nullptr);
            }
        }
    });

    QList<CREMapInformation*> mapuse = store->getArchetypeUse(item);
    foreach(CREMapInformation* information, mapuse)
    {
        if (!goOn)
            continue;
        goOn = callback(MAP_USE, nullptr, nullptr, information, nullptr);
    }
    auto allMaps = store->allMaps();
    foreach(CREMapInformation *information, allMaps)
    {
        if (!goOn)
            return;
        foreach(RandomMap* rm, information->randomMaps())
        {
            if (strcmp(item->name, rm->parameters()->final_exit_archetype) == 0)
            {
                goOn = callback(RANDOM_MAP_FINAL_EXIT, nullptr, nullptr, information, nullptr);
            }
            if (!goOn)
                return;
        }
    }

    int count = 1;
    recipelist* list;
    while ((list = get_formulalist(count++)))
    {
        if (!goOn)
            break;
        recipe* rec = list->items;
        while (goOn && rec)
        {
            for (size_t ing = 0; ing < rec->arch_names; ing++)
            {
                if (strcmp(rec->arch_name[ing], item->name) == 0)
                {
                    goOn = callback(ALCHEMY_PRODUCT, nullptr, nullptr, nullptr, rec);
                    break;
                }
            }
            rec = rec->next;
        }
    }
}

void ResourcesManager::archetypeModified(archetype *arch) {
    myArchetypes.assetModified(arch);
}

void ResourcesManager::saveArchetypes() {
    myArchetypes.saveModifiedAssets();
}

void ResourcesManager::questModified(quest_definition *quest) {
    myQuests.assetModified(quest);
}

void ResourcesManager::saveQuests() {
    myQuests.saveModifiedAssets();
}

void ResourcesManager::treasureModified(treasurelist *treasure) {
    myTreasures.assetModified(treasure);
}

void ResourcesManager::saveTreasures() {
    auto no = myTreasures.dirtyAssetsWithNoOrigin();
    while (!no.empty()) {
        auto list = no.back();
        auto origins = myTreasures.files();
        while (true) {
            AssetOriginAndCreationDialog dlg(AssetOriginAndCreationDialog::Treasure, AssetOriginAndCreationDialog::DefineOrigin, list->name,
                    origins, std::vector<std::string>());
            if (dlg.exec() == QDialog::Accepted) {
                myTreasures.assetDefined(list, dlg.file().toStdString());
                break;
            } else {
                if (QMessageBox::question(nullptr, tr("Lose changes to treasure list %1?").arg(list->name),
                        tr("Really discard changes to treasure list %1?").arg(list->name)) == QMessageBox::Yes) {
                    break;
                }
            }
        }
        no.pop_back();
    }
    myTreasures.saveModifiedAssets();
}

void ResourcesManager::generalMessageModified(GeneralMessage *asset) {
    myGeneralMessages.assetModified(asset);
}

void ResourcesManager::saveGeneralMessages() {
    myGeneralMessages.saveModifiedAssets();
}

void ResourcesManager::saveArtifacts() {
    myArtifacts.saveModifiedAssets();
}
