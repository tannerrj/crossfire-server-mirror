/*
 * Crossfire -- cooperative multi-player graphical RPG and adventure game
 *
 * Copyright (c) 2021 the Crossfire Development Team
 *
 * Crossfire is free software and comes with ABSOLUTELY NO WARRANTY. You are
 * welcome to redistribute it under certain conditions. For details, please
 * see COPYING and LICENSE.
 *
 * The authors can be reached via e-mail at <crossfire@metalforge.org>.
 */

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "global.h"
#include "compat.h"
#include "bufferreader.h"

struct BufferReader {
    char *buf;              /**< Data buffer. */

    size_t allocated_size;  /**< Allocated size of buf. */
    size_t buffer_length;   /**< Used size of buf. */

    char *current_line;     /**< Pointer to the next line of the buffer. */
    size_t line_index;      /**< Current line index. */
};

BufferReader *bufferreader_create() {
    BufferReader *buffer = static_cast<BufferReader *>(calloc(1, sizeof(*buffer)));
    if (!buffer) {
        fatal(OUT_OF_MEMORY);
    }

    return buffer;
}

void bufferreader_destroy(BufferReader *br) {
    free(br->buf);
    free(br);
}

/**
 * Ensure the buffer can hold the specified length, initialize all fields.
 * @param br buffer to initialize.
 * @param length lenfth to initialize for.
 */
static void bufferreader_init_for_length(BufferReader *br, size_t length) {
    size_t needed = length + 1;
    if (needed > br->allocated_size) {
        br->buf = static_cast<char *>(realloc(br->buf, needed));
        if (!br->buf) {
            fatal(OUT_OF_MEMORY);
        }
        br->allocated_size = needed;
    }
    br->buffer_length = length;
    assert(br->buf != NULL); // silence static analyzer. if br->allocated_size>0 then br->buf is allocated.
    br->buf[br->buffer_length] = '\0';
    br->current_line = br->buffer_length > 0 ? br->buf : NULL;
    br->line_index = 0;
}

BufferReader *bufferreader_init_from_file(BufferReader *br, const char *filepath, const char *failureMessage, LogLevel failureLevel) {
    FILE *file = fopen(filepath, "rb");
    if (!file) {
        LOG(failureLevel, failureMessage, filepath, strerror(errno));
        return NULL;
    }
    if (!br) {
        br = bufferreader_create();
    }

    // Try to determine length to avoid having to resize bufferreader
    if (fseek(file, 0L, SEEK_END) == 0) {
        bufferreader_init_for_length(br, ftell(file));
        if (fseek(file, 0, SEEK_SET) != 0) {
            // could not seek back to beginning, bail out
            fclose(file);
            return NULL;
        }
    }

    size_t actual = fread(br->buf, 1, br->buffer_length, file);
    if (actual != br->buffer_length) {
        LOG(llevError, "Expected to read %zu bytes, only read %zu!\n", br->buffer_length, actual);
        br->buffer_length = actual;
    }
    fclose(file);
    return br;
}

void bufferreader_init_from_tar_file(BufferReader *br, mtar_t *tar, mtar_header_t *h) {
    bufferreader_init_for_length(br, h->size);
    mtar_read_data(tar, br->buf, h->size);
}

BufferReader *bufferreader_init_from_memory(BufferReader *br, const char *data, size_t length) {
    if (!br) {
        br = bufferreader_create();
    }
    bufferreader_init_for_length(br, length);
    memcpy(br->buf, data, length);
    return br;
}

char *bufferreader_next_line(BufferReader *br) {
    if (!br->current_line) {
        return NULL;
    }

    br->line_index++;
    char *newline = strchr(br->current_line, '\n');
    char *curr = br->current_line;
    if (newline) {
        br->current_line = newline + 1;
        (*newline) = '\0';
    } else {
        br->current_line = NULL;
    }
    return curr;
}

char *bufferreader_get_line(BufferReader *br, char *buffer, size_t length) {
    if (!br->current_line) {
        return NULL;
    }

    br->line_index++;
    char *newline = strchr(br->current_line, '\n');
    char *curr = br->current_line;
    if (newline) {
        size_t cp = MIN(length, (size_t)(newline - curr + 1));
        br->current_line = newline + 1;
        strncpy(buffer, curr, cp);
        buffer[cp] = '\0';
    } else {
        strncpy(buffer, curr, length);
        br->current_line = NULL;
    }

    return buffer;
}

size_t bufferreader_current_line(BufferReader *br) {
    return br->line_index;
}

size_t bufferreader_data_length(BufferReader *br) {
    return  br->buffer_length;
}

char *bufferreader_data(BufferReader *br) {
    return br->buf;
}
