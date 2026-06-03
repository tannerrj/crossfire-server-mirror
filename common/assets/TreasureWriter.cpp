/*
 * Crossfire -- cooperative multi-player graphical RPG and adventure game
 *
 * Copyright (c) 2020 the Crossfire Development Team
 *
 * Crossfire is free software and comes with ABSOLUTELY NO WARRANTY. You are
 * welcome to redistribute it under certain conditions. For details, please
 * see COPYING and LICENSE.
 *
 * The authors can be reached via e-mail at <crossfire@metalforge.org>.
 */

#include "TreasureWriter.h"

#define W(x, n) { if (item->x) { stringbuffer_append_printf(buf, "%s" n "\n", indentItems.c_str(), item->x); } }

static void writeItem(const treasure *item, const std::string &indent, StringBuffer *buf) {
    std::string indentItems(indent);
    indentItems += "\t";
    if (item->item) {
        stringbuffer_append_printf(buf, "%sarch %s\n", indentItems.c_str(), item->item->name);
    }
    W(artifact, "artifact %s");
    W(name, "list %s");
    W(list_magic_value, "list_magic_value %d");
    W(list_magic_adjustment, "list_magic_adjustment %d");
    indentItems += "\t";
    W(change_arch.name, "change_name %s");
    W(change_arch.title, "change_title %s");
    W(change_arch.slaying, "change_slaying %s");
    if (item->chance != 100) {
        stringbuffer_append_printf(buf, "%schance %d\n", indentItems.c_str(), item->chance);
    }
    W(nrof, "nrof %d");
    W(nrof_rolls, "nrof_rolls %d");
    W(magic, "magic %d");
    if (item->next_yes) {
        stringbuffer_append_printf(buf, "%syes\n", indentItems.c_str());
        writeItem(item->next_yes, indentItems + "\t", buf);
    }
    if (item->next_no) {
        stringbuffer_append_printf(buf, "%sno\n", indentItems.c_str());
        writeItem(item->next_no, indentItems + "\t", buf);
    }
    if (item->next) {
        stringbuffer_append_string(buf, "more\n");
        writeItem(item->next, indent, buf);
    } else {
        stringbuffer_append_printf(buf, "%send\n", indent.c_str());
    }
}

void TreasureWriter::write(const treasurelist *list, StringBuffer *buf) {
    if (list->total_chance == 0) {
        stringbuffer_append_string(buf, "treasure ");
    } else {
        stringbuffer_append_string(buf, "treasureone ");
    }
    stringbuffer_append_string(buf, list->name);
    stringbuffer_append_string(buf, "\n");
    writeItem(list->items, std::string(), buf);
}
