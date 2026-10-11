#include "ls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int g_exit_status = 0;

int get_exit_status(void) {
    return g_exit_status;
}

void set_exit_status(int status) {
    if (status > g_exit_status) {
        g_exit_status = status;
    }
}

static void print_usage(const char *prog_name) {
    fprintf(stderr, "usage: %s [-AacdFfhiklnqRrSstuw] [file ...]\n", prog_name);
}

int parse_options(int argc, char *argv[], LsOptions *opts, int *first_operand_idx) {
    /* Initialize default option values */
    memset(opts, 0, sizeof(*opts));

    /* Terminal detection for default non-printable char policy */
    if (isatty(STDOUT_FILENO)) {
        opts->hide_nonprint = true;
        opts->raw_nonprint = false;
    } else {
        opts->hide_nonprint = false;
        opts->raw_nonprint = true;
    }

    /* NetBSD man page: -A is always set for the super-user */
    if (geteuid() == 0) {
        opts->almost_all = true;
    }

    /* BLOCKSIZE environment variable */
    opts->blocksize = 512;
    const char *bs_env = getenv("BLOCKSIZE");
    if (bs_env != NULL && *bs_env != '\0') {
        char *endptr;
        long val = strtol(bs_env, &endptr, 10);
        if (val > 0) {
            if (*endptr == 'k' || *endptr == 'K') {
                val *= 1024;
            } else if (*endptr == 'm' || *endptr == 'M') {
                val *= 1024 * 1024;
            } else if (*endptr == 'g' || *endptr == 'G') {
                val *= 1024 * 1024 * 1024;
            }
            opts->blocksize = val;
        }
    }

    opts->time_type = TIME_MTIME;

    int ch;
    opterr = 1;
    while ((ch = getopt(argc, argv, "AacdFfhiklnqRrSstuw")) != -1) {
        switch (ch) {
            case 'A':
                opts->almost_all = true;
                opts->show_all = false;
                break;
            case 'a':
                opts->show_all = true;
                opts->almost_all = false;
                break;
            case 'c':
                opts->time_type = TIME_CTIME; /* Overrides -u */
                break;
            case 'd':
                opts->dir_as_file = true;
                break;
            case 'F':
                opts->classify = true;
                break;
            case 'f':
                opts->unsorted = true;
                opts->show_all = true; /* NetBSD: enables -a and disables sort */
                opts->almost_all = false;
                break;
            case 'h':
                opts->human_readable = true;
                opts->kilobyte = false; /* Overrides -k */
                break;
            case 'i':
                opts->inode = true;
                break;
            case 'k':
                opts->kilobyte = true;
                opts->human_readable = false; /* Overrides -h */
                break;
            case 'l':
                opts->long_format = true;
                opts->numeric_ids = false; /* Overrides -n */
                break;
            case 'n':
                opts->long_format = true;
                opts->numeric_ids = true; /* Overrides -l */
                break;
            case 'q':
                opts->hide_nonprint = true;
                opts->raw_nonprint = false; /* Overrides -w */
                break;
            case 'R':
                opts->recursive = true;
                break;
            case 'r':
                opts->reverse_sort = true;
                break;
            case 'S':
                opts->sort_by_size = true;
                opts->sort_by_time = false;
                break;
            case 's':
                opts->show_blocks = true;
                break;
            case 't':
                opts->sort_by_time = true;
                opts->sort_by_size = false;
                break;
            case 'u':
                opts->time_type = TIME_ATIME; /* Overrides -c */
                break;
            case 'w':
                opts->raw_nonprint = true;
                opts->hide_nonprint = false; /* Overrides -q */
                break;
            case '?':
            default:
                print_usage(argv[0]);
                set_exit_status(1);
                return -1;
        }
    }

    *first_operand_idx = optind;
    return 0;
}

int process_operands(int argc, char *argv[], int start_idx, const LsOptions *opts) {
    /* If no operands are given, list current directory */
    if (start_idx >= argc) {
        if (opts->dir_as_file) {
            /* -d with no operands lists '.' as a file */
            FileInfo *fi = file_info_create(NULL, ".");
            if (fi != NULL) {
                FileInfo *arr[1];
                arr[0] = fi;
                display_files(arr, 1, opts, false);
                file_info_free(fi);
            }
            return get_exit_status();
        }
        return traverse_directory(".", opts, opts->recursive);
    }

    int total_operands = argc - start_idx;
    if (total_operands == 1) {
        const char *op = argv[start_idx];
        struct stat st;
        if (lstat(op, &st) != 0) {
            fprintf(stderr, "my_ls: %s: %s\n", op, strerror(errno));
            set_exit_status(1);
            return get_exit_status();
        }

        bool is_dir = false;
        if (S_ISDIR(st.st_mode)) {
            is_dir = true;
        } else if (S_ISLNK(st.st_mode) && !opts->dir_as_file && !opts->long_format && !opts->classify) {
            /* Symlink to directory without -d/-l/-F: follow to target */
            struct stat target_st;
            if (stat(op, &target_st) == 0 && S_ISDIR(target_st.st_mode)) {
                is_dir = true;
            }
        }

        if (is_dir && !opts->dir_as_file) {
            return traverse_directory(op, opts, opts->recursive);
        } else {
            FileInfo *fi = file_info_create(NULL, op);
            if (fi != NULL) {
                FileInfo *arr[1];
                arr[0] = fi;
                display_files(arr, 1, opts, false);
                file_info_free(fi);
            }
            return get_exit_status();
        }
    }

    /* Multiple operands: separate into non-directory and directory groups */
    FileInfo **non_dirs = malloc(sizeof(FileInfo *) * total_operands);
    FileInfo **dir_fis = malloc(sizeof(FileInfo *) * total_operands);
    FileInfo **all_ops = malloc(sizeof(FileInfo *) * total_operands);
    if (!non_dirs || !dir_fis || !all_ops) {
        fprintf(stderr, "my_ls: out of memory\n");
        set_exit_status(1);
        free(non_dirs);
        free(dir_fis);
        free(all_ops);
        return 1;
    }

    size_t non_dir_count = 0;
    size_t dir_count = 0;
    size_t all_count = 0;

    for (int i = start_idx; i < argc; i++) {
        const char *op = argv[i];
        struct stat st;
        if (lstat(op, &st) != 0) {
            fprintf(stderr, "my_ls: %s: %s\n", op, strerror(errno));
            set_exit_status(1);
            continue;
        }

        bool is_dir = false;
        if (S_ISDIR(st.st_mode)) {
            is_dir = true;
        } else if (S_ISLNK(st.st_mode) && !opts->dir_as_file && !opts->long_format && !opts->classify) {
            struct stat target_st;
            if (stat(op, &target_st) == 0 && S_ISDIR(target_st.st_mode)) {
                is_dir = true;
            }
        }

        FileInfo *fi = file_info_create(NULL, op);
        if (fi == NULL) {
            continue;
        }

        all_ops[all_count++] = fi;

        if (is_dir && !opts->dir_as_file) {
            dir_fis[dir_count++] = fi;
        } else {
            non_dirs[non_dir_count++] = fi;
        }
    }

    /* Sort and display non-directory operands first */
    if (non_dir_count > 0) {
        sort_files(non_dirs, non_dir_count, opts);
        display_files_with_scope(non_dirs, non_dir_count, opts, false, all_ops, all_count);
    }

    /* Sort and process directory operands */
    if (dir_count > 0) {
        sort_files(dir_fis, dir_count, opts);

        for (size_t i = 0; i < dir_count; i++) {
            /* Print newline before directory header if non_dirs printed or not first dir */
            if (non_dir_count > 0 || i > 0) {
                printf("\n");
            }
            /* Print header <dirname>: */
            printf("%s:\n", dir_fis[i]->name);
            traverse_directory(dir_fis[i]->full_path, opts, false);
        }
    }

    /* Free all created FileInfo objects */
    for (size_t i = 0; i < all_count; i++) {
        file_info_free(all_ops[i]);
    }
    free(non_dirs);
    free(dir_fis);
    free(all_ops);

    return get_exit_status();
}

int main(int argc, char *argv[]) {
    LsOptions opts;
    int first_operand_idx = 1;

    if (parse_options(argc, argv, &opts, &first_operand_idx) != 0) {
        return get_exit_status();
    }

    process_operands(argc, argv, first_operand_idx, &opts);

    return get_exit_status();
}
