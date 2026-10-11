#include "ls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int traverse_directory(const char *dir_path, const LsOptions *opts, bool print_header) {
    if (print_header) {
        printf("%s:\n", dir_path);
    }

    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        fprintf(stderr, "my_ls: %s: %s\n", dir_path, strerror(errno));
        set_exit_status(1);
        return 1;
    }

    size_t capacity = 64;
    size_t count = 0;
    FileInfo **files = malloc(sizeof(FileInfo *) * capacity);
    if (files == NULL) {
        fprintf(stderr, "my_ls: out of memory\n");
        set_exit_status(1);
        closedir(dir);
        return 1;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;

        /* Filter directory entries */
        if (!opts->show_all) {
            if (opts->almost_all) {
                if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                    continue;
                }
            } else {
                if (name[0] == '.') {
                    continue;
                }
            }
        }

        FileInfo *fi = file_info_create(dir_path, name);
        if (fi == NULL) {
            continue;
        }

        if (count >= capacity) {
            size_t new_cap = capacity * 2;
            FileInfo **new_files = realloc(files, sizeof(FileInfo *) * new_cap);
            if (new_files == NULL) {
                file_info_free(fi);
                break;
            }
            files = new_files;
            capacity = new_cap;
        }

        files[count++] = fi;
    }

    closedir(dir);

    /* Sort entries */
    sort_files(files, count, opts);

    /* Display directory entries */
    display_files(files, count, opts, true);

    /* Recursively list subdirectories if -R is set */
    if (opts->recursive) {
        for (size_t i = 0; i < count; i++) {
            if (files[i]->stat_ok && S_ISDIR(files[i]->st.st_mode)) {
                /* Never recurse into '.' or '..' */
                if (strcmp(files[i]->name, ".") == 0 || strcmp(files[i]->name, "..") == 0) {
                    continue;
                }
                printf("\n");
                traverse_directory(files[i]->full_path, opts, true);
            }
        }
    }

    /* Free all allocated FileInfo structures */
    for (size_t i = 0; i < count; i++) {
        file_info_free(files[i]);
    }
    free(files);

    return 0;
}
