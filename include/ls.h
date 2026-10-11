#ifndef LS_H
#define LS_H
#ifndef _NETBSD_SOURCE
#define _NETBSD_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>

/* Time field selection for sorting (-t) and display (-l) */
typedef enum {
    TIME_MTIME = 0, /* Default: file modification time */
    TIME_CTIME,     /* -c: file status change time */
    TIME_ATIME      /* -u: file access time */
} TimeType;

/* Command-line options according to NetBSD 10.1 ls(1) */
typedef struct {
    bool show_all;         /* -a: include entries starting with '.' */
    bool almost_all;       /* -A: list all except '.' and '..' */
    bool dir_as_file;      /* -d: treat directories as plain files */
    bool classify;         /* -F: append type indicator (/ * @ % = |) */
    bool unsorted;         /* -f: output not sorted (also enables -a) */
    bool human_readable;   /* -h: sizes in human-readable format */
    bool inode;            /* -i: print file serial number (inode) */
    bool kilobyte;         /* -k: sizes/blocks reported in kilobytes */
    bool long_format;      /* -l: list in long format */
    bool numeric_ids;      /* -n: list numeric UID and GID */
    bool hide_nonprint;    /* -q: non-printable chars replaced with '?' */
    bool recursive;        /* -R: recursively list subdirectories */
    bool reverse_sort;     /* -r: reverse sorting order */
    bool sort_by_size;     /* -S: sort by file size descending */
    bool show_blocks;      /* -s: display allocated blocks */
    bool sort_by_time;     /* -t: sort by time modified/specified */
    bool raw_nonprint;     /* -w: force raw printing of non-printable chars */
    TimeType time_type;    /* Selected time field: mtime, ctime (-c), or atime (-u) */
    long blocksize;        /* Block size in bytes (from BLOCKSIZE env or 512) */
} LsOptions;

/* Metadata for a single directory entry or file operand */
typedef struct {
    char *name;             /* Entry name (d_name or operand name) */
    char *full_path;        /* Resolved path for lstat/readlink */
    struct stat st;         /* File status from lstat() */
    bool stat_ok;           /* True if lstat succeeded */
    int err_num;            /* errno if lstat failed */
    char *symlink_target;   /* Link destination if S_ISLNK and readable */
} FileInfo;

/* Global exit status tracking */
int get_exit_status(void);
void set_exit_status(int status);

/* Option parsing */
int parse_options(int argc, char *argv[], LsOptions *opts, int *first_operand_idx);

/* FileInfo constructors / destructors */
FileInfo *file_info_create(const char *parent_dir, const char *name);
void file_info_free(FileInfo *fi);

/* Sorting */
void sort_files(FileInfo **files, size_t count, const LsOptions *opts);

/* Formatting & Display */
void format_mode_string(mode_t mode, char *out_str);
void format_human_size(off_t bytes, char *out_str, size_t max_len);
void format_time_string(time_t t, char *out_str, size_t max_len);
void display_files(FileInfo **files, size_t count, const LsOptions *opts, bool is_dir_listing);
void display_files_with_scope(FileInfo **files, size_t count, const LsOptions *opts, bool is_dir_listing, FileInfo **scope_files, size_t scope_count);
void print_sanitized_name(const char *name, const LsOptions *opts);

/* Directory traversal & operands */
int traverse_directory(const char *dir_path, const LsOptions *opts, bool print_header);
int process_operands(int argc, char *argv[], int start_idx, const LsOptions *opts);

#endif /* LS_H */
