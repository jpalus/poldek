/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#if HAVE_STRSIGNAL
# define __USE_GNU 1
#endif

#include <string.h>

//#if HAVE_STRSIGNAL
//# undef __USE_GNU
//#endif


#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <obstack.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fnmatch.h>

#include <rpm/rpmlib.h>
#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nhash.h>

#include <vfile/vfile.h>
#include <vfile/p_open.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"

#ifdef HAVE_FORKPTY

static void rpmr_process_output(struct p_open_st *st, int verbose_level) 
{
    int           endl = 1;

    while (1) {
        struct timeval to = { 1, 0 };
        fd_set fdset;
        int rc;
        
        FD_ZERO(&fdset);
        FD_SET(st->fd, &fdset);
        if ((rc = select(st->fd + 1, &fdset, NULL, NULL, &to)) < 0) {
            if (errno == EAGAIN)
                continue;
            
            break;
            
        } else if (rc > 0) {
            char  buf[2048];
            int   n;
            
            if ((n = read(st->fd, buf, sizeof(buf) - 1)) <= 0) {
                break;
            
            } else {
                int i;
                
                buf[n] = '\0';
                msg_tty(verbose_level, "_%s", buf);
                if (!log_enabled_filelog())
                    continue;
                
                for (i=0; i < n; i++) {
                    int c = buf[i];
                
                    if (c == '\r')
                        continue;
                
                    if (c == '\n')
                        endl = 1;
                    
                    if (endl) {
                        endl = 0;
                        msg_f(0, "_\n");
                        msg_f(0, "rpm: ");
                        continue;
                    }
                    msg_f(0, "_%c", c);
                }
            }
        }
    }
}


int rpmr_exec(const char *cmd, char *const argv[], int ontty, int verbose_level)
{
    struct p_open_st pst;
    int n, ec;
    
    p_st_init(&pst);
    if (ontty) {
        if (pty_open(&pst, cmd, argv) == NULL) 
            return 0;
    } else {
        if (p_open(&pst, 0, cmd, argv) == NULL) 
            return 0;
    }
    
    n = 0;
    if (verbose == 0) {
        verbose = 1;
        n = 1;
    }

    rpmr_process_output(&pst, verbose_level);
    if ((ec = p_close(&pst) != 0) && pst.errmsg != NULL)
        logn(LOGERR, "%s", pst.errmsg);

    p_st_destroy(&pst);
    
    if (n)
        verbose--;
    
    return ec;
}

#else  /* HAVE_FORKPTY */

int rpmr_exec(const char *cmd, char *const argv[], int ontty, int verbose_level)
{
    int rc, st, n;
    pid_t pid;

    ontty = ontty;               /* unused */
    
    msg_f(1, _("Executing %s "), cmd);
    n = 0;
    while (argv[n])
        msg_f(1, "_%s ", argv[n++]);
    msg_f(1, "\n");
    if (access(cmd, X_OK) != 0) {
        logn(LOGERR, _("%s: no such file"), cmd);
        return -1;
    }
    
    if ((pid = fork()) == 0) {
        execv(cmd, argv);
	exit(EXIT_FAILURE);
        
    } else if (pid < 0) {
        logn(LOGERR, _("%s: no such file"), cmd);
        return -1;
    }

    rc = 0;
    while (wait(&st) > 0) {
        if (WIFEXITED(st)) {
            const char *errmsg_exited = N_("%s exited with %d");
            rc = WEXITSTATUS(st);
            if (rc != 0)
                logn(LOGERR, _(errmsg_exited), cmd, WEXITSTATUS(st));
            else 
                msgn_f(1, _(errmsg_exited), cmd, rc);
            
        } else if (WIFSIGNALED(st)) {
#if HAVE_STRSIGNAL
            logn(LOGERR, _("%s terminated by signal %s"), cmd,
                 strsignal(WTERMSIG(st)));
#else
            logn(LOGERR, _("%s terminated by signal %d"), cmd,
                 WTERMSIG(st));
#endif        
            rc = -1;
        } else {
            logn(LOGERR, _("%s died under inscrutable circumstances"), cmd);
            rc = -1;
        }
    }
    
    return rc;
}

#endif /* HAVE_FORKPTY */


int packages_rpminstall(tn_array *pkgs, struct pkgset *ps, struct inst_s *inst) 
{
    char **argv;
    char *cmd;
    int i, n, nopts = 0, ec;
    int nv = verbose;
    
    n = 128 + n_array_size(pkgs);
    argv = alloca((n + 1) * sizeof(*argv));
    argv[n] = NULL;
    n = 0;

    
    if (!packages_fetch(pkgs, inst->cachedir, 0))
        return 0;
    
    if (inst->instflags & PKGINST_TEST) {
        cmd = "/bin/rpm";
        argv[n++] = "rpm";
        
    } else if (inst->flags & INSTS_USESUDO) {
        cmd = "/usr/bin/sudo";
        argv[n++] = "sudo";
        argv[n++] = "/bin/rpm";
        
    } else {
        cmd = "/bin/rpm";
        argv[n++] = "rpm";
    }
    
    if (ps->flags & PSMODE_INSTALL)
        argv[n++] = "--install";
    else if (ps->flags & PSMODE_UPGRADE)
        argv[n++] = "--upgrade";
    else {
        n_assert(0);
        die();
    }

    if (nv > 0) {
        argv[n++] = "-vh";
        nv--;
    }

    if (nv > 0)
        nv--;
    
    while (nv-- > 0) 
        argv[n++] = "-v";
    
    if (inst->instflags & PKGINST_TEST)
        argv[n++] = "--test";
    
    if (inst->instflags & PKGINST_JUSTDB)
        argv[n++] = "--justdb";
        
    if (inst->instflags & PKGINST_FORCE)
        argv[n++] = "--force";
    
    if (inst->instflags & PKGINST_NODEPS)
        argv[n++] = "--nodeps";
	
    if (inst->rootdir) {
    	argv[n++] = "--root";
	argv[n++] = (char*)inst->rootdir;
    }

    argv[n++] = "--noorder";    /* packages always ordered by me */

    if (inst->rpmacros) 
        for (i=0; i<n_array_size(inst->rpmacros); i++) {
            argv[n++] = "--define";
            argv[n++] = n_array_nth(inst->rpmacros, i);
        }
    
    if (inst->rpmopts) 
        for (i=0; i<n_array_size(inst->rpmopts); i++)
            argv[n++] = n_array_nth(inst->rpmopts, i);
    
    nopts = n;
    for (i=0; i < n_array_size(ps->ordered_pkgs); i++) {
        struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
        if (pkg_is_marked(pkg)) {
            char path[PATH_MAX], *s, *name;
            char *pkgpath = pkg->pkgdir->path;
            int len;
            
            
            name = pkg_filename_s(pkg);
            
            if (vfile_url_type(pkgpath) == VFURL_PATH) {
                len = snprintf(path, sizeof(path), "%s/%s", pkgpath, name);
            
            } else {
                char buf[1024];
                
                vfile_url_as_dirpath(buf, sizeof(buf), pkgpath);
                len = snprintf(path, sizeof(path), "%s/%s/%s", inst->cachedir,
                               buf, name);
            }

            s = alloca(len + 1);
            memcpy(s, path, len);
            s[len] = '\0';
            argv[n++] = s;
        }
    }
    
        
    n_assert(n > nopts); 
    argv[n++] = NULL;

    if (verbose > 0) {
        char buf[1024], *p;
        p = buf;
        
        for (i=0; i<nopts; i++) 
            p += snprintf(p, &buf[sizeof(buf) - 1] - p, " %s", argv[i]);
        *p = '\0';
        msgn(1, _("Executing%s..."), buf);
    }
    

    ec = rpmr_exec(cmd, argv, 1, 1);
    
    if (ec == 0 && (inst->instflags & PKGINST_TEST) == 0 &&
        (inst->flags & INSTS_KEEP_DOWNLOADS) == 0) {
        
        n = nopts;
        for (i=0; i < n_array_size(ps->ordered_pkgs); i++) {
            struct pkg *pkg = n_array_nth(ps->ordered_pkgs, i);
            int url_type;
            
            if (!pkg_is_marked(pkg))
                continue;
            
            url_type = vfile_url_type(pkg->pkgdir->path);
            if ((url_type & (VFURL_PATH | VFURL_UNKNOWN | VFURL_CDROM)) == 0) {
                DBG("unlink %s\n", argv[n]); 
                unlink(argv[n]);
            }
            n++;
        }
    }

    return ec == 0;
}
