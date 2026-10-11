#include "ls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <grp.h>
#ifdef __linux__
#include <sys/sysmacros.h>
#endif

#ifndef major
#define major(dev) ((unsigned int)(((dev) >> 8) & 0xff))
#endif
#ifndef minor
#define minor(dev) ((unsigned int)((dev) & 0xff))
#endif

static char get_classify_char(mode_t mode) {
    if (S_ISDIR(mode)) {
        return '/';
    } else if (S_ISLNK(mode)) {
        return '@';
    } else if (S_ISSOCK(mode)) {
        return '=';
    } else if (S_ISFIFO(mode)) {
        return '|';
    } else if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) {
        return '*';
    }
    return '\0';
}

static long long get_display_blocks(const FileInfo *fi, const LsOptions *opts) {
    if (!fi->stat_ok) {
        return 0;
    }

    if (opts->kilobyte) {
        /* In 1024-byte blocks: round up */
        return (fi->st.st_blocks + 1) / 2;
    }

    if (opts->blocksize > 0 && opts->blocksize != 512) {
        return (fi->st.st_blocks * 512 + opts->blocksize - 1) / opts->blocksize;
    }

    /* Default 512-byte blocks */
    return fi->st.st_blocks;
}

static int count_digits_ll(long long n) {
    int digits = 0;
    if (n <= 0) {
        digits = 1;
    }
    while (n > 0) {
        digits++;
        n /= 10;
    }
    return digits;
}

static int count_digits_ull(unsigned long long n) {
    int digits = 0;
    if (n == 0) {
        digits = 1;
    }
    while (n > 0) {
        digits++;
        n /= 10;
    }
    return digits;
}

void display_files_with_scope(FileInfo **files, size_t count, const LsOptions *opts, bool is_dir_listing,
                              FileInfo **scope_files, size_t scope_count) {
    /* Print total blocks line for directories when -l or -s is enabled */
    if (is_dir_listing && (opts->long_format || opts->show_blocks)) {
        long long total_512 = 0;
        long long total_calc = 0;

        for (size_t i = 0; i < count; i++) {
            if (files && files[i] && files[i]->stat_ok) {
                total_512 += files[i]->st.st_blocks;
                total_calc += get_display_blocks(files[i], opts);
            }
        }

        if (opts->human_readable) {
            char hum[32];
            format_human_size(total_512 * 512, hum, sizeof(hum));
            printf("total %s\n", hum);
        } else {
            printf("total %lld\n", total_calc);
        }
    }

    if (files == NULL || count == 0) {
        return;
    }

    if (scope_files == NULL || scope_count == 0) {
        scope_files = files;
        scope_count = count;
    }

    /* Calculate column widths from scope_files */
    int max_inode_w = 0;
    int max_blocks_w = 0;
    int max_nlink_w = 0;
    int max_owner_w = 0;
    int max_group_w = 0;
    int max_size_w = 0;
    int max_major_w = 0;
    int max_minor_w = 0;
    bool has_device = false;

    for (size_t i = 0; i < scope_count; i++) {
        const FileInfo *sfi = scope_files[i];
        if (!sfi || !sfi->stat_ok) {
            continue;
        }

        if (opts->inode) {
            int w = count_digits_ull((unsigned long long)sfi->st.st_ino);
            if (w > max_inode_w) max_inode_w = w;
        }

        if (opts->show_blocks) {
            int w = 0;
            if (opts->human_readable) {
                char tmp[32];
                format_human_size(sfi->st.st_blocks * 512, tmp, sizeof(tmp));
                w = (int)strlen(tmp);
            } else {
                w = count_digits_ll(get_display_blocks(sfi, opts));
            }
            if (w > max_blocks_w) max_blocks_w = w;
        }

        if (opts->long_format) {
            int nw = count_digits_ull((unsigned long long)sfi->st.st_nlink);
            if (nw > max_nlink_w) max_nlink_w = nw;

            /* Owner string */
            char ubuf[32];
            const char *u_str = ubuf;
            if (opts->numeric_ids) {
                snprintf(ubuf, sizeof(ubuf), "%u", (unsigned int)sfi->st.st_uid);
            } else {
                struct passwd *pwd = getpwuid(sfi->st.st_uid);
                if (pwd != NULL) {
                    u_str = pwd->pw_name;
                } else {
                    snprintf(ubuf, sizeof(ubuf), "%u", (unsigned int)sfi->st.st_uid);
                }
            }
            int ow = (int)strlen(u_str);
            if (ow > max_owner_w) max_owner_w = ow;

            /* Group string */
            char gbuf[32];
            const char *g_str = gbuf;
            if (opts->numeric_ids) {
                snprintf(gbuf, sizeof(gbuf), "%u", (unsigned int)sfi->st.st_gid);
            } else {
                struct group *grp = getgrgid(sfi->st.st_gid);
                if (grp != NULL) {
                    g_str = grp->gr_name;
                } else {
                    snprintf(gbuf, sizeof(gbuf), "%u", (unsigned int)sfi->st.st_gid);
                }
            }
            int gw = (int)strlen(g_str);
            if (gw > max_group_w) max_group_w = gw;

            /* Size string / Device numbers */
            if (S_ISCHR(sfi->st.st_mode) || S_ISBLK(sfi->st.st_mode)) {
                has_device = true;
                int maj_w = count_digits_ull(major(sfi->st.st_rdev));
                int min_w = count_digits_ull(minor(sfi->st.st_rdev));
                if (maj_w > max_major_w) max_major_w = maj_w;
                if (min_w > max_minor_w) max_minor_w = min_w;
            } else {
                char sbuf[32];
                if (opts->human_readable) {
                    format_human_size(sfi->st.st_size, sbuf, sizeof(sbuf));
                } else {
                    snprintf(sbuf, sizeof(sbuf), "%lld", (long long)sfi->st.st_size);
                }
                int sw = (int)strlen(sbuf);
                if (sw > max_size_w) max_size_w = sw;
            }
        }
    }

    if (has_device) {
        int dev_w = max_major_w + 2 + max_minor_w;
        if (dev_w > max_size_w) {
            max_size_w = dev_w;
        }
    }

    /* Temporary arrays for current files being displayed */
    char **owner_names = NULL;
    char **group_names = NULL;
    char **size_strs = NULL;

    if (opts->long_format) {
        owner_names = calloc(count, sizeof(char *));
        group_names = calloc(count, sizeof(char *));
        size_strs = calloc(count, sizeof(char *));

        for (size_t i = 0; i < count; i++) {
            const FileInfo *fi = files[i];
            if (!fi->stat_ok) continue;

            /* Owner */
            char ubuf[32];
            const char *u_str = ubuf;
            if (opts->numeric_ids) {
                snprintf(ubuf, sizeof(ubuf), "%u", (unsigned int)fi->st.st_uid);
            } else {
                struct passwd *pwd = getpwuid(fi->st.st_uid);
                if (pwd != NULL) {
                    u_str = pwd->pw_name;
                } else {
                    snprintf(ubuf, sizeof(ubuf), "%u", (unsigned int)fi->st.st_uid);
                }
            }
            owner_names[i] = strdup(u_str);

            /* Group */
            char gbuf[32];
            const char *g_str = gbuf;
            if (opts->numeric_ids) {
                snprintf(gbuf, sizeof(gbuf), "%u", (unsigned int)fi->st.st_gid);
            } else {
                struct group *grp = getgrgid(fi->st.st_gid);
                if (grp != NULL) {
                    g_str = grp->gr_name;
                } else {
                    snprintf(gbuf, sizeof(gbuf), "%u", (unsigned int)fi->st.st_gid);
                }
            }
            group_names[i] = strdup(g_str);

            /* Size */
            if (!S_ISCHR(fi->st.st_mode) && !S_ISBLK(fi->st.st_mode)) {
                char sbuf[32];
                if (opts->human_readable) {
                    format_human_size(fi->st.st_size, sbuf, sizeof(sbuf));
                } else {
                    snprintf(sbuf, sizeof(sbuf), "%lld", (long long)fi->st.st_size);
                }
                size_strs[i] = strdup(sbuf);
            }
        }
    }

    /* Print each entry */
    for (size_t i = 0; i < count; i++) {
        const FileInfo *fi = files[i];

        if (opts->inode) {
            if (fi->stat_ok) {
                printf("%*llu ", max_inode_w, (unsigned long long)fi->st.st_ino);
            } else {
                printf("%*s ", max_inode_w, "?");
            }
        }

        if (opts->show_blocks) {
            if (fi->stat_ok) {
                if (opts->human_readable) {
                    char tmp[32];
                    format_human_size(fi->st.st_blocks * 512, tmp, sizeof(tmp));
                    printf("%*s ", max_blocks_w, tmp);
                } else {
                    printf("%*lld ", max_blocks_w, get_display_blocks(fi, opts));
                }
            } else {
                printf("%*s ", max_blocks_w, "?");
            }
        }

        if (opts->long_format) {
            char mode_str[16];
            if (fi->stat_ok) {
                format_mode_string(fi->st.st_mode, mode_str);
            } else {
                snprintf(mode_str, sizeof(mode_str), "??????????");
            }
            printf("%s ", mode_str);

            if (fi->stat_ok) {
                printf("%*lu ", max_nlink_w, (unsigned long)fi->st.st_nlink);
            } else {
                printf("%*s ", max_nlink_w, "?");
            }

            printf("%-*s ", max_owner_w, (owner_names && owner_names[i]) ? owner_names[i] : "?");
            printf("%-*s ", max_group_w, (group_names && group_names[i]) ? group_names[i] : "?");

            if (fi->stat_ok && (S_ISCHR(fi->st.st_mode) || S_ISBLK(fi->st.st_mode))) {
                printf("%*u, %*u ", max_major_w, (unsigned int)major(fi->st.st_rdev),
                                   max_minor_w, (unsigned int)minor(fi->st.st_rdev));
            } else {
                printf("%*s ", max_size_w, (size_strs && size_strs[i]) ? size_strs[i] : "?");
            }

            char time_str[32];
            if (fi->stat_ok) {
                time_t t = fi->st.st_mtime;
                if (opts->time_type == TIME_CTIME) {
                    t = fi->st.st_ctime;
                } else if (opts->time_type == TIME_ATIME) {
                    t = fi->st.st_atime;
                }
                format_time_string(t, time_str, sizeof(time_str));
            } else {
                snprintf(time_str, sizeof(time_str), "------------");
            }
            printf("%s ", time_str);
        }

        /* Entry name */
        print_sanitized_name(fi->name, opts);

        /* Classify indicator */
        if (opts->classify && fi->stat_ok) {
            if (!opts->long_format || !S_ISLNK(fi->st.st_mode)) {
                char ind = get_classify_char(fi->st.st_mode);
                if (ind != '\0') {
                    putchar(ind);
                }
            }
        }

        /* Symbolic link destination */
        if (opts->long_format && fi->stat_ok && S_ISLNK(fi->st.st_mode)) {
            if (fi->symlink_target != NULL) {
                printf(" -> %s", fi->symlink_target);
            }
        }

        putchar('\n');
    }

    /* Clean up temporary strings */
    if (owner_names) {
        for (size_t i = 0; i < count; i++) free(owner_names[i]);
        free(owner_names);
    }
    if (group_names) {
        for (size_t i = 0; i < count; i++) free(group_names[i]);
        free(group_names);
    }
    if (size_strs) {
        for (size_t i = 0; i < count; i++) free(size_strs[i]);
        free(size_strs);
    }
}

void display_files(FileInfo **files, size_t count, const LsOptions *opts, bool is_dir_listing) {
    display_files_with_scope(files, count, opts, is_dir_listing, files, count);
}
