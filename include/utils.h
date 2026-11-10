#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

/* Funzioni di I/O sicuro da terminale (per il Client) */
int safe_read_line(char *buf, size_t size);
int get_valid_int(const char *prompt, int min, int max, int *out_val);
int get_valid_coords(const char *prompt, int *row, int *col);

/* Utilità di parsing e stringhe */
char *trim_whitespace(char *str);
bool parse_int_in_range(const char *str, int min, int max, int *out_val);
bool parse_coord_str(const char *str, int *row, int *col);
void safe_str_copy(char *dest, const char *src, size_t dest_size);

/* Logging formattato */
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_error(const char *fmt, ...);

#endif /* UTILS_H */
