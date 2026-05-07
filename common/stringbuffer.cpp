/*
 * Crossfire -- cooperative multi-player graphical RPG and adventure game
 *
 * Copyright (c) 1999-2014 Mark Wedel and the Crossfire Development Team
 * Copyright (c) 1992 Frank Tore Johansen
 *
 * Crossfire is free software and comes with ABSOLUTELY NO WARRANTY. You are
 * welcome to redistribute it under certain conditions. For details, please
 * see COPYING and LICENSE.
 *
 * The authors can be reached via e-mail at <crossfire@metalforge.org>.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "libproto.h"
#include "stringbuffer.h"

static void stringbuffer_ensure(StringBuffer *sb, size_t len);

StringBuffer *stringbuffer_new(void) {
    return new std::string;
}

void stringbuffer_delete(StringBuffer *sb) {
    delete sb;
}

char *stringbuffer_finish(StringBuffer *sb) {
    char* dat = strdup(sb->c_str());
    delete sb;
    return dat;
}

sstring stringbuffer_finish_shared(StringBuffer *sb) {
    sstring result = add_string(sb->c_str());
    delete sb;
    return result;
}

void stringbuffer_append_string(StringBuffer *sb, const char *str) {
    *sb += str;
}

void stringbuffer_append_char(StringBuffer *sb, const char c) {
    *sb += c;
}

void stringbuffer_append_int64(StringBuffer *sb, int64_t x) {
    if (x < 0) {
        stringbuffer_append_char(sb, '-');
        if (x == INT64_MIN) {
            x = INT64_MAX;
        } else {
            x = -x;
        }
    }

    const int MAX_DIGITS = 20; // handle at most 20 digits, since an int64_t has at most 19 digits
    char buf[MAX_DIGITS];
    int i;
    for (i = 0; i < MAX_DIGITS; i++) {
        buf[i] = x % 10 + '0';
        if ((x /= 10) == 0) {
            i++;
            break;
        }
    }

    // copy into sb in reverse
    for (int j = i - 1; j >= 0; j--) {
        stringbuffer_append_char(sb, buf[j]);
    }
}

void stringbuffer_append_printf(StringBuffer *sb, const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    char buf[HUGE_BUF];
    vsnprintf(buf, sizeof(buf), format, arg);
    va_end(arg);
    stringbuffer_append_string(sb, buf);
}

void stringbuffer_append_stringbuffer(StringBuffer *sb, const StringBuffer *sb2) {
    *sb += *sb2;
}

/**
 * Ensure sb can hold at least len more characters, growing the sb if not.
 */
static void stringbuffer_ensure(StringBuffer *sb, size_t len) {
    sb->reserve(len);
}

void stringbuffer_append_multiline_block(StringBuffer *sb, const char *start, const char *content, const char *end) {
    if (!content || *content == '\0') {
        return;
    }
    *sb += start;
    *sb += "\n";
    *sb += content;

    // Add newline if len(content) is non-zero and missing a trailing newline
    if ((*content != '\0') && (sb->back() != '\n')) {
        *sb += "\n";
    }

    if (end == NULL) {
        *sb += "end_";
        *sb += start;
    } else {
        *sb += "end";
    }
    *sb += "\n";
}

size_t stringbuffer_length(StringBuffer *sb) {
    return sb->size();
}

void stringbuffer_trim_whitespace(StringBuffer *sb) {
    while (isspace(sb->back())) {
        sb->pop_back();
    }
}
