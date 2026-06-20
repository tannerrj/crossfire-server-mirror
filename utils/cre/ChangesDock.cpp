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

#include "ChangesDock.h"
#include <QTextEdit>
#include <QFile>
#include "CRESettings.h"
#include <QHelpEngineCore>
#include <QHelpLink>

ChangesDock::ChangesDock(QHelpEngineCore *help, QWidget *parent) : QDockWidget(tr("Changes"), parent) {
    setAllowedAreas(Qt::RightDockWidgetArea);
    setFeatures(DockWidgetClosable);
    setVisible(false);

    QTextEdit *changes = new QTextEdit(this);
    changes->setReadOnly(true);
    setWidget(changes);

    connect(help, &QHelpEngineCore::setupFinished, [this, help, changes] () { helpReady(help, changes); });
}

void ChangesDock::helpReady(QHelpEngineCore *help, QTextEdit *edit) {
    QString content("No content to display");
    auto links = help->documentsForIdentifier("changes");
    if (!links.empty()) {
        content = help->fileData(links.front().url);
    }
    edit->setText(content);

    CRESettings settings;
    if (settings.showChanges() && settings.changesLength() != content.length()) {
        setVisible(true);
        settings.setChangesLength(content.length());
    }

}
