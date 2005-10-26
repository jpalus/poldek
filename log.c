/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@k2.net.pl>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2 as
  published by the Free Software Foundation (see file COPYING for details).

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

/*
  $Id$
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <time.h>

#include <trurl/nstr.h>
#include <trurl/n_snprintf.h>
#include "i18n.h"
#include "poldek_term.h"
#define POLDEK_LOG_H_INTERNAL
#include "log.h"

int  poldek_VERBOSE = 0;

static void say_goodbye(const char *msg);
void (*poldek_log_say_goodbye)(const char *msg) = say_goodbye;

static char l_prefix[64];
static FILE *l_stream, *l_fstream = NULL;

static void vlog_tty(int pri, const char *fmt, va_list args);

static void say_goodbye(const char *msg)
{
    msg = msg;
    /* do nothing, msg is logged before die */
    return;
}

int poldek_verbose(void)
{
    return poldek_VERBOSE;
}

int poldek_set_verbose(int v)
{
    int vv = poldek_VERBOSE;
    poldek_VERBOSE = v;
    return vv;
}


void poldek_log(int pri, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    poldek_vlog(pri, 0, fmt, args);
    va_end(args);
}

void poldek_log_i(int pri, int indent, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    poldek_vlog(pri, indent, fmt, args);
    va_end(args);
}


void poldek_vlog(int pri, int indent, const char *fmt, va_list args)
{
    static int last_endlined = 1;
    char buf[1024], tmp_fmt[1024];
    int n = 0, flags, is_cont = 0, is_endlined = 0;
    
    if (*fmt == '_') {
        fmt++;
        is_cont = 1;
        
    } else if (*fmt == '\n') {
        buf[n++] = '\n';
        fmt++;
        is_endlined = 1;
    }

    if (*fmt) {
        int n;
        
        n = strlen(fmt);
        is_endlined = (fmt[n - 1] == '\n');
        
        if ((pri & LOGOPT_N) && is_endlined == 0 &&
            (int)sizeof(tmp_fmt) > n + 2) {
            
            memcpy(tmp_fmt, fmt, n);
            tmp_fmt[n++] = '\n';
            tmp_fmt[n] = '\0';
            fmt = tmp_fmt;
            is_endlined = 1;
        }
    }

    if (last_endlined == 0 && !is_cont && (pri & (LOGERR|LOGWARN)))
        buf[n++] = '\n';        
    last_endlined = is_endlined;
        
    if (indent > 0) {
        memset(&buf[n], ' ', indent);
        n += indent;
    }

    buf[n] = '\0';

    /* revert LOG[TTY|FLAGS]  */
    flags = LOGTTY | LOGFILE;
    if (pri & LOGTTY)
        flags &= ~LOGFILE;
    
    else if (pri & LOGFILE)
        flags &= ~LOGTTY;
    
    
    if (flags & LOGTTY) {
        if (l_stream == NULL) l_stream = stdout;
        fprintf(l_stream, "%s", buf);
        if (*fmt)
            vlog_tty(pri, fmt, args);
        fflush(l_stream);
    }

    if ((flags & LOGFILE) && l_fstream) {
        if (*fmt == '\0') {
            fprintf(l_fstream, "%s", buf);
            
        } else {
            if (!is_cont) {
                char timbuf[64];
                time_t t;
            
                t = time(NULL);
                strftime(timbuf, sizeof(timbuf), "%Y/%m/%d %H:%M:%S ",
                         localtime(&t));

                fprintf(l_fstream, "%s", timbuf);
            }
            if (pri & LOGERR)
                fprintf(l_fstream, "%s", _("error: "));
            else if (pri & LOGWARN)
                fprintf(l_fstream, "%s", _("warn: "));
            
            fprintf(l_fstream, "%s", buf);
            vfprintf(l_fstream, fmt, args);
            fflush(l_fstream);
        }
    }
    
    if (pri & LOGDIE) {
        char msg[1024];
        n_snprintf(msg, sizeof(msg), fmt, args);
        say_goodbye(msg);
        abort();
    }
}


void poldek_log_msg(const char *fmt, ...) 
{
    va_list args;
    if (poldek_VERBOSE > -1) {
        va_start(args, fmt);
        poldek_vlog(LOGINFO, 0, fmt, args);
        va_end(args);
    }
}

void poldek_log_err(const char *fmt, ...) 
{
    va_list args;
    if (poldek_VERBOSE > -1) {
        va_start(args, fmt);
        poldek_vlog(LOGERR, 0, fmt, args);
        va_end(args);
    }
}

void poldek_log_tty(const char *fmt, ...)
{
    va_list args;
    if (poldek_VERBOSE > -1) {
        va_start(args, fmt);
        vlog_tty(LOGINFO, fmt, args);
        va_end(args);
    }
}

void poldek_log_msg_i(int indent, const char *fmt, ...) 
{
    va_list args;
    
    va_start(args, fmt);
    poldek_vlog(LOGINFO, indent, fmt, args);
    va_end(args);
}


int poldek_log_init(const char *pathname, FILE *tty, const char *prefix)
{
    int is_not_stdstream = l_stream != stdout && l_stream != stderr;

    if (l_stream != NULL && is_not_stdstream) 
        fclose(l_stream);

    if (l_fstream)
        fclose(l_fstream);
    
    l_stream = tty;
    if (pathname)
        l_fstream = fopen(pathname, "a");

    l_prefix[0] = '\0';
    if (prefix)
        snprintf(l_prefix, sizeof(l_prefix), "%s:", prefix);
    
    if (pathname)
        return l_fstream != NULL;
    return 1;
}


void poldek_log_closelog(void)
{
    if (l_stream != NULL && l_stream != stdout && l_stream != stderr) 
	fclose(l_stream);
    
    l_stream = NULL;

    if (l_fstream) {
        fclose(l_fstream);
        l_fstream = NULL;
    }
}


FILE *poldek_log_stream(void) 
{
    return l_stream ? l_stream : stdout;
}


FILE *poldek_log_file_stream(void) 
{
    return l_fstream;
}

int poldek_log_enabled_filelog(void)
{
    return l_fstream != NULL;
}

static
void vlog_tty(int pri, const char *fmt, va_list args)
{
    char buf[2048];
    int n = 0;
    
    if (pri & LOGERR)
        n = poldek_term_snprintf_c(PRCOLOR_RED | PRAT_BOLD,
                                   buf, sizeof(buf), _("error: "));
    
    else if (pri & LOGWARN)
        n = poldek_term_snprintf_c(PRCOLOR_RED | PRAT_BOLD,
                                   buf, sizeof(buf), _("warn: "));

    else if (pri & LOGNOTICE)
        n = poldek_term_snprintf_c(PRCOLOR_YELLOW | PRAT_BOLD,
                                   buf, sizeof(buf), _("notice: "));

    if (n > 0)
        fprintf(l_stream, "%s", buf);

    vfprintf(l_stream, fmt, args);
    fflush(l_stream);
}

#if 0
static char *text_wrap(char *dest, int size, const char *text, int tolen) 
{
    char *p, *q;
    int n = 0;
    
        
    p = dest;
    while (*p) {
        n++;
        if (isspace(*p)) 
            q = p;
        
        if (len > tolen) {
            if (isspace(*p))
                dest = '\n';
            else
                dest
            
        

}
#endif
        
