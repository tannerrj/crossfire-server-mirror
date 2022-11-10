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

#include "CRECombatSimulator.h"
#include "CREPixmap.h"

#include <global.h>
#include <sproto.h>
#include "assets.h"
#include "AssetsManager.h"
#include "archetypes/ArchetypeComboBox.h"

CRECombatSimulator::CRECombatSimulator()
{
    QGridLayout* layout = new QGridLayout(this);
    int line = 0;

    layout->addWidget(new QLabel(tr("First fighter:"), this), line, 0);
    myFirst = new ArchetypeComboBox(this, false);
    layout->addWidget(myFirst, line++, 1);

    layout->addWidget(new QLabel(tr("Second fighter:"), this), line, 0);
    mySecond = new ArchetypeComboBox(this, false);
    layout->addWidget(mySecond, line++, 1);

    layout->addWidget(new QLabel(tr("Number of fights:"), this), line, 0);
    myCombats = new QSpinBox(this);
    myCombats->setMinimum(1);
    myCombats->setMaximum(10000);
    layout->addWidget(myCombats, line++, 1);

    layout->addWidget(new QLabel(tr("Maximum number of rounds:"), this), line, 0);
    myMaxRounds = new QSpinBox(this);
    myMaxRounds->setMinimum(1);
    myMaxRounds->setMaximum(10000);
    myMaxRounds->setValue(500);
    layout->addWidget(myMaxRounds, line++, 1);

    myResultLabel = new QLabel(tr("Combat result:"), this);
    myResultLabel->setVisible(false);
    layout->addWidget(myResultLabel, line++, 0, 1, 2);
    myResult = new QLabel(this);
    myResult->setVisible(false);
    layout->addWidget(myResult, line++, 0, 1, 2);

    QDialogButtonBox* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Close, Qt::Horizontal, this);
    layout->addWidget(box, line++, 0, 1, 2);
    connect(box, SIGNAL(rejected()), this, SLOT(reject()));
    connect(box, SIGNAL(accepted()), this, SLOT(fight()));

    setWindowTitle(tr("Combat simulator"));
}

CRECombatSimulator::~CRECombatSimulator()
{
}

void CRECombatSimulator::fight(const archetype* first, const archetype* second)
{
    int limit = myMaxRounds->value();
    object* obfirst = object_create_arch(const_cast<archetype *>(first));
    object* obsecond = object_create_arch(const_cast<archetype *>(second));

    // make a big map so large monsters are ok in map
    mapstruct* test_map = get_empty_map(50, 50);

    // insert shifted for monsters like titans who have parts with negative values
    obfirst = object_insert_in_map_at(obfirst, test_map, NULL, 0, 12, 12);
    obsecond = object_insert_in_map_at(obsecond, test_map, NULL, 0, 37, 37);

    OBJECT_REF_CREATE(obfirst);
    OBJECT_REF_CREATE(obsecond);

    while (limit-- > 0 && obfirst->stats.hp >= 0 && obsecond->stats.hp >= 0)
    {
        if (obfirst->speed_left > 0) {
            --obfirst->speed_left;
            monster_do_living(obfirst);
            if (obfirst->stats.hp > myFirstMaxHp)
                myFirstMaxHp = obfirst->stats.hp;

            attack_ob(obsecond, obfirst);
            if (!OBJECT_REF_VALID(obsecond))
                break;
            if (obsecond->stats.hp < mySecondMinHp && obsecond->stats.hp > 0)
                mySecondMinHp = obsecond->stats.hp;
        }

        if (obsecond->speed_left > 0) {
            --obsecond->speed_left;
            monster_do_living(obsecond);
            if (obsecond->stats.hp > mySecondMaxHp)
                mySecondMaxHp = obsecond->stats.hp;

            attack_ob(obfirst, obsecond);
            if (!OBJECT_REF_VALID(obfirst))
                break;
            if (obfirst->stats.hp < myFirstMinHp && obfirst->stats.hp > 0)
                myFirstMinHp = obfirst->stats.hp;
        }

        if (obfirst->speed_left <= 0)
            obfirst->speed_left += FABS(obfirst->speed);
        if (obsecond->speed_left <= 0)
            obsecond->speed_left += FABS(obsecond->speed);
    }

    if (limit > 0)
    {
        if (obfirst->stats.hp < 0)
            mySecondVictories++;
        else
            myFirstVictories++;
    }

    if (OBJECT_REF_VALID(obfirst))
    {
        object_remove(obfirst);
        object_free(obfirst, 0);
    }
    if (OBJECT_REF_VALID(obsecond))
    {
        object_remove(obsecond);
        object_free(obsecond, 0);
    }
    free_map(test_map);
}

void CRECombatSimulator::fight()
{
    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

    myFirstVictories = 0;
    mySecondVictories = 0;

    const archetype* first = myFirst->arch();
    const archetype* second = mySecond->arch();

    /*
     * Set min and max hp here, in case a monster doesn't ever get attacked
     * Example as of the time of writing is cherub vs. cherub
     * without this fix, the first cherub had maxhp of 10000 and minhp of -1
     *
     * So, temporaily create the objects to then determine what the maxhp is for
     * each object from the archetypes.
     */
    object* obfirst = object_create_arch(const_cast<archetype *>(first));
    object* obsecond = object_create_arch(const_cast<archetype *>(second));

    myFirstMinHp = obfirst->stats.maxhp;
    myFirstMaxHp = obfirst->stats.maxhp;
    mySecondMinHp = obsecond->stats.maxhp;
    mySecondMaxHp = obsecond->stats.maxhp;
    
    /*
     * Then, free the objects since this is done.
     */
    object_free(obfirst, 0);
    object_free(obsecond, 0);

    int count = myCombats->value();
    while (count-- > 0)
    {
        fight(first, second);
    }

    myResult->setText(tr("Draw: %1 fights\n%2 victories: %3 (max hp: %4, min hp: %5)\n%6 victories: %7 (max hp: %8, min hp: %9)")
        .arg(myCombats->value() - myFirstVictories - mySecondVictories)
        .arg(first->name).arg(myFirstVictories).arg(myFirstMaxHp).arg(myFirstMinHp)
        .arg(second->name).arg(mySecondVictories).arg(mySecondMaxHp).arg(mySecondMinHp));

    myResultLabel->setVisible(true);
    myResult->setVisible(true);

    QApplication::restoreOverrideCursor();
}
