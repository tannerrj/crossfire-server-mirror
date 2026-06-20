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

#ifndef TREASURE_WRAPPER_H
#define TREASURE_WRAPPER_H

#include <vector>

#include <QObject>
#include <QStringList>

#include "global.h"

#include "CREPixmap.h"
#include "artifacts/AssetWithArtifacts.h"
#include "artifacts/ArtifactWrapper.h"

class ResourcesManager;
class TreasureYesNo;

/**
 * Wrapper for a @ref treasure item.
 */
class TreasureWrapper : public AssetWithArtifacts<treasure> {
    Q_OBJECT

    Q_PROPERTY(quint8 chance READ chance WRITE setChance)
    Q_PROPERTY(quint8 magic READ magic WRITE setMagic)
    Q_PROPERTY(quint16 nrof READ nrof WRITE setNrof)
    Q_PROPERTY(quint16 nrof_rolls READ nrofRolls WRITE setNrofRolls)
    Q_PROPERTY(const treasurelist *list READ list WRITE setList)
    Q_PROPERTY(quint8 list_magic_value READ listMagicValue WRITE setListMagicValue)
    Q_PROPERTY(qint8 list_magic_adjustment READ listMagicAdjustment WRITE setListMagicAdjustment)
    Q_PROPERTY(const archetype *arch READ arch WRITE setArch)
    Q_PROPERTY(QString artifact READ artifact WRITE setArtifact)

public:
    TreasureWrapper(AssetWrapper *parent, treasure *tr, ResourcesManager *resources);
    virtual ~TreasureWrapper();

    virtual QString displayName() const override;
    virtual QIcon displayIcon() const override;

    virtual int childrenCount() const override;
    virtual AssetWrapper *child(int child) override;
    virtual int childIndex(AssetWrapper *child) override;
    virtual void removeChild(AssetWrapper *child) override;

    virtual PossibleUse uses(const AssetWrapper *asset, std::string &) const override;

    virtual bool canDrop(const QMimeData *data, int row) const override;
    virtual void drop(const QMimeData *data, int row) override;

    virtual void fillMenu(QMenu *menu) override;

    uint8_t chance() const;
    void setChance(uint8_t chance);
    uint8_t magic() const;
    void setMagic(uint8_t magic);
    uint16_t nrof() const;
    void setNrof(uint16_t nrof);
    uint16_t nrofRolls() const;
    void setNrofRolls(uint16_t nrofRolls);
    const treasurelist *list() const;
    void setList(const treasurelist *list);
    quint8 listMagicValue() const;
    void setListMagicValue(quint8 value);
    qint8 listMagicAdjustment() const;
    void setListMagicAdjustment(qint8 value);
    const archetype *arch() const;
    void setArch(const archetype *arch);
    const QString artifact() const;
    void setArtifact(const QString &art);

public slots:
    void swapYesNo();

protected:
    TreasureYesNo *myNextYes;
    TreasureYesNo *myNextNo;

    void doAddChild(TreasureYesNo **my, treasure **ti, bool isYes, int index, treasurelist *tl, archetype *arch);
    void addChild(treasurelist *tl, archetype *arch);
    void doRemoveChild(TreasureYesNo **tr, treasure **ti, int index);
};

class TreasureYesNo : public AssetWrapper {
    Q_OBJECT
public:
    TreasureYesNo(TreasureWrapper *parent, treasure *tr, ResourcesManager *resources, bool isYes);

    virtual QString displayName() const override { return myIsYes ? "Yes" : "No"; }
    virtual QIcon displayIcon() const override { return myIsYes ? CREPixmap::getTreasureYesIcon() : CREPixmap::getTreasureNoIcon(); }

    virtual int childrenCount() const override { return 1; }
    virtual AssetWrapper *child(int child) override { return child == 0 ? myWrapped : nullptr; }
    virtual int childIndex(AssetWrapper *child) override { return child == myWrapped ? 0 : -1; }
    virtual void removeChild(AssetWrapper *) override { myParent->removeChild(this); }

    virtual void fillMenu(QMenu *menu) override;
    void setIsYes(bool isYes) { myIsYes = isYes; }

protected:
    bool myIsYes;
    AssetWrapper *myWrapped;
};

#endif /* TREASURE_WRAPPER_H */
