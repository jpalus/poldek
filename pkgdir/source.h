/* $Id$ */
#ifndef POLDEK_SOURCE_H
#define POLDEK_SOURCE_H

#include <trurl/narray.h>

/* source options */
#define PKGSOURCE_NOAUTO     (1 << 0)
#define PKGSOURCE_NOAUTOUP   (1 << 1)
#define PKGSOURCE_VRFY_GPG   (1 << 2)
#define PKGSOURCE_VRFY_PGP   (1 << 3)
#define PKGSOURCE_VRFY_SIGN  (1 << 4)
#define PKGSOURCE_TYPE       (1 << 5)
#define PKGSOURCE_PRI        (1 << 6)
#define PKGSOURCE_DSCR       (1 << 7)
#define PKGSOURCE_NAMED      (1 << 10)
#define PKGSOURCE_COMPRESS   (1 << 11)
#define PKGSOURCE_AUTOUPA    (1 << 12) /* do --upa if --up returns
                                          "desynchronized" index */

struct source {
    unsigned  flags;
    
    char      *type;            /* type (as pkgdir types) */
    char      *name;            /* source name */
    char      *path;            /* path to idx */
    char      *pkg_prefix;      /* packages prefix path */
    
    char      *compress;        /* none, gz, bz2, etc */
    int       pri;
    int       no;
    char      *dscr;
    char      *lc_lang;
    tn_array  *exclude_path;
    char      *original_type;   /* type of source repo for this source  */
    unsigned  subopt_flags;
    int       _refcnt;
};

struct source *source_malloc(void);

struct source *source_new(const char *name, const char *type,
                          const char *path, const char *pkg_prefix);
struct source *source_new_pathspec(const char *type, const char *pathspec,
                                   const char *pkg_prefix);

void source_free(struct source *src);
struct source *source_link(struct source *src);
struct source *source_set_pkg_prefix(struct source *src, const char *prefix);
struct source *source_set_type(struct source *src, const char *type);
struct source *source_set_default_type(struct source *src);

int source_cmp(const struct source *s1, const struct source *s2);
int source_cmp_uniq(const struct source *s1, const struct source *s2);
int source_cmp_name(const struct source *s1, const struct source *s2);
int source_cmp_pri(const struct source *s1, const struct source *s2);
int source_cmp_pri_name(const struct source *s1, const struct source *s2);
int source_cmp_no(const struct source *s1, const struct source *s2);

#define PKGSOURCE_UP         (1 << 0)
#define PKGSOURCE_UPA        (1 << 1)
#define PKGSOURCE_UPAUTOA    (1 << 2)

int source_update(struct source *src, unsigned flags);

void source_printf(const struct source *src);

#define source_idstr(src) \
 (((src)->flags & PKGSOURCE_NAMED) ? (src)->name : vf_url_slim_s((src)->path, 0))

#define source_is_remote(src) \
    (vf_url_type((src)->path) & VFURL_REMOTE)

#define source_is_local(src)  (!source_is_remote(src))

#define source_is_type(src, t) (strcmp((src)->type, t) == 0)

int sources_update(tn_array *sources, unsigned flags);

#define PKGSOURCE_CLEAN  (1 << 0)
#define PKGSOURCE_CLEANA (1 << 1)
int source_clean(struct source *src, unsigned flags);

int sources_clean(tn_array *sources, unsigned flags);

#define PKGSOURCE_MKIDX_NODIFF         (1 << 0)
#define PKGSOURCE_MKIDX_COMPRESSED_GZ  (1 << 1)
#define PKGSOURCE_MKIDX_COMPRESSED_BZ2 (1 << 2)

int source_make_idx(struct source *src, const char *stype, 
                    const char *dtype, const char *idxpath,
                    unsigned flags);


int sources_add(tn_array *sources, struct source *src);
void sources_score(tn_array *sources);

#endif
