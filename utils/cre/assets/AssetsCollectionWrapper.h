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

#ifndef ASSETS_COLLECTION_WRAPPER_H
#define ASSETS_COLLECTION_WRAPPER_H

#include "AssetWrapper.h"
#include "../ResourcesManager.h"
#include "AssetsCollection.h"
#include <algorithm>

template<typename T>
class AssetsCollectionWrapper : public AssetWrapper {
public:
    AssetsCollectionWrapper(AssetWrapper *parent, const QString &name, AssetsCollection<T> *collection, ResourcesManager *resources, const QString &tip)
        : AssetWrapper(parent), myName(name) {
        collection->each([&] (T *asset) {
            myAssets.append(resources->wrap(asset, this));
        });
        std::sort(myAssets.begin(), myAssets.end(), compareByDisplayName);
        setProperty(tipProperty, tip);
    }

    virtual QString displayName() const { return myName; }
    virtual int childrenCount() const override { return myAssets.size(); }
    virtual AssetWrapper *child(int index) override { return myAssets[index]; }
    virtual int childIndex(AssetWrapper *child) override { return myAssets.indexOf(child); }

    virtual bool canDrop(const QMimeData *, int) const override { return true; }

protected:
    QString myName;
    QVector<AssetWrapper *> myAssets;
};

#endif /* ASSETS_COLLECTION_WRAPPER_H */
