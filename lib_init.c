/*
  Copyright (C) 2000 - 2002 Pawel A. Gajda <mis@pld.org.pl>

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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#include <fnmatch.h>

#include <trurl/nmalloc.h>
#include <trurl/nassert.h>
#include <trurl/n_snprintf.h>
#include <trurl/nstr.h>

#include "vfile/vfile.h"
#include "sigint/sigint.h"

#include "pkgdir/source.h"
#include "pkgdir/pkgdir.h"
#include "pkgset.h"
#include "conf.h"
#include "log.h"
#include "misc.h"
#include "i18n.h"
#include "poldek.h"
#include "poldek_intern.h"
#include "poldek_term.h"
#include "pm/pm.h"

/* _iflags */
#define SOURCES_SETUPDONE   (1 << 1)
#define CACHEDIR_SETUPDONE  (1 << 2)
#define SOURCES_LOADED      (1 << 3)
#define SETUP_DONE          (1 << 4)  

const char poldek_BUG_MAILADDR[] = "<mis@pld.org.pl>";
const char poldek_VERSION_BANNER[] = PACKAGE " " VERSION " (" VERSION_STATUS ")";
const char poldek_BANNER[] = PACKAGE " " VERSION " (" VERSION_STATUS ")\n"
"Copyright (C) 2000-2004 Pawel A. Gajda <mis@pld.org.pl>\n"
"This program may be freely redistributed under the terms of the GNU GPL v2";

static const char *poldek_logprefix = "poldek";

void (*poldek_assert_hook)(const char *expr, const char *file, int line) = NULL;
void (*poldek_malloc_fault_hook)(void) = NULL;


static void register_vf_handlers_compat(const tn_hash *htcnf);
static void register_vf_handlers(const tn_array *fetchers);

static
int get_conf_sources(struct poldek_ctx *ctx, tn_array *sources,
                     tn_array *srcs_named,
                     tn_hash *htcnf, tn_array *htcnf_sources);

static
int get_conf_opt_list(const tn_hash *htcnf, const char *name, tn_array *tolist);

extern
int poldek_load_sources__internal(struct poldek_ctx *ctx, int load_dbdepdirs);

static struct {
    const char *name;
    int op;
    int defaultv;
} default_op_map[] = {
    { "use_sudo",             POLDEK_OP_USESUDO, 0        },
    { "confirm_installation", POLDEK_OP_CONFIRM_INST, 0   },
    { "confirm_removal",      POLDEK_OP_CONFIRM_UNINST, 1 },
    { "keep_downloads",       POLDEK_OP_KEEP_DOWNLOADS, 0 },
    { "choose_equivalents_manually", POLDEK_OP_EQPKG_ASKUSER, 0 },
    { "particle_install",     POLDEK_OP_PARTICLE, 1  },
    { "follow",               POLDEK_OP_FOLLOW, 1    },
    { "obsoletes",            POLDEK_OP_OBSOLETES, 1 },
    { "conflicts",            POLDEK_OP_CONFLICTS, 1 },
    { "mercy",                POLDEK_OP_VRFYMERCY, 1 },
    { "greedy",               POLDEK_OP_GREEDY, 1    },
    { "allow_duplicates",     POLDEK_OP_ALLOWDUPS, 1 },
    { "unique_package_names", POLDEK_OP_UNIQN, 0  },
    { "promoteepoch", POLDEK_OP_PROMOTEPOCH, 0  },
    { NULL, POLDEK_OP_HOLD,   1  },
    { NULL, POLDEK_OP_IGNORE, 1  }, 
    { NULL, 0, 0 }
};

int poldek__is_setup_done(struct poldek_ctx *ctx) 
{
    return (ctx->_iflags & SETUP_DONE) == SETUP_DONE;
}


static inline void check_if_setup_done(struct poldek_ctx *ctx) 
{
    if ((ctx->_iflags & SETUP_DONE) == SETUP_DONE)
        return;

    logn(LOGERR | LOGDIE, "poldek_setup() call is a must...");
}


int poldek_selected_sources(struct poldek_ctx *ctx, unsigned srcflags_excl)
{
    int i, nsources = 0;
    
    n_array_sort(ctx->sources);
    n_array_uniq(ctx->sources);
        
    for (i=0; i < n_array_size(ctx->sources); i++) {
        struct source *src = n_array_nth(ctx->sources, i);

        if ((src->flags & srcflags_excl) == 0)
            nsources++;
    }

    return nsources;
}


static
int addsource(tn_array *sources, struct source *src,
              int justaddit, tn_array *srcs_named, int *matches) 
{
    int rc = 0;
    
    if (n_array_size(srcs_named) == 0 || justaddit) {
        sources_add(sources, src);
        rc = 1;
                
    } else {
        int i;
        int added = 0;
        
        for (i=0; i < n_array_size(srcs_named); i++) {
            struct source *s = n_array_nth(srcs_named, i);

            if (fnmatch(s->name, src->name, 0) == 0) {
                matches[i]++;
                if (added)
                    continue;
                
                /* given by name -> clear flags */
                src->flags &= ~(PKGSOURCE_NOAUTO | PKGSOURCE_NOAUTOUP);

                /* reproritize */
                src->no = s->no + matches[i];
                src->pri = 0;
                
                sources_add(sources, src);
                added = 1;
                rc = 1;
            }
        }
    }
    
    return rc;
}


static
int prepare_sources(struct poldek_ctx *ctx,
                    tn_hash *poldek_cnf, tn_array *sources)
{
    struct source   *src;
    int             i, rc = 1;
    tn_array        *srcs_path, *srcs_named;

    
    sources_score(sources);
    
    srcs_path = n_array_clone(sources);
    srcs_named = n_array_clone(sources);
    
    for (i=0; i < n_array_size(sources); i++) {
        src = n_array_nth(sources, i);
           /* supplied by -n */
        if ((src->flags & PKGSOURCE_NAMED) && src->path == NULL) 
            n_array_push(srcs_named, source_link(src));
        else if (src->path) 
            n_array_push(srcs_path, source_link(src));
        else {
            logn(LOGERR, "%s: source without name nor path",
                 src->name ? src->name : "null");
            rc = 0;
        }
    }

    if (poldek_cnf && (n_array_size(srcs_named) > 0 ||
                       n_array_size(sources) == 0)) {
        
        tn_hash *htcnf;
        tn_array *htcnf_sources;

        htcnf = poldek_conf_get_section_ht(poldek_cnf, "global");
        htcnf_sources = poldek_conf_get_section_arr(poldek_cnf, "source");

        rc = get_conf_sources(ctx, srcs_path, srcs_named, htcnf, htcnf_sources);
    }
    
    
    n_array_free(srcs_named);
    n_array_clean(sources);

    for (i=0; i < n_array_size(srcs_path); i++) {
        struct source *src = n_array_nth(srcs_path, i);
        n_array_push(sources, source_link(src));
    }

    n_array_free(srcs_path);
    n_array_sort(sources);
    n_array_uniq_ex(sources, (tn_fn_cmp)source_cmp_uniq);
    
    sources_score(sources);
    
    return rc;
}

static
struct source *source_new_htcnf(struct poldek_ctx *ctx,
                                const tn_hash *htcnf, int no) 
{
    char spec[PATH_MAX], name[20];
    struct source *src;
    int  n = 0;
    int  v;
    char *vs;
    
    
    vs = poldek_conf_get(htcnf, "name", NULL);
    if (vs == NULL) {
        n_snprintf(name, sizeof(name), "src%.2d", no);
        vs = name;
    }
    
    n += n_snprintf(&spec[n], sizeof(spec) - n, "%s", vs);

    if ((vs = poldek_conf_get(htcnf, "type", NULL)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",type=%s", vs);

    if ((v = poldek_conf_get_int(htcnf, "pri", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",pri=%d", v);
    
    if ((v = poldek_conf_get_bool(htcnf, "noauto", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",noauto");

    if ((v = poldek_conf_get_bool(htcnf, "noautoup", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",noautoup");

    if ((v = poldek_conf_get_bool(htcnf, "signed", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",sign");
    
    else if ((v = poldek_conf_get_bool(htcnf, "sign", 0)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",sign");

    if ((vs = poldek_conf_get(htcnf, "dscr", NULL)))
        n += n_snprintf(&spec[n], sizeof(spec) - n, ",dscr=%s", vs);

    vs = poldek_conf_get(htcnf, "path", NULL);
    if (vs == NULL)
        vs = poldek_conf_get(htcnf, "url", NULL);
    //printf("spec %d = %s\n", n_hash_size(htcnf), spec);
    n_assert(vs);

    n_snprintf(&spec[n], sizeof(spec) - n, " %s", vs);
    
    vs = poldek_conf_get(htcnf, "prefix", NULL);
    
    src = source_new_pathspec(NULL, spec, vs);
    get_conf_opt_list(htcnf, "exclude path", src->mkidx_exclpath);
    if (n_array_size(src->mkidx_exclpath) == 0 &&
        n_array_size(ctx->ts->mkidx_exclpath) > 0) {
        
        src->mkidx_exclpath = n_array_dup(ctx->ts->mkidx_exclpath,
                                          (tn_fn_dup)strdup);
    }
    
    return src;
    
}
    
static 
int get_conf_sources(struct poldek_ctx *ctx,
                     tn_array *sources, tn_array *srcs_named,
                     tn_hash *htcnf, tn_array *htcnf_sources)
{
    struct source   *src;
    int             i, nerr, getall = 0;
    int             *matches = NULL;
    tn_array        *list;

    if (n_array_size(srcs_named) == 0 && n_array_size(sources) == 0)
        getall = 1;
    
    else if (n_array_size(srcs_named) > 0) {
        matches = alloca(n_array_size(srcs_named) * sizeof(int));
        memset(matches, 0, n_array_size(srcs_named) * sizeof(int));
    }

    if ((list = poldek_conf_get_multi(htcnf, "source"))) {
        for (i=0; i < n_array_size(list); i++) {
            src = source_new_pathspec(NULL, n_array_nth(list, i), NULL);
            if (!addsource(sources, src, getall, srcs_named, matches))
                source_free(src);
        }
        n_array_free(list);
    }
    
    /* source\d+, prefix\d+ pairs  */
    for (i=0; i < 100; i++) {
        char opt[64], *src_val;
        
        snprintf(opt, sizeof(opt), "source%d", i);
        if ((src_val = poldek_conf_get(htcnf, opt, NULL))) {
            snprintf(opt, sizeof(opt), "prefix%d", i);
            src = source_new_pathspec(NULL, src_val,
                                      poldek_conf_get(htcnf, opt, NULL));
            
            if (!addsource(sources, src, getall, srcs_named, matches))
                source_free(src);
        }
    }

    if (htcnf_sources) {
        for (i=0; i < n_array_size(htcnf_sources); i++) {
            tn_hash *ht = n_array_nth(htcnf_sources, i);
            
            src = source_new_htcnf(ctx, ht, n_array_size(sources));
            if (!addsource(sources, src, getall, srcs_named, matches))
                source_free(src);
        }
    }

    nerr = 0;
    for (i=0; i < n_array_size(srcs_named); i++) {
        if (matches[i] == 0) {
            struct source *src = n_array_nth(srcs_named, i);
            logn(LOGERR, _("%s: no such source"), src->name);
            nerr++;
        }
    }

    if (nerr == 0 && getall)
        for (i=0; i < n_array_size(sources); i++) {
            struct source *src = n_array_nth(sources, i);
            src->no = i;
        }


    return nerr == 0;
}


static
int get_conf_opt_list(const tn_hash *htcnf, const char *name,
                      tn_array *tolist)
{
    tn_array *list;
    int i = 0;
    
    if ((list = poldek_conf_get_multi(htcnf, name))) {
        for (i=0; i < n_array_size(list); i++)
            n_array_push(tolist, n_strdup(n_array_nth(list, i)));
        
        n_array_free(list);
    }
    
    n_array_sort(tolist);
    n_array_uniq(tolist);
    return i;
}


static char *extract_handler_name(char *name, int size, const char *cmd) 
{
    char *p;
    
    n_snprintf(name, size, "%s", cmd);
    if ((p = strchr(name, ' ')))
        *p = '\0';
    
    name = n_basenam(name);
    return name;
}


static void register_vf_handlers_compat(const tn_hash *htcnf) 
{
    char name[128], *v;
    tn_array *protocols;

    protocols = n_array_new(2, NULL, (tn_fn_cmp)strcmp);
    
    if ((v = poldek_conf_get(htcnf, "ftp_http_get", NULL))) {
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "ftp");
        n_array_push(protocols, "http");
        //vfile_cnflags |= VFILE_USEXT_FTP | VFILE_USEXT_HTTP;
        vfile_register_ext_handler(name, protocols, v);
    }
    
    if ((v = poldek_conf_get(htcnf, "ftp_get", NULL))) {
        //vfile_cnflags |= VFILE_USEXT_FTP;
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "ftp");
        vfile_register_ext_handler(name, protocols, v);
    }
    
    if ((v = poldek_conf_get(htcnf, "http_get", NULL))) {
        //vfile_cnflags |= VFILE_USEXT_HTTP;
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "http");
        vfile_register_ext_handler(name, protocols, v);
    }
    
    if ((v = poldek_conf_get(htcnf, "https_get", NULL))) {
        //vfile_cnflags |= VFILE_USEXT_HTTPS;
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "https");
        vfile_register_ext_handler(name, protocols, v);
    }
        
    if ((v = poldek_conf_get(htcnf, "rsync_get", NULL))) {
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "rsync");
        vfile_register_ext_handler(name, protocols, v);
    }
    
    if ((v = poldek_conf_get(htcnf, "cdrom_get", NULL))) {
        extract_handler_name(name, sizeof(name), v);
        n_array_clean(protocols);
        n_array_push(protocols, "cdrom");
        vfile_register_ext_handler(name, protocols, v);
    }
    n_array_free(protocols);
}


static void register_vf_handlers(const tn_array *fetchers)
{
    char name[128];
    tn_array *protocols;
    int i;

    if (fetchers == NULL)
        return;

    protocols = n_array_new(2, NULL, (tn_fn_cmp)strcmp);

    for (i=0; i<n_array_size(fetchers); i++) {
        char     *nam, *cmd;
        tn_hash  *ht;
        
        ht = n_array_nth(fetchers, i);
        
        nam = poldek_conf_get(ht, "name", NULL);
        cmd  = poldek_conf_get(ht, "cmd", NULL);
        
        if (cmd == NULL)
            continue;
        
        if (nam == NULL)
            nam = extract_handler_name(name, sizeof(name), cmd);

        if (nam == NULL)
            continue;

        n_array_clean(protocols);
        get_conf_opt_list(ht, "proto", protocols);
        if (n_array_size(protocols) == 0)
            continue;

        vfile_register_ext_handler(nam, protocols, cmd);
    }
    n_array_free(protocols);
}

int set_default_vf_fetcher(int tag, const char *confvalue) 
{
    const char **tl, **tl_save, *name;
    char  *p, *val;
    int   len;

    len = strlen(confvalue) + 1;
    val = alloca(len);
    memcpy(val, confvalue, len);

    if ((p = strchr(val, ':')) == NULL || *(p + 1) == '/')
        return 0;

    *p = '\0';
    p++;
    while (isspace(*p))
        p++;
    name = p;
    
    tl = tl_save = n_str_tokl(val, ", \t");
    while (*tl) {
        vfile_configure(tag, *tl, name);
        //printf("conf %d (%s) -> (%s)\n", tag, *tl, name);
        tl++;
    }
    
    n_str_tokl_free(tl_save);
    return 1;
}


static void zlib_in_rpm(struct poldek_ctx *ctx) 
{
    char              *argv[2], *libdir, cmd[256];
    struct p_open_st  pst;
    tn_hash           *htcnf;
    int               ec;


    htcnf = poldek_conf_get_section_ht(ctx->htconf, "global");
    libdir = poldek_conf_get(htcnf, "_libdir", NULL);
    if (libdir == NULL)
        libdir = "/usr/lib";

    n_snprintf(cmd, sizeof(cmd), "%s/poldek/zlib-in-rpm.sh", libdir);
    
    argv[0] = cmd;
    argv[1] = NULL;

    p_st_init(&pst);
    if (p_open(&pst, 0, cmd, argv) == NULL) {
        p_st_destroy(&pst);
        return;
    }
    
    if ((ec = p_close(&pst)) == 0) {
        logn(LOGNOTICE, "zlib-in-rpm detected, enabling workaround");
        vfile_configure(VFILE_CONF_EXTCOMPR, 1);
    }
    
    p_st_destroy(&pst);
}


void poldek__apply_tsconfig(struct poldek_ctx *ctx, struct poldek_ts *ts)
{
    tn_hash           *htcnf = NULL;
    int               i;

    htcnf = poldek_conf_get_section_ht(ctx->htconf, "global");
    i = 0;
    DBGF("ts %p, tsctx %p\n", ts, ctx->ts);
    while (default_op_map[i].name) {
        int op = default_op_map[i].op;
        int v0 = ts->getop(ts, op);
        if (v0 == default_op_map[i].defaultv) { /* not modified by cmdl opts */
            int v = poldek_conf_get_bool(htcnf,
                                         default_op_map[i].name,
                                         default_op_map[i].defaultv);
            ts->setop(ts, op, v);
        }

        if (ts != ctx->ts &&
            ts->getop(ts, op) != ctx->ts->getop(ctx->ts, op)) {
            if (poldek_ts_op_touched(ts, op)) {
                DBGF("NOT apply %s(%d) = %d\n", default_op_map[i].name,
                   op, ts->getop(ts, op));
                goto l_continue_loop;
            }
            
            
            DBGF("apply %s(%d) = %d\n", default_op_map[i].name,
                   op, ctx->ts->getop(ctx->ts, op));
            ts->setop(ts, op, ctx->ts->getop(ctx->ts, op));
        }
    l_continue_loop:
        i++;
    }
    
    if (ts->getop(ts, POLDEK_OP_CONFIRM_INST) && verbose < 1)
        verbose = 1;
    
    if (ts->getop(ts, POLDEK_OP_GREEDY)) {
        int v = poldek_conf_get_bool(htcnf, "aggressive greedy", 1);
        ts->setop(ts, POLDEK_OP_AGGREEDY, v);
        ts->setop(ts, POLDEK_OP_FOLLOW, 1);
    }
}

    

int poldek_load_config(struct poldek_ctx *ctx, const char *path)
{
    tn_hash           *htcnf = NULL;
    char              *v;
    tn_array          *list;
    
        
    if (poldek__is_setup_done(ctx))
        logn(LOGERR | LOGDIE, "load_config() called after setup()");
    
    if (path != NULL)
        ctx->htconf = poldek_conf_load(path, 0);
    else 
        ctx->htconf = poldek_conf_loadefault();
    
    if (ctx->htconf == NULL)
        return 0;
    
    poldek__apply_tsconfig(ctx, ctx->ts);

    htcnf = poldek_conf_get_section_ht(ctx->htconf, "global");
    register_vf_handlers_compat(htcnf);
    register_vf_handlers(poldek_conf_get_section_arr(ctx->htconf, "fetcher"));

    if ((list = poldek_conf_get_multi(htcnf, "default_fetcher"))) {
        int i;
        for (i=0; i < n_array_size(list); i++)
            set_default_vf_fetcher(VFILE_CONF_DEFAULT_CLIENT,
                                   n_array_nth(list, i));
        n_array_free(list);
    }

    if ((list = poldek_conf_get_multi(htcnf, "proxy"))) {
        int i;
        for (i=0; i < n_array_size(list); i++)
            set_default_vf_fetcher(VFILE_CONF_PROXY, n_array_nth(list, i));
        n_array_free(list);
    }
    
    get_conf_opt_list(htcnf, "rpmdef", ctx->ts->rpmacros);
    get_conf_opt_list(htcnf, "hold", ctx->ts->hold_patterns);
    get_conf_opt_list(htcnf, "ignore", ctx->ts->ign_patterns);
    get_conf_opt_list(htcnf, "exclude path", ctx->ts->mkidx_exclpath);

    if ((v = poldek_conf_get(htcnf, "cachedir", NULL)))
        ctx->ts->cachedir = v;
    
    if (poldek_conf_get_bool(htcnf, "vfile_ftp_sysuser_as_anon_passwd", 0))
        vfile_configure(VFILE_CONF_SYSUSER_AS_ANONPASSWD, 1);

    if ((v = poldek_conf_get(htcnf, "default_index_type", NULL)))
        poldek_conf_PKGDIR_DEFAULT_TYPE = n_strdup(v);

    if (poldek_conf_get_bool(htcnf, "vfile_external_compress", 0))
        vfile_configure(VFILE_CONF_EXTCOMPR, 1);
    
    else if (poldek_conf_get_bool(htcnf, "auto_zlib_in_rpm", 1))
        zlib_in_rpm(ctx);

    return 1;
}


static void n_malloc_fault(void) 
{
    printf("Something wrong, something not quite right...\n"
           "Memory exhausted\n");
    exit(EXIT_FAILURE);
}


static void n_assert_hook(const char *expr, const char *file, int line) 
{
    printf("Something wrong, something not quite right.\n"
           "Assertion '%s' failed, %s:%d\n"
           "Please report this bug to %s.\n\n",
           expr, file, line, poldek_BUG_MAILADDR);
    abort();
}

static
void self_init(void) 
{
    uid_t uid;

    uid = getuid();
    if (uid != geteuid() || getgid() != getegid()) {
        logn(LOGERR, _("I'm set*id'ed, give up"));
        exit(EXIT_FAILURE);
    }
#if 0
    if (uid == 0) {
        logn(LOGWARN, _("Running me as root is not a good habit"));
        sleep(1);
    }
#endif    
}

static
void init_internal(void) 
{
#ifdef HAVE_MALLOPT
# include <malloc.h>

#if defined HAVE_MALLOC_H && defined POLDEK_MEM_DEBUG
    old_malloc_hook = __malloc_hook;
    __malloc_hook = Fnn;
#endif
    mallopt(M_MMAP_THRESHOLD, 1024);
    //mallopt(M_MMAP_MAX, 0);
    
#endif /* HAVE_MALLOPT */

    if (poldek_malloc_fault_hook == NULL)
        poldek_malloc_fault_hook = n_malloc_fault;

    if (poldek_assert_hook == NULL)
        poldek_assert_hook = n_assert_hook;

    n_assert_sethook(poldek_assert_hook);
    n_malloc_set_failhook(poldek_malloc_fault_hook);
}

static
void poldek_destroy(struct poldek_ctx *ctx) 
{
    ctx = ctx;
    if (ctx->htconf)
        n_hash_free(ctx->htconf);
    sigint_destroy();

    n_array_free(ctx->sources);
    
    if (ctx->pkgdirs) {
        n_array_free(ctx->pkgdirs);
        ctx->pkgdirs = NULL;
    }
    
    if (ctx->ps)
        pkgset_free(ctx->ps);

    poldek_ts_free(ctx->ts);
}

void poldek_free(struct poldek_ctx *ctx)
{
    poldek_destroy(ctx);
    free(ctx);
}


int poldek_configure(struct poldek_ctx *ctx, int param, ...) 
{
    va_list ap;
    int rc;
    void     *vv;
    
    va_start(ap, param);
    
    switch (param) {
        case POLDEK_CONF_SOURCE:
            vv = va_arg(ap, void*);
            if (vv) {
                struct source *src = (struct source*)vv;
                if (src->path)
                    src->path = poldek__conf_path(src->path, NULL);
                sources_add(ctx->sources, src);
            }
            break;
            
        case POLDEK_CONF_PM:
            vv = va_arg(ap, void*);
            if (vv)
                n_hash_replace(ctx->_cnf, "pm", n_strdup(vv));
            break;
            

        case POLDEK_CONF_LOGFILE:
            vv = va_arg(ap, void*);
            if (vv)
                log_init(vv, stdout, poldek_logprefix);
            break;

        case POLDEK_CONF_LOGTTY:
            vv = va_arg(ap, void*);
            if (vv)
                log_init(vv, stdout, poldek_logprefix);
            break;


        default:
            rc = poldek_ts_vconfigure(ctx->ts, param, ap);
            break;
    }
    
    va_end(ap);
    return rc;
}

static void poldek_vf_vlog_cb(int pri, const char *fmt, va_list ap)
{
    int logpri = 0;
    
    if (pri & VFILE_LOG_INFO)
        logpri |= LOGINFO;
    
    else if (pri & VFILE_LOG_ERR)
        logpri |= LOGERR;

    if (pri & VFILE_LOG_TTY)
        logpri |= LOGTTY;
    
    else {
        //snprintf(logfmt, "vf: %s", fmt);
        //fmt = logfmt;
    }
    
    vlog(logpri, 0, fmt, ap);
}

static
int poldek_init(struct poldek_ctx *ctx, unsigned flags)
{
    struct poldek_ts *ts;
    char *cachedir;
    int i;
    
    flags = flags;

    memset(ctx, 0, sizeof(*ctx));
    ctx->sources = n_array_new(4, (tn_fn_free)source_free,
                               (tn_fn_cmp)source_cmp);
    ctx->ps = NULL;
    ctx->_cnf = n_hash_new(16, free);
    n_hash_insert(ctx->_cnf, "pm", n_strdup("rpm")); /* default pm */
    
    pm_module_init();
    ctx->pmctx = NULL;
    
    ctx->ts = poldek_ts_new(NULL);
    ts = ctx->ts;
    
    i = 0;
    while (default_op_map[i].op) {
        ts->setop(ts, default_op_map[i].op, default_op_map[i].defaultv);
        i++;
    }
    
    mem_info_verbose = -1;
    verbose = 0;
    
    log_init(NULL, stdout, poldek_logprefix);
    self_init();

    bindtextdomain(PACKAGE, NULL);
    textdomain(PACKAGE);

    term_init();
    init_internal();
    pkgdirmodule_init();
    vfile_verbose = &verbose;

    
    vfile_configure(VFILE_CONF_LOGCB, poldek_vf_vlog_cb);
    cachedir = setup_cachedir(NULL);
    vfile_configure(VFILE_CONF_CACHEDIR, cachedir);
    free(cachedir);
    return 1;
}

struct poldek_ctx *poldek_new(unsigned flags)
{
    struct poldek_ctx *ctx;
    
    ctx = n_malloc(sizeof(*ctx));
    if (poldek_init(ctx, flags))
        return ctx;
    
    free(ctx);
    return NULL;
}

int poldek_setup_cachedir(struct poldek_ctx *ctx)
{
    char *path = NULL;
    
    if (ctx->_iflags & CACHEDIR_SETUPDONE)
        return 1;

    path = setup_cachedir(ctx->ts->cachedir);
    DBGF("%s -> %s\n", ctx->ts->cachedir, path);
    n_assert(path);
    free(ctx->ts->cachedir);
    ctx->ts->cachedir = path;
    vfile_configure(VFILE_CONF_CACHEDIR, path);
    ctx->_iflags |= CACHEDIR_SETUPDONE;
    return 1;
}

static
int setup_sources(struct poldek_ctx *ctx)
{
    int i, autoupa = 0;
    tn_hash *htcnf;
    
    if (ctx->_iflags & SOURCES_SETUPDONE)
        return 1;
        
    if (!prepare_sources(ctx, ctx->htconf, ctx->sources))
        return 0;

    htcnf = poldek_conf_get_section_ht(ctx->htconf, "global");
    autoupa = poldek_conf_get_bool(htcnf, "autoupa", 1);
    
    for (i=0; i < n_array_size(ctx->sources); i++) {
        struct source *src = n_array_nth(ctx->sources, i);
        if (autoupa)
            src->flags |= PKGSOURCE_AUTOUPA;
        source_set_default_type(src);
    }
    
    ctx->_iflags |= SOURCES_SETUPDONE;
    return 1;
}


static int setup_pm(struct poldek_ctx *ctx) 
{
    const char *pm = n_hash_get(ctx->_cnf, "pm");
    n_assert(pm);

    if (strcmp(pm, "rpm") == 0) {
        ctx->pmctx = pm_new(pm);
        pm_configure(ctx->pmctx, "macros", ctx->ts->rpmacros);
        
    } else if (strcmp(pm, "pset") == 0) {
        struct source *dest = NULL;
        
        n_array_sort_ex(ctx->sources, (tn_fn_cmp)source_cmp_no);
        if (n_array_size(ctx->sources) < 2) {
            logn(LOGERR, "%s: missing destination source", pm);
             
        } else {
            dest = n_array_nth(ctx->sources, n_array_size(ctx->sources) - 1);
            n_assert(dest);
            
            if (source_is_remote(dest) && 0) {
                logn(LOGERR, "%s: destination source could not be remote",
                     source_idstr(dest));
                
            } else {
                ctx->pmctx = pm_new(pm);
                pm_configure(ctx->pmctx, "source", dest);
                n_array_pop(ctx->sources); /* remove dest */
            }
        }
        
        n_array_sort(ctx->sources);
        if (ctx->pmctx) {
            ctx->ts->setop(ctx->ts, POLDEK_OP_CONFLICTS, 0);
            ctx->ts->setop(ctx->ts, POLDEK_OP_OBSOLETES, 0);
        }
        
    } else {
        logn(LOGERR, "%s: unknown PM type", pm);
        return 0;
    }

    if (ctx->pmctx == NULL)
        logn(LOGERR, "%s: PM setup failed", pm);
    
    return ctx->pmctx != NULL;
}


int poldek_setup(struct poldek_ctx *ctx) 
{
    int rc = 1;
    
    if ((ctx->_iflags & SETUP_DONE) == SETUP_DONE)
        return 1;
    
    poldek_setup_cachedir(ctx);
    
    rc = setup_sources(ctx);
    
    if (rc && !setup_pm(ctx))
        rc = 0;

    ctx->_iflags |= SETUP_DONE;
    return rc;
}


int poldek_is_sources_loaded(struct poldek_ctx *ctx) 
{
    return ctx->_iflags & SOURCES_LOADED;
}


int poldek_load_sources(struct poldek_ctx *ctx)
{
    int rc;

    check_if_setup_done(ctx);
    
    if (ctx->_iflags & SOURCES_LOADED)
        return 1;
    
    rc = poldek_load_sources__internal(ctx, 1);
    ctx->_iflags |= SOURCES_LOADED;
    return rc;
}

struct pkgdir *poldek_load_destination_pkgdir(struct poldek_ctx *ctx)
{
    return pkgdb_to_pkgdir(ctx->pmctx, ctx->ts->rootdir, NULL,
                           ctx->ts->pm_pdirsrc ? "source" : NULL,
                           ctx->ts->pm_pdirsrc ? ctx->ts->pm_pdirsrc : NULL,
                           NULL);
}



int poldek_is_interactive_on(const struct poldek_ctx *ctx)
{
    return ctx->ts->getop_v(ctx->ts, POLDEK_OP_CONFIRM_INST,
                            POLDEK_OP_CONFIRM_UNINST,
                            POLDEK_OP_EQPKG_ASKUSER, 0);
}
