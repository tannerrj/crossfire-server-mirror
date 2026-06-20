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

#ifndef ASSET_USE_TREE_H
#define ASSET_USE_TREE_H

#include <QTreeView>

class AssetWrapper;
class UseFilterAssetModel;
class AssetModel;

/**
 * Tree displaying assets filtered by whether they use a specific asset or not.
 */
class AssetUseTree : public QTreeView {
    Q_OBJECT

    Q_PROPERTY(AssetWrapper *filter READ filter WRITE setFilter)
public:
    AssetUseTree(AssetModel *assets, QWidget *parent);

protected:
    UseFilterAssetModel *myFilter;

    AssetWrapper *filter() const;
    void setFilter(AssetWrapper *filter);
};

#endif /* ASSET_USE_TREE_H */
