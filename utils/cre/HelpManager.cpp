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

#include "HelpManager.h"
#include <QApplication>
#include <QtGui>
#include <QDialog>
#include <QHelpContentWidget>
#include <QHelpIndexWidget>
#include <QHelpLink>
#include <QWidget>
#include <QVBoxLayout>
#include <QSplitter>
#include "HelpBrowser.h"

const char *HelpManager::helpIdProperty = "cre_help_id";

HelpManager::HelpManager(const QString &helpRoot) : QHelpEngine(helpRoot + "/cre.qhc") {
    myDisplay = new QDialog();
    QVBoxLayout *layout = new QVBoxLayout(myDisplay);
    QSplitter *horizSplitter = new QSplitter(Qt::Horizontal, myDisplay);
    layout->addWidget(horizSplitter);

    QTabWidget* tWidget = new QTabWidget(horizSplitter);
    tWidget->addTab(contentWidget(), tr("Contents"));
    tWidget->addTab(indexWidget(), tr("Index"));

    myBrowser = new HelpBrowser(this, horizSplitter);
    connect(contentWidget(), SIGNAL(linkActivated(QUrl)), myBrowser, SLOT(setSource(QUrl)));
    connect(indexWidget(), SIGNAL(linkActivated(QUrl, QString)), myBrowser, SLOT(setSource(QUrl)));

    horizSplitter->insertWidget(0, tWidget);
    horizSplitter->insertWidget(1, myBrowser);

    myDisplay->setVisible(false);
}

HelpManager::~HelpManager() {
    delete myDisplay;
}

void HelpManager::displayHelp() {
    myDisplay->setVisible(true);
    myDisplay->setFocus();
    myDisplay->activateWindow();
    myDisplay->raise();
    myBrowser->setSource(computeUrlToDisplay());
}

void HelpManager::setHelpId(QWidget *widget, const QString &id) {
    widget->setProperty(helpIdProperty, id);
}


QUrl HelpManager::computeUrlForWidget(QWidget *widget) const {
    if (!widget) {
        return QUrl();
    }
    auto helpId = widget->property(helpIdProperty);
    if (!helpId.isNull()) {
        auto links = documentsForIdentifier(helpId.toString());
        if (!links.empty()) {
            return links.front().url;
        }
    }
    return computeUrlForWidget(widget->parentWidget());
}

QUrl HelpManager::computeUrlToDisplay() const {
    auto widget = QApplication::focusWidget();
    auto url = computeUrlForWidget(widget);
    if (url.isValid()) {
        return url;
    }
    return QUrl("qthelp://com.real-time.crossfire/cre/index.html");
}
