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

#include "TreasurePanel.h"
#include <QMimeData>
#include "assets.h"
#include "AssetsManager.h"
#include "archetypes/ArchetypeComboBox.h"
#include "treasures/TreasureListComboBox.h"
#include "MimeUtils.h"
#include "HelpManager.h"

TreasurePanel::TreasurePanel(QWidget* parent) : AssetWrapperPanel(parent) {
    addSpinBox(tr("Chance:"), "chance", 0, 255, false);
    addSpinBox(tr("Magic:"), "magic", 0, 255, false);
    addSpinBox(tr("Count:"), "nrof", 0, 65535, false);
    addSpinBox(tr("Rolls count:"), "nrof_rolls", 0, 65535, false);
    myList = addTreasureList(tr("Treasure:"), "list", false);
    addSpinBox(tr("Magic to generate with:"), "list_magic_value", std::numeric_limits<uint8_t>::min(), std::numeric_limits<uint8_t>::max(), false);
    addSpinBox(tr("Magic adjustment:"), "list_magic_adjustment", std::numeric_limits<int8_t>::min(), std::numeric_limits<int8_t>::max(), false);
    myArch = addArchetype(tr("Archetype:"), "arch");
    addLineEdit(tr("Artifact:"), "artifact", false);
    addBottomFiller();
    setAcceptDrops(true);
    HelpManager::setHelpId(this, "treasures");
}

void TreasurePanel::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat(MimeUtils::Archetype) || event->mimeData()->hasFormat(MimeUtils::TreasureList)) {
        event->acceptProposedAction();
    }
}

void TreasurePanel::dragMoveEvent(QDragMoveEvent *event) {
    event->acceptProposedAction();
}

void TreasurePanel::dropEvent(QDropEvent *event) {
    auto archs = MimeUtils::extract(event->mimeData(), MimeUtils::Archetype, getManager()->archetypes());
    if (!archs.empty()) {
        auto arch = archs.front();
        myArch->setArch(HEAD(arch));
        event->acceptProposedAction();
    }
    auto lists = MimeUtils::extract(event->mimeData(), MimeUtils::TreasureList, getManager()->treasures());
    if (!lists.empty()) {
        myList->setList(lists.front());
        event->acceptProposedAction();
    }
}
