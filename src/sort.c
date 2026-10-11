#include "ls.h"
#include <stdlib.h>
#include <string.h>

static const LsOptions *s_current_opts = NULL;

static void get_file_time(const FileInfo *fi, const LsOptions *opts, time_t *sec, long *nsec) {
    if (!fi->stat_ok) {
        *sec = 0;
        *nsec = 0;
        return;
    }
    switch (opts->time_type) {
        case TIME_CTIME:
            *sec = fi->st.st_ctime;
            *nsec = fi->st.st_ctim.tv_nsec;
            break;
        case TIME_ATIME:
            *sec = fi->st.st_atime;
            *nsec = fi->st.st_atim.tv_nsec;
            break;
        case TIME_MTIME:
        default:
            *sec = fi->st.st_mtime;
            *nsec = fi->st.st_mtim.tv_nsec;
            break;
    }
}

static int compare_file_info(const void *p1, const void *p2) {
    const FileInfo *a = *(const FileInfo * const *)p1;
    const FileInfo *b = *(const FileInfo * const *)p2;

    int cmp = 0;

    if (s_current_opts != NULL && s_current_opts->sort_by_size) {
        /* -S: Sort by size, largest file first */
        if (a->st.st_size != b->st.st_size) {
            cmp = (b->st.st_size > a->st.st_size) ? 1 : -1;
        } else {
            /* Tie-breaker: lexicographical order */
            cmp = strcmp(a->name, b->name);
        }
    } else if (s_current_opts != NULL && s_current_opts->sort_by_time) {
        /* -t: Sort by time, most recent first */
        time_t a_sec, b_sec;
        long a_nsec, b_nsec;
        get_file_time(a, s_current_opts, &a_sec, &a_nsec);
        get_file_time(b, s_current_opts, &b_sec, &b_nsec);

        if (a_sec != b_sec) {
            cmp = (b_sec > a_sec) ? 1 : -1;
        } else if (a_nsec != b_nsec) {
            cmp = (b_nsec > a_nsec) ? 1 : -1;
        } else {
            /* Tie-breaker: lexicographical order */
            cmp = strcmp(a->name, b->name);
        }
    } else {
        /* Default: Lexicographical order by entry name */
        cmp = strcmp(a->name, b->name);
    }

    if (s_current_opts != NULL && s_current_opts->reverse_sort) {
        cmp = -cmp;
    }

    return cmp;
}

void sort_files(FileInfo **files, size_t count, const LsOptions *opts) {
    if (files == NULL || count <= 1) {
        return;
    }

    /* -f: Do not sort directory entries */
    if (opts != NULL && opts->unsorted) {
        return;
    }

    s_current_opts = opts;
    qsort(files, count, sizeof(FileInfo *), compare_file_info);
    s_current_opts = NULL;
}
