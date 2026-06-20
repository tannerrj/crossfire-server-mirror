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

#ifndef ASSET_MODEL_H
#define ASSET_MODEL_H

#include <QAbstractItemModel>
#include <QVector>
#include <QSortFilterProxyModel>

#include <map>
#include <tuple>
#include <utility>

#include "assets/AssetWrapper.h"

/**
 * Qt model representing all items in CRE, with the exception of experience.
 */
class AssetModel : public QAbstractItemModel {
    Q_OBJECT
public:
    AssetModel(AssetWrapper *assets, QObject *parent);
    virtual ~AssetModel();

    virtual int columnCount(const QModelIndex& parent) const override;
    virtual QModelIndex index(int row, int column, const QModelIndex& parent) const override;
    virtual QModelIndex parent(const QModelIndex& index) const override;
    virtual int rowCount(const QModelIndex & parent) const override;
    virtual QVariant data(const QModelIndex& index, int role) const override;
    virtual QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    virtual Qt::ItemFlags flags(const QModelIndex& index) const override;
    //  virtual bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    //  virtual bool removeRows(int row, int count, const QModelIndex& parent = QModelIndex()) override;
    virtual QMimeData *mimeData(const QModelIndexList &indexes) const override;
    virtual bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) const override;
    virtual bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent) override;

protected slots:
    void assetModified(AssetWrapper *asset, AssetWrapper::ChangeType type, int extra);

private:
    AssetWrapper * myAssets;
};

/**
 * Proxy model filtering based on whether an asset uses a specific asset or not.
 */
class UseFilterAssetModel : public QSortFilterProxyModel {
public:
    UseFilterAssetModel(QObject *parent);

    void setFilter(AssetWrapper *asset);
    AssetWrapper *filter() const { return myAsset; }
    virtual QVariant data(const QModelIndex& index, int role) const override;

protected:
    virtual bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

    AssetWrapper *myAsset;
    mutable std::map<AssetWrapper *, bool> myCachedFilter;
    mutable std::map<AssetWrapper *, std::string> myCachedHints;
};

class QScriptEngine;

/**
 * Proxy model filtering based on user-written filter, used in the main resource view.
 */
class ScriptFilterAssetModel : public QSortFilterProxyModel {
public:
    ScriptFilterAssetModel(AssetModel *model, QScriptEngine *engine, QObject *parent);

    void setFilter(const QString &filter);
    const QString& filter() const { return myFilter; }

protected:
    virtual bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool acceptItem(AssetWrapper *item) const;

    QScriptEngine *myEngine;
    QString myFilter;
    mutable std::map<AssetWrapper *, bool> myCachedFilter;
};

#endif /* ASSET_MODEL_H */
