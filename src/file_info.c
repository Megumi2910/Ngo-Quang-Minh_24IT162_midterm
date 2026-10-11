#include "ls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>

FileInfo *file_info_create(const char *parent_dir, const char *name) {
    if (name == NULL) {
        return NULL;
    }

    FileInfo *fi = calloc(1, sizeof(FileInfo));
    if (fi == NULL) {
        return NULL;
    }

    fi->name = strdup(name);
    if (fi->name == NULL) {
        free(fi);
        return NULL;
    }

    /* Build full path for lstat/readlink */
    if (parent_dir == NULL || parent_dir[0] == '\0') {
        fi->full_path = strdup(name);
    } else {
        size_t plen = strlen(parent_dir);
        size_t nlen = strlen(name);
        bool has_slash = (parent_dir[plen - 1] == '/');
        size_t total_len = plen + (has_slash ? 0 : 1) + nlen + 1;

        fi->full_path = malloc(total_len);
        if (fi->full_path != NULL) {
            if (has_slash) {
                snprintf(fi->full_path, total_len, "%s%s", parent_dir, name);
            } else {
                snprintf(fi->full_path, total_len, "%s/%s", parent_dir, name);
            }
        }
    }

    if (fi->full_path == NULL) {
        free(fi->name);
        free(fi);
        return NULL;
    }

    /* Call lstat() to inspect the file without dereferencing symbolic links */
    if (lstat(fi->full_path, &fi->st) == 0) {
        fi->stat_ok = true;

        /* If symbolic link, read link target */
        if (S_ISLNK(fi->st.st_mode)) {
            char target_buf[4096];
            ssize_t rlen = readlink(fi->full_path, target_buf, sizeof(target_buf) - 1);
            if (rlen > 0) {
                target_buf[rlen] = '\0';
                fi->symlink_target = strdup(target_buf);
            }
        }
    } else {
        fi->stat_ok = false;
        fi->err_num = errno;
    }

    return fi;
}

void file_info_free(FileInfo *fi) {
    if (fi == NULL) {
        return;
    }
    free(fi->name);
    free(fi->full_path);
    free(fi->symlink_target);
    free(fi);
}

void format_mode_string(mode_t mode, char *out_str) {
    /* 10 characters + null terminator */
    /* 1. File type character */
    if (S_ISREG(mode)) {
        out_str[0] = '-';
    } else if (S_ISDIR(mode)) {
        out_str[0] = 'd';
    } else if (S_ISLNK(mode)) {
        out_str[0] = 'l';
    } else if (S_ISCHR(mode)) {
        out_str[0] = 'c';
    } else if (S_ISBLK(mode)) {
        out_str[0] = 'b';
    } else if (S_ISFIFO(mode)) {
        out_str[0] = 'p';
    } else if (S_ISSOCK(mode)) {
        out_str[0] = 's';
    } else {
        out_str[0] = '?';
    }

    /* 2. Owner permissions */
    out_str[1] = (mode & S_IRUSR) ? 'r' : '-';
    out_str[2] = (mode & S_IWUSR) ? 'w' : '-';
    if (mode & S_ISUID) {
        out_str[3] = (mode & S_IXUSR) ? 's' : 'S';
    } else {
        out_str[3] = (mode & S_IXUSR) ? 'x' : '-';
    }

    /* 3. Group permissions */
    out_str[4] = (mode & S_IRGRP) ? 'r' : '-';
    out_str[5] = (mode & S_IWGRP) ? 'w' : '-';
    if (mode & S_ISGID) {
        out_str[6] = (mode & S_IXGRP) ? 's' : 'S';
    } else {
        out_str[6] = (mode & S_IXGRP) ? 'x' : '-';
    }

    /* 4. Other permissions */
    out_str[7] = (mode & S_IROTH) ? 'r' : '-';
    out_str[8] = (mode & S_IWOTH) ? 'w' : '-';
    if (mode & S_ISVTX) {
        out_str[9] = (mode & S_IXOTH) ? 't' : 'T';
    } else {
        out_str[9] = (mode & S_IXOTH) ? 'x' : '-';
    }

    out_str[10] = '\0';
}

void format_human_size(off_t bytes, char *out_str, size_t max_len) {
    if (bytes < 1024) {
        snprintf(out_str, max_len, "%lld", (long long)bytes);
        return;
    }

    static const char units[] = "KMGTPE";
    double size = (double)bytes;
    size_t unit_idx = 0;

    while (size >= 1024.0 && unit_idx < sizeof(units) - 1) {
        size /= 1024.0;
        unit_idx++;
    }

    if (unit_idx > 0) {
        unit_idx--;
    }

    char unit_char = units[unit_idx];

    if (size < 9.95) {
        double val = ceil(size * 10.0 - 1e-9) / 10.0;
        snprintf(out_str, max_len, "%.1f%c", val, unit_char);
    } else {
        long rounded = (long)ceil(size - 1e-9);
        snprintf(out_str, max_len, "%ld%c", rounded, unit_char);
    }
}

void format_time_string(time_t t, char *out_str, size_t max_len) {
    struct tm tm_buf;
    if (localtime_r(&t, &tm_buf) == NULL) {
        snprintf(out_str, max_len, "------------");
        return;
    }

    time_t now = time(NULL);
    /* 6 months is approx 180 days (180 * 24 * 3600 seconds) */
    time_t six_months = (time_t)180 * 24 * 3600;

    if (t > now || t < now - six_months) {
        /* "%b %e  %Y" -> e.g. "Jan  1  2024" */
        strftime(out_str, max_len, "%b %e  %Y", &tm_buf);
    } else {
        /* "%b %e %H:%M" -> e.g. "Oct 11 14:07" */
        strftime(out_str, max_len, "%b %e %H:%M", &tm_buf);
    }
}

void print_sanitized_name(const char *name, const LsOptions *opts) {
    if (name == NULL) {
        return;
    }

    if (opts->raw_nonprint) {
        fputs(name, stdout);
        return;
    }

    /* opts->hide_nonprint is true */
    for (const unsigned char *p = (const unsigned char *)name; *p != '\0'; p++) {
        if (isprint(*p)) {
            putchar(*p);
        } else {
            putchar('?');
        }
    }
}
