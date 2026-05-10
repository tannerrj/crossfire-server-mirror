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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#ifdef WIN32
#include <windows.h>
#endif
#include "logger.h"
#include "output_file.h"


/**
 * The extension for temporary files that is appended to the original output
 * filename during write operations.
 */
#define TMP_EXT ".tmp"


FILE *of_open(OutputFile *of, const char *fname) {
    char *fname_tmp;
    FILE *f;

    fname_tmp = static_cast<char *>(malloc(strlen(fname)+sizeof(TMP_EXT)));
    if (fname_tmp == NULL) {
        LOG(llevError, "%s: out of memory\n", fname);
        return NULL;
    }

    sprintf(fname_tmp, "%s%s", fname, TMP_EXT);
    remove(fname_tmp);
    f = fopen(fname_tmp, "w");
    if (f == NULL) {
        LOG(llevError, "could not open %s: %s\n", fname_tmp, strerror(errno));
        free(fname_tmp);
        return NULL;
    }

    of->fname = strdup_local(fname);
    if (of->fname == NULL) {
        LOG(llevError, "%s: out of memory\n", fname);
        free(fname_tmp);
        fclose(f);
        return NULL;
    }
    of->fname_tmp = fname_tmp;
    of->file = f;
    return f;
}

int of_close(OutputFile *of) {
    if (ferror(of->file)) {
        LOG(llevError, "%s: write error\n", of->fname);
        fclose(of->file);
        remove(of->fname_tmp);
        free(of->fname_tmp);
        free(of->fname);
        return 0;
    }
    if (fclose(of->file) != 0) {
        LOG(llevError, "could not write %s: %s\n", of->fname, strerror(errno));
        remove(of->fname_tmp);
        free(of->fname_tmp);
        free(of->fname);
        return 0;
    }
#ifdef WIN32
    /* Windows: use MoveFileExA with replace flag to handle locked files */
    if (!MoveFileExA(of->fname_tmp, of->fname, MOVEFILE_REPLACE_EXISTING)) {
        DWORD err = GetLastError();
        char errbuf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, errbuf, sizeof(errbuf), NULL);
        LOG(llevError, "%s: cannot rename from %s: %s\n", of->fname, of->fname_tmp, errbuf);
        remove(of->fname_tmp);
        free(of->fname_tmp);
        free(of->fname);
        return 0;
    }
#else
    if (rename(of->fname_tmp, of->fname) != 0) {
        LOG(llevError, "%s: cannot rename from %s: %s\n", of->fname, of->fname_tmp, strerror(errno));
        remove(of->fname_tmp);
        free(of->fname_tmp);
        free(of->fname);
        return 0;
    }
#endif
    free(of->fname_tmp);
    free(of->fname);
    return 1;
}

void of_cancel(OutputFile *of)
{
    fclose(of->file);
    remove(of->fname_tmp);
    free(of->fname_tmp);
    free(of->fname);
}
