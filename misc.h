/* $Id$ */
#ifndef POLDEK_MISC_H
#define POLDEK_MISC_H

#include <ctype.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

#include <vfile/p_open.h>

void die(void);

char *architecture(void);

char *trimslash(char *path);
char *next_token(char **str, char delim, int *toklen);
int is_rwxdir(const char *path);

inline static int validstr(const char *str) 
{
    while (*str) {
        if (isspace(*str) || *str == '*' || *str == '?' || *str == '&')
            return 0;
        str++;
    }
    return 1;
}

extern int mem_info_verbose;
void print_mem_info(const char *prefix);
void mem_info(int level, const char *msg);

void process_cmd_output(struct p_open_st *st, const char *prefix);

#endif /* POLDEK_MISC_H */
