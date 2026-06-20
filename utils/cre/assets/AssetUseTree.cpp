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

#include "AssetUseTree.h"
#include "assets/AssetModel.h"

AssetUseTree::AssetUseTree(AssetModel *assets, QWidget *parent) : QTreeView(parent) {
    myFilter = new UseFilterAssetModel(this);
    myFilter->setSourceModel(assets);
    setModel(myFilter);
    setIconSize(QSize(32, 32));
    setRootIsDecorated(true);
}

AssetWrapper *AssetUseTree::filter() const
{
    return myFilter->filter();
}

void AssetUseTree::setFilter(AssetWrapper *filter) {
    myFilter->setFilter(filter);
}
