/* 
  Copyright (C) 2000, 2001 Pawel A. Gajda (mis@k2.net.pl)
 
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License published by
  the Free Software Foundation (see file COPYING for details).
*/

/*
  $Id$
*/

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <trurl/nassert.h>
#include <trurl/narray.h>
#include <trurl/nmalloc.h>

#include "i18n.h"
#include "log.h"
#include "pkg.h"
#include "pkgset.h"
#include "misc.h"
#include "cli.h"
#include "op.h"

static error_t parse_opt(int key, char *arg, struct argp_state *state);
static error_t cmdl_parse_opt(int key, char *arg, struct argp_state *state);
static int install(struct cmdarg *cmdarg);

#define OPT_GID             1500
#define OPT_INST_NODEPS     (OPT_GID + 1)
#define OPT_INST_FORCE      (OPT_GID + 2)
#define OPT_INST_REINSTALL  (OPT_GID + 3)
#define OPT_INST_DOWNGRADE  (OPT_GID + 4)
#define OPT_INST_FETCH      (OPT_GID + 6)

#define OPT_INST_JUSTDB           (OPT_GID + 7)
#define OPT_INST_TEST             't'
#define OPT_INST_RPMDEF           (OPT_GID + 9)
#define OPT_INST_MKSCRIPT         (OPT_GID + 10)
#define OPT_INST_POLDEK_MKSCRIPT  (OPT_GID + 11)
#define OPT_INST_NOFOLLOW         'N'
#define OPT_INST_FRESHEN          'F'
#define OPT_INST_HOLD             (OPT_GID + 14)
#define OPT_INST_NOHOLD           (OPT_GID + 15)
#define OPT_INST_IGNORE           (OPT_GID + 16)
#define OPT_INST_NOIGNORE         (OPT_GID + 17)
#define OPT_INST_GREEDY           'G'
#define OPT_INST_UNIQNAMES        'Q'


static struct argp_option options[] = {
{"mercy", 'm', 0, 0, N_("Be tolerant for bugs which RPM tolerates"), OPT_GID},
{"force", OPT_INST_FORCE, 0, 0, N_("Be unconcerned"), OPT_GID },
{"test", 't', 0, 0, N_("Don't install, but tell if it would work or not"), OPT_GID },
{"fresh", 'F', 0, 0, N_("Upgrade packages, but only if an earlier version "
     "currently exists"), OPT_GID },

{"nofollow", 'N', 0, 0, N_("Don't automatically install packages required by "
     "selected ones"), OPT_GID },

{"greedy", 'G', 0, 0, N_("Automatically upgrade packages which dependencies "
     "are broken by unistalled ones"), OPT_GID }, 
    
{0, 'I', 0, 0, N_("Install, not upgrade packages"), OPT_GID },
{"reinstall", OPT_INST_REINSTALL, 0, 0, N_("Reinstall"), OPT_GID }, 
{"downgrade", OPT_INST_DOWNGRADE, 0, 0, N_("Downgrade"), OPT_GID },     

{"fetch", OPT_INST_FETCH, "DIR", OPTION_ARG_OPTIONAL,
N_("Do not install, only download packages"), OPT_GID },

{"nodeps", OPT_INST_NODEPS, 0, 0,
 N_("Install packages with broken dependencies"), OPT_GID },

{0,  'v', 0, 0, N_("Be verbose."), OPT_GID },
{NULL, 'h', 0, OPTION_HIDDEN, "", OPT_GID }, /* alias for -? */
{ 0, 0, 0, 0, 0, 0 },
};

static struct argp_option cmdl_options[] = {
    {0,0,0,0, N_("Package installation:"), OPT_GID - 100 },
    {"install", 'i', 0, 0, N_("Install given packages"), OPT_GID - 100 },
    {"reinstall", OPT_INST_REINSTALL, 0, 0, N_("Reinstall given packages"),
         OPT_GID - 100},
    {"downgrade", OPT_INST_DOWNGRADE, 0, 0, N_("Downgrade"), OPT_GID - 100 },     
    {"upgrade", 'u', 0, 0, N_("Upgrade given packages"), OPT_GID - 100 },
    {NULL, 'h', 0, OPTION_HIDDEN, "", OPT_GID - 100 }, /* for compat with -Uvh */
    { 0, 0, 0, 0, 0, 0 },
};


struct command command_install = {
    COMMAND_HASVERBOSE | COMMAND_MODIFIESDB, 
    "install", N_("PACKAGE..."), N_("Install packages"), 
    options, parse_opt,
    NULL, install, NULL, NULL, NULL
};

static struct argp cmd_argp = {
    options, parse_opt, 0, 0, 0, 0, 0
};

static struct argp_child cmd_argp_child[2] = {
    { &cmd_argp, 0, NULL, 0 },
    { NULL, 0, NULL, 0 },
};

static struct argp poclidek_argp = {
    cmdl_options, cmdl_parse_opt, 0, 0, cmd_argp_child, 0, 0
};


static 
struct argp_child poclidek_argp_child = {
    &poclidek_argp, 0, NULL, OPT_GID,
};


static int cmdl_run(struct poclidek_opgroup_rt *rt);

struct poclidek_opgroup poclidek_opgroup_install = {
    "Package installation", 
    &poclidek_argp, 
    &poclidek_argp_child,
    cmdl_run
};

struct cmdl_arg_s {
    struct cmdarg cmdarg;
};

static
error_t cmdl_parse_opt(int key, char *arg, struct argp_state *state)
{
    struct poclidek_opgroup_rt *rt;
    struct poldekcli_ctx *cctx = NULL;
    struct cmdl_arg_s *arg_s;


    rt = state->input;
    cctx = rt->cctx;
    
    if (rt->_opdata != NULL) {
        arg_s = rt->_opdata;
        
    } else {
        arg_s = n_malloc(sizeof(*arg_s));
        memset(arg_s, 0, sizeof(*arg_s));
        arg_s->cmdarg.cctx = rt->cctx;
        rt->_opdata = arg_s;
        
    }
    
    arg = arg;
    switch (key) {
        case ARGP_KEY_INIT:
            state->child_inputs[0] = &arg_s->cmdarg;
            state->child_inputs[1] = NULL;
            printf("parse_opt0 %p %p\n", state->child_inputs[0], arg_s->cmdarg.cctx);
            //cmdarg->cctx->pkgset->flags |= PSMODE_UPGRADE;
            //cmdarg->cctx->pkgset->flags &= ~PSMODE_INSTALL;
            poldek_configure_f_reset(cctx->ctx);
            //poldek_configure_f(cctx->ctx, INSTS_UPGRADE);
            break;


            
        case OPT_INST_DOWNGRADE:
        case OPT_INST_REINSTALL:
        case 'U':
        case 'u':
        case 'i':
            poldek_configure_f(cctx->ctx, INSTS_INSTALL);

            if (key == 'u' || key == 'U')
                poldek_configure_f(cctx->ctx, INSTS_UPGRADE);
            
            else if (key == OPT_INST_DOWNGRADE)
                poldek_configure_f(cctx->ctx, INSTS_DOWNGRADE);
            
            else if (key == OPT_INST_REINSTALL)
                poldek_configure_f(cctx->ctx, INSTS_REINSTALL);
            break;
            
    }
    
    return 0;
}

static
error_t parse_opt(int key, char *arg, struct argp_state *state)
{
    struct cmdarg *cmdarg = state->input;
    struct poldekcli_ctx *cctx;

    //printf("parse_opt  %p %p\n", cmdarg, cmdarg->cctx);
    cctx = cmdarg->cctx;
    
    
    arg = arg;
    switch (key) {
        case ARGP_KEY_INIT:
            poldek_configure_f_reset(cctx->ctx);
            //poldek_configure_f(cctx->ctx, INSTS_UPGRADE);
            break;
            
        case 'm':
            //cmdarg->cctx->pkgset->flags |= PSVERIFY_MERCY;
            break;

        case OPT_INST_NODEPS:
            poldek_configure_f(cctx->ctx, INSTS_NODEPS);
            break;
            
        case OPT_INST_FORCE:
            poldek_configure_f(cctx->ctx, INSTS_FORCE);
            break;
            
            
        case 't':
            if (poldek_configure_f_isset(cctx->ctx, INSTS_TEST))
                poldek_configure_f(cctx->ctx, INSTS_RPMTEST);
            else
                poldek_configure_f(cctx->ctx, INSTS_TEST);
            break;
            
        case 'F':
            poldek_configure_f(cctx->ctx, INSTS_FRESHEN);
            break;

        case 'N':
            poldek_configure_f_clr(cctx->ctx, INSTS_FOLLOW);
            break;

        case 'G':
            poldek_configure_f(cctx->ctx, INSTS_GREEDY);
            break;

        case 'I':
            poldek_configure_f(cctx->ctx, INSTS_INSTALL);
            poldek_configure_f_clr(cctx->ctx, INSTS_UPGRADE);
            break;

        case OPT_INST_REINSTALL:
            poldek_configure_f(cctx->ctx, INSTS_REINSTALL);
            break;

        case OPT_INST_DOWNGRADE:
            poldek_configure_f(cctx->ctx, INSTS_DOWNGRADE);
            break;

        case OPT_INST_FETCH:
            if (arg) {
                if (!is_rwxdir(arg)) {
                    logn(LOGERR, _("%s: no such directory"), arg);
                    return EINVAL;
                }
                
                poldek_configure(cctx->ctx, POLDEK_CONF_FETCHDIR, arg);
            }

            poldek_configure_f(cctx->ctx, INSTS_JUSTFETCH);
            break;
            
        default:
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}


static int install(struct cmdarg *cmdarg)
{
    tn_array              *pkgs = NULL;
    struct install_info   iinf;
    int                   i, rc = 1, is_test = 0;
    struct poldekcli_ctx  *cctx;
    
    cctx = cmdarg->cctx;
    
    sh_resolve_packages(cmdarg->pkgnames, cctx->avpkgs, &pkgs, 1);
    
    if (pkgs == NULL || n_array_size(pkgs) == 0) {
        rc = 0;
        goto l_end;
    }

    packages_unmark_all(cmdarg->cctx->avpkgs);
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);
        pkg_hand_mark(pkg);
    }

    if (poldek_configure_f_isset(cctx->ctx, INSTS_TEST | INSTS_RPMTEST)) {
        iinf.installed_pkgs = NULL;
        iinf.uninstalled_pkgs = NULL;
        
    } else {
        iinf.installed_pkgs = pkgs_array_new(16);
        iinf.uninstalled_pkgs = pkgs_array_new(16);
    }
    
    //rc = install_pkgs(cmdarg->cctx->pkgset, cmdarg->cctx->inst,
    //                  iinf.installed_pkgs ? &iinf : NULL);
    logn(LOGERR, "NFY");
    
    if (rc == 0) 
        msgn(1, _("There were errors"));


    /* update installed set */
    if (iinf.installed_pkgs && cmdarg->cctx->instpkgs) {
        
        for (i=0; i < n_array_size(iinf.uninstalled_pkgs); i++) {
            struct pkg *pkg = n_array_nth(iinf.uninstalled_pkgs, i);
            n_array_remove(cmdarg->cctx->instpkgs, pkg);
            printf("- %s\n", pkg->nvr);
        }
        n_array_sort(cmdarg->cctx->instpkgs);
        
        
        for (i=0; i < n_array_size(iinf.installed_pkgs); i++) {
            struct pkg *pkg = n_array_nth(iinf.installed_pkgs, i);
            n_array_push(cmdarg->cctx->instpkgs, pkg_link(pkg));
        }
        n_array_sort(cmdarg->cctx->instpkgs);
        
        
        //printf("s = %d\n", n_array_size(cmdarg->cctx->instpkgs));
        if (n_array_size(iinf.installed_pkgs) +
            n_array_size(iinf.uninstalled_pkgs))
            cmdarg->cctx->ts_instpkgs = time(0);
    }
    
    if (iinf.installed_pkgs) {
        n_array_free(iinf.installed_pkgs);
        n_array_free(iinf.uninstalled_pkgs);
    }
    
    
 l_end:

    if (pkgs != cmdarg->cctx->avpkgs)
        n_array_free(pkgs);
    
    return rc;
}


static int cmdl_run(struct poclidek_opgroup_rt *rt)
{
    struct poldek_ctx *ctx = rt->cctx->ctx;
    int rc;

    
    printf("oprun install %p\n", rt);

    if (!poldek_configure_f_isset(ctx, INSTS_INSTALL))
        return 0;

    if (usrpkgset_size(rt->cctx->ups) == 0) {
        logn(LOGERR, _("no packages specified"));
        return OPGROUP_RC_ERROR;
    }

    if (!poldekcli_load_packages(rt->cctx, 1))
        exit(EXIT_FAILURE);
    
    if ((rc = poldek_mark_usrset(ctx, rt->cctx->ups, 0))) {
        rc = poldek_install(ctx, NULL);
    }
    
    return rc ? 0 : OPGROUP_RC_ERROR;
}