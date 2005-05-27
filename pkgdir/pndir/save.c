/*
  Copyright (C) 2000 - 2005 Pawel A. Gajda <mis@k2.net.pl>

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

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <sys/param.h>          /* for PATH_MAX */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nstream.h>
#include <trurl/n_snprintf.h>
#include <trurl/nmalloc.h>

#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "pkgdir.h"
#include "pkg.h"
#include "pkgu.h"
#include "pkgmisc.h"
#include "pkgroup.h"
#include "pndir.h"

static const char *pndir_DEFAULT_ARCH = "noarch";
static const char *pndir_DEFAULT_OS = "linux";


struct pndir_paths {
    char  path_main[PATH_MAX];
    char  path[PATH_MAX];
    char  path_md[PATH_MAX];
    char  path_dscr[PATH_MAX];
    char  fmt_dscr[PATH_MAX];
    char  path_diff_toc[PATH_MAX];
};

static
int pndir_difftoc_vaccum(const struct pndir_paths *paths);


char *pndir_mkidx_pathname(char *dest, size_t size, const char *pathname,
                           const char *suffix) 
{
    char *ext, *bn = NULL;
    int suffix_len;

    suffix_len = strlen(suffix);
    
    if (strlen(pathname) + suffix_len + 1 > size)
        return NULL;
    
    bn = n_basenam(pathname);
    if ((ext = strrchr(bn, '.')) == NULL || strcmp(ext + 1,
                                                   pndir_extension) == 0) {
        n_snprintf(dest, size, "%s%s", pathname, suffix);
        
    } else {
        int len = ext - pathname + 1;
        n_assert(len + suffix_len + strlen(ext) + 1 < size);
        n_strncpy(dest, pathname, len);
        strcat(dest, suffix);
        
        if (strstr(suffix, ext) == NULL)
            strcat(dest, ext);
        dest[size - 1] = '\0';
    }

    return dest;
}

static 
int fheader(char *hdr, size_t size, const char *name, struct pkgdir *pkgdir) 
{
    char datestr[128];
    int n;

    
    strftime(datestr, sizeof(datestr),
             "%a, %d %b %Y %H:%M:%S GMT", gmtime(&pkgdir->ts));
    
    n = n_snprintf(hdr, size, 
                   "# %s v%d.%d\n"
                   "# This file was generated by poldek " VERSION " on %s.\n"
                   "# PLEASE DO *NOT* EDIT or poldek will hate you.\n"
                   "# Contains %d packages",
                   name, FILEFMT_MAJOR, FILEFMT_MINOR,
                   datestr, pkgdir->pkgs ? n_array_size(pkgdir->pkgs) : 0);
    
    if (pkgdir->flags & PKGDIR_DIFF) {
        strftime(datestr, sizeof(datestr),
             "%a, %d %b %Y %H:%M:%S GMT", gmtime(&pkgdir->orig_ts));
        
        n += n_snprintf(&hdr[n], size - n, 
                        ", %d removed (diff from %s)",
                        pkgdir->removed_pkgs ?
                        n_array_size(pkgdir->removed_pkgs) : 0,
                        datestr);
    }
    
    n += n_snprintf(&hdr[n], size - n, "\n");
    return n;
}


static int do_unlink(const char *path) 
{
    struct stat st;
    
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        return vf_localunlink(path);
        
    return 0;
}

static tn_hash *put_avlangs(struct tndb *db, struct pkgdir *pkgdir,
                            tn_buf *nbuf /* workbuf */)
{
    tn_array *avlangs;
    tn_hash *langs_h = NULL;
    int i;
    
    n_assert(pkgdir->avlangs_h);
    
    if (n_hash_size(pkgdir->avlangs_h) == 0)
        return NULL;

    langs_h = n_hash_new(32, NULL);
    n_hash_ctl(langs_h, TN_HASH_NOCPKEY);
    n_buf_clean(nbuf);
    
    avlangs = n_hash_keys(pkgdir->avlangs_h);
    n_array_sort(avlangs);
    for (i=0; i < n_array_size(avlangs); i++) {
        struct pkgdir_avlang *avl;
        const char *lang = n_array_nth(avlangs, i);
        int percent;
        
        avl = n_hash_get(pkgdir->avlangs_h, lang);
        n_assert(avl);
        DBGF("lang? %s\n", lang);
        percent = (avl->count * 100) / n_array_size(pkgdir->pkgs);
        if (percent < 20) {
            msgn(2, _(" Omiting '%s' descriptions (%d - %d%% only)..."),
                 lang, avl->count, percent);
            continue;
        }
        n_hash_insert(langs_h, avl->lang, NULL);
        n_buf_printf(nbuf, "%s|%u:", avl->lang, avl->count);
    }
    
    if (n_buf_size(nbuf) > 0) {
        n_assert(n_hash_size(langs_h) > 0);
        tndb_put(db, pndir_tag_langs, strlen(pndir_tag_langs),
                 n_buf_ptr(nbuf), n_buf_size(nbuf) - 1); /* eat last ':' */
    }
    
    if (n_hash_size(langs_h) == 0) {
        n_hash_free(langs_h);
        langs_h = NULL;
    }
    
    return langs_h;
}
        

static
void put_pndir_header(struct tndb *db, struct pkgdir *pkgdir, unsigned flags,
                      tn_hash **langstosave_h)
{
    char    buf[4096];
    tn_buf  *nbuf;
    int     n, i;
    
    n = fheader(buf, sizeof(buf), pndir_poldeksindex, pkgdir);
    tndb_put(db, pndir_tag_hdr, strlen(pndir_tag_hdr), buf, n);

    
    nbuf = n_buf_new(4096);
    if (flags & PKGDIR_CREAT_NODESC) n_buf_printf(nbuf, "nodesc:");
    if (flags & PKGDIR_CREAT_NOFL)   n_buf_printf(nbuf, "nofl:");
    if (flags & PKGDIR_CREAT_NOUNIQ) n_buf_printf(nbuf, "nouniq:");
    if (n_buf_size(nbuf) > 0)
        tndb_put(db, pndir_tag_opt, strlen(pndir_tag_opt),
                 n_buf_ptr(nbuf), n_buf_size(nbuf) - 1); /* eat last ':' */

    
    n = n_snprintf(buf, sizeof(buf), "%lu", pkgdir->ts);
    tndb_put(db, pndir_tag_ts, strlen(pndir_tag_ts), buf, n);

    if (pkgdir->flags & PKGDIR_DIFF) {
        n = n_snprintf(buf, sizeof(buf), "%lu", pkgdir->orig_ts);
        tndb_put(db, pndir_tag_ts_orig, strlen(pndir_tag_ts_orig), buf, n);
        
        if (pkgdir->removed_pkgs && n_array_size(pkgdir->removed_pkgs)) {
            char pkgkey[256];
            int n;
            
            n_buf_clean(nbuf);
            for (i=0; i < n_array_size(pkgdir->removed_pkgs); i++) {
                struct pkg *pkg = n_array_nth(pkgdir->removed_pkgs, i);
                n = pndir_make_pkgkey(pkgkey, sizeof(pkgkey), pkg);
                //n_buf_printf(nbuf, "%s ", pkg_evr_snprintf_s(pkg));
                n_buf_write(nbuf, pkgkey, n);
                n_buf_puts(nbuf, " ");
            }
            
            tndb_put(db, pndir_tag_removed, strlen(pndir_tag_removed),
                     n_buf_ptr(nbuf), n_buf_size(nbuf));
        }
    }

    if (pkgdir->depdirs && n_array_size(pkgdir->depdirs)) {
        n_buf_clean(nbuf);
        for (i=0; i<n_array_size(pkgdir->depdirs); i++) 
            n_buf_printf(nbuf, "%s:", (char*)n_array_nth(pkgdir->depdirs, i));
        
        tndb_put(db, pndir_tag_depdirs, strlen(pndir_tag_depdirs),
                 n_buf_ptr(nbuf), n_buf_size(nbuf) - 1); /* eat last ':' */
    }
    
    DBGF("avlangs_h %p %d\n", pkgdir->avlangs_h,
           pkgdir->avlangs_h ? n_hash_size(pkgdir->avlangs_h) : 0);

    if ((flags & PKGDIR_CREAT_NODESC) == 0)
        *langstosave_h = put_avlangs(db, pkgdir, nbuf);
    

    if (pkgdir->pkgroups) {
        n_buf_clean(nbuf);
        pkgroup_idx_store(pkgdir->pkgroups, nbuf);
        if (n_buf_size(nbuf) > 0)
            tndb_put(db, pndir_tag_pkgroups, strlen(pndir_tag_pkgroups),
                     n_buf_ptr(nbuf), n_buf_size(nbuf));
        
    }
    
    n_buf_free(nbuf);
    tndb_put(db, pndir_tag_endhdr, strlen(pndir_tag_endhdr), "\n", 1);
}


int pndir_make_pkgkey(char *key, size_t size, const struct pkg *pkg)
{
    char epoch[32];
    int n, nn;
    
    *epoch = '\0';
    if (pkg->epoch)
        snprintf(epoch, sizeof(epoch), "%d:", pkg->epoch);
    
    n = n_snprintf(key, size, "%s#%s%s-%s#", pkg->name, epoch, pkg->ver,
                   pkg->rel);

    nn = n;

    if (pkg->_arch) {
        const char *arch = pkg_arch(pkg);
        if (strcmp(arch, pndir_DEFAULT_ARCH) != 0)
            n += n_snprintf(&key[n], size - n, "%s", arch);
    }
    
    if (pkg->_os) {
        const char *os = pkg_os(pkg);
        if (strcmp(os, pndir_DEFAULT_OS) != 0)
            n += n_snprintf(&key[n], size - n, ":%s", os);
    }

    if (nn == n) {              /* eat second '#' */
        n--;
        key[n] = '\0';
    }

    return n;
}


struct pkg *pndir_parse_pkgkey(char *key, int klen, struct pkg *pkg)
{
    char        *name;
    const char  *ver, *rel, *arch = NULL, *os = NULL;
    char        *evr, *buf, *p;
    int32_t     epoch;

    
    if (pkg)    /* modify key if pkg is given i.e "in-place" */
        buf = key;
    
    else {
        buf = alloca(klen + 1);
        memcpy(buf, key, klen + 1);
    }
    
    
    if ((p = strchr(buf, '#')) == NULL)
        return NULL;
    
    *p = '\0';
    p++;

    name = buf;
    evr = p;
    
    if ((p = strchr(p, '#')) != NULL) {
        *p = '\0';
        p++;
        
        if (*p == ':') {
            p++;
            os = p;
            
        } else {
            arch = p;
            if ((p = strchr(p, ':')) != NULL) {
                *p = '\0';
                p++;
                os = p;
            }
        }
    }
    
    
    if (!poldek_util_parse_evr(evr, &epoch, &ver, &rel))
        return 0;
    
    if (ver == NULL || rel == NULL) {
        logn(LOGERR, _("%s:%s: failed to parse evr string"), name, evr);
        return NULL;
    }

    if (os == NULL)
        os = pndir_DEFAULT_OS;
    
    if (arch == NULL)
        arch = pndir_DEFAULT_ARCH;

    if (pkg == NULL)
        return pkg_new(name, epoch, ver, rel, arch, os);

    pkg->name = name;
    pkg->epoch = epoch;
    pkg->ver = (char*)ver;
    pkg->rel = (char*)rel;
    pkg_set_arch(pkg, arch);
    pkg_set_os(pkg, os);
    return pkg;
}


static
int pndir_difftoc_vaccum(const struct pndir_paths *paths)
{
    tn_array     *lines; 
    char         line[2048], *dn, *bn;
    char         tmp[PATH_MAX], difftoc_path_bak[PATH_MAX];
    struct stat  st_idx, st;
    struct vfile *vf;
    int          lineno, i, len;
    off_t        diffs_size;
    
    if (stat(paths->path_main, &st_idx) != 0) {
        logn(LOGERR, "vaccum: stat %s: %m", paths->path_main);
        return 0;
    }

    memcpy(tmp, paths->path_diff_toc, sizeof(tmp));
    n_basedirnam(tmp, &dn, &bn);

    vf = vfile_open(paths->path_diff_toc, VFT_TRURLIO, VFM_RO);
    if (vf == NULL)
        return 0;
    
    lines = n_array_new(128, NULL, NULL);
    while ((len = n_stream_gets(vf->vf_tnstream, line, sizeof(line))) > 0) {
        char *l;

        l = alloca(len + 1);
        memcpy(l, line, len + 1);
        n_array_push(lines, l);
        DBGF("l = [%s]\n", l);
    }
    
    if (n_array_size(lines)) {
        snprintf(difftoc_path_bak, sizeof(difftoc_path_bak), "%s-",
                 paths->path_diff_toc);
        rename(paths->path_diff_toc, difftoc_path_bak);
    }
    vfile_close(vf);

    vf = vfile_open(paths->path_diff_toc, VFT_TRURLIO, VFM_RW);
    if (vf == NULL) {
        rename(difftoc_path_bak, paths->path_diff_toc);
        n_array_free(lines);
        return 0;
    }

    
    lineno = 0;
    diffs_size = 0;
    for (i = n_array_size(lines) - 1; i >= 0; i--) {
        char *p, *l, path[PATH_MAX];

        l = n_array_nth(lines, i);
        if ((p = strchr(l, ' ')) == NULL) {
            logn(LOGERR, _("vaccum: %s: format error"), paths->path_diff_toc);
            *l = '\0';
            continue;
        }
        
        *p = '\0';
        /*  "- 1" to save space for ".md" (to unlink md too) */
        snprintf(path, sizeof(path) - 1, "%s/%s", dn, l);

        *p = ' ';
        
        if (stat(path, &st) != 0) {
            if (errno != ENOENT)
                logn(LOGERR, "vaccum diff: stat %s: %m", l);
            *l = '\0';
            continue;
        }
        DBGF("path = %s %ld, %ld, %ld\n", path, st.st_size, diffs_size,
             st_idx.st_size);
        
        if (lineno) {
            if (vf_valid_path(path)) {
                char *p;
                
                msgn(1, _("Removing outdated %s"), n_basenam(path));
                unlink(path);
                if ((p = strrchr(path, '.')) && strcmp(p, ".gz") == 0) {
                    strcpy(p, pndir_digest_ext);
                    //msgn(1, _("Removing outdated MDD %s"), n_basenam(path));
                    unlink(path);
                }
            }
            
        } else {
            if (diffs_size + st.st_size > (st_idx.st_size * 0.9))
                lineno = i;
            else
                diffs_size += st.st_size;
        }
    }

    for (i = lineno; i < n_array_size(lines); i++) {
        char *l;
        
        l = n_array_nth(lines, i);
        if (*l)
            n_stream_printf(vf->vf_tnstream, "%s", l);
    }

    vfile_close(vf);
    n_array_free(lines);
    return 1;
}

static
int pndir_difftoc_update(const struct pkgdir *pkgdir,
                         const struct pndir_paths *paths)
{
    struct vfile   *vf;
    struct pndir   *idx;


    vf = vfile_open(paths->path_diff_toc, VFT_TRURLIO, VFM_APPEND);
    if (vf == NULL)
        return 0;

    idx = pkgdir->mod_data;
    n_assert(idx && idx->md_orig);
    n_stream_printf(vf->vf_tnstream, "%s %lu %s %lu\n", 
                    n_basenam(paths->path), pkgdir->ts,
                    idx->md_orig, pkgdir->orig_ts);
    vfile_close(vf);
    
    if (pkgdir->pkgs && n_array_size(pkgdir->pkgs))
        return pndir_difftoc_vaccum(paths);

    return 1;
}


static
int mk_paths(struct pndir_paths *paths, const char *path, struct pkgdir *pkgdir)
{
    char             suffix[64] = "", dscr_suffix[64] = "";
    char             dscr_suffix_fmt[128] = "", tmp[PATH_MAX];
    int              psize;

    memset(paths, 0, sizeof(paths));
    
    psize = PATH_MAX;
    snprintf(paths->path_main, psize, "%s", path);
    
    if ((pkgdir->flags & PKGDIR_DIFF) == 0) {
        snprintf(dscr_suffix, sizeof(dscr_suffix), "%s",
                 pndir_desc_suffix);

        snprintf(dscr_suffix_fmt, sizeof(dscr_suffix_fmt), "%s%%s%%s",
                 pndir_desc_suffix);
        
        snprintf(paths->path, psize, "%s", path);
        
    } else {
        char *dn, *bn, tsstr[32], temp[PATH_MAX];

        pndir_tsstr(tsstr, sizeof(tsstr), pkgdir->orig_ts);

        snprintf(suffix, sizeof(suffix), ".%s", tsstr);
        snprintf(dscr_suffix, sizeof(dscr_suffix), "%s.%s",
                 pndir_desc_suffix, tsstr);

        snprintf(dscr_suffix_fmt, sizeof(dscr_suffix_fmt), "%s%%s%%s.%s",
                 pndir_desc_suffix, tsstr);
        
        snprintf(temp, sizeof(temp), "%s", path);
        
        n_basedirnam(temp, &dn, &bn);
        if (!mk_dir(dn, pndir_packages_incdir))
            return 0;
		

        snprintf(tmp, psize, "%s/%s/%s", dn, pndir_packages_incdir, bn);

        if (pndir_mkidx_pathname(paths->path, psize, tmp, suffix) == NULL)
            return 0;

        snprintf(tmp, psize, "%s/%s/%s", dn, pndir_packages_incdir,
                 n_basenam(path));
        
        pndir_mkidx_pathname(paths->path_diff_toc, psize, tmp,
                             pndir_difftoc_suffix);
        path = tmp;
    }
    
    pndir_mkidx_pathname(paths->path_dscr, psize, path, dscr_suffix);
    pndir_mkidx_pathname(paths->fmt_dscr, psize, path, dscr_suffix_fmt);
    
#if ENABLE_TRACE
    printf("\nPATHS\n");
    printf("path_main  %s\n", paths->path_main);
    printf("path       %s\n", paths->path);
    printf("path_dscr  %s\n", paths->path_dscr);
    printf("path_dscrf %s\n", paths->fmt_dscr);
    printf("path_toc   %s\n\n", paths->path_diff_toc);
#endif    
    return 1;
}


static
int pndir_save_pkginfo(int nth, struct pkguinf *pkgu, struct pkgdir *pkgdir,
                       tn_hash *db_dscr_h, const char *key, int klen,
                       tn_buf *nbuf, const char *pathtmpl)
{
    
    tn_array *langs = pkguinf_langs(pkgu);
    int i;

    nth = nth;
    DBGF("langs = %d\n", n_array_size(langs));
    for (i=0; i < n_array_size(langs); i++) {
        char *lang = n_array_nth(langs, i);
        struct tndb *db;
        
        DBGF("Saving %s\n", lang);
        if (n_hash_size(pkgdir->avlangs_h) > 0 &&
            !n_hash_exists(pkgdir->avlangs_h, lang)) {
            DBGF("skip0 %s\n", lang);
            continue;
        }
        
        if ((db = pndir_db_dscr_h_dbcreat(db_dscr_h, pathtmpl, lang)) == NULL)
            return 0;
                
        n_buf_clean(nbuf);
        if (db && pkguinf_store(pkgu, nbuf, lang)) {
            const char *akey;
            char dkey[512];
            int  aklen;
            
            if (strcmp(lang, "C") == 0) {
                akey = key;
                aklen = klen;
                
            } else {
                aklen = n_snprintf(dkey, sizeof(dkey), "%s%s", key, lang);
                akey = dkey;
            }
                    
            tndb_put(db, akey, aklen, n_buf_ptr(nbuf), n_buf_size(nbuf));
            n_buf_clean(nbuf);
        }
    }
    return 1;
}


int pndir_m_create(struct pkgdir *pkgdir, const char *pathname, unsigned flags)
{
    struct tndb      *db = NULL;
    int              i, nerr = 0, save_descr = 0;
    struct pndir     *idx;
    tn_array         *keys = NULL;
    tn_buf           *nbuf = NULL;
    unsigned         st_flags = 0;
    tn_hash          *db_dscr_h = NULL;
    tn_hash          *langstosave_h = NULL;
    tn_array         *langstosave = NULL;
    struct pndir_paths paths;
    tn_array         *exclpath = NULL;

    idx = pkgdir->mod_data;
    if (pkgdir->ts == 0) 
        pkgdir->ts = time(0);

    if (pathname == NULL) {
        if (pkgdir->flags & PKGDIR_DIFF)
            pathname = pkgdir->orig_idxpath;
        else
            pathname = pndir_localidxpath(pkgdir);
    }

    n_assert(pathname);
    mk_paths(&paths, pathname, pkgdir);
    
    DBGF("flags %d\n", flags);
    msgn_tty(1, _("Writing %s..."), vf_url_slim_s(paths.path, 0));
    msgn_f(1, _("Writing %s..."), vf_url_slim_s(paths.path, 0));
    do_unlink(paths.path);
    db = tndb_creat(paths.path, PNDIR_COMPRLEVEL,
                    TNDB_NOHASH | TNDB_SIGN_DIGEST);
    if (db == NULL) {
        logn(LOGERR, "%s: %m\n", paths.path);
		nerr++;
		goto l_end;
    }

    langstosave_h = NULL;
    put_pndir_header(db, pkgdir, flags, &langstosave_h);
    if (langstosave_h)
        langstosave = n_hash_keys(langstosave_h);
    
    if (pkgdir->pkgs == NULL)
        goto l_close;

    db_dscr_h = pndir_db_dscr_h_new();
    keys = n_array_new(n_array_size(pkgdir->pkgs), free, (tn_fn_cmp)strcmp);
    nbuf = n_buf_new(1024 * 256);

    st_flags = 0;
    st_flags |= PKGSTORE_NOEVR | PKGSTORE_NOARCH | PKGSTORE_NOOS |
        PKGSTORE_NODESC;        /* have them in key */

    if (flags & PKGDIR_CREAT_NOFL)
        st_flags |= PKGSTORE_NOANYFL;

    if (flags & PKGDIR_CREAT_wRECNO)
        st_flags |= PKGSTORE_RECNO;

    save_descr = 0;
    if (pkgdir->avlangs_h && (flags & PKGDIR_CREAT_NODESC) == 0)
        save_descr = 1;

    DBGF("avlangs_h %p %d, %d\n", pkgdir->avlangs_h,
         pkgdir->avlangs_h ? n_hash_size(pkgdir->avlangs_h) : 0, save_descr);
    
    MEMINF("start");
    if (pkgdir->src && pkgdir->src->exclude_path)
        exclpath = pkgdir->src->exclude_path;

    for (i=0; i < n_array_size(pkgdir->pkgs); i++) {
        struct pkg         *pkg;
        struct pkguinf     *pkgu;
        char               key[512];
        int                klen;
        

        pkg = n_array_nth(pkgdir->pkgs, i);

        klen = pndir_make_pkgkey(key, sizeof(key), pkg);
        n_array_push(keys, n_strdupl(key, klen));

        n_buf_clean(nbuf);
        if (pkg_store(pkg, nbuf, exclpath, pkgdir->depdirs, st_flags))
            tndb_put(db, key, klen, n_buf_ptr(nbuf), n_buf_size(nbuf));
        
        if (i % 1000 == 0)
            MEMINF("%d packages", i);

        if (!save_descr)
            continue;
        
        if ((pkgu = pkg_info_ex(pkg, langstosave))) {
            int v;
            
            v = pndir_save_pkginfo(i, pkgu, pkgdir, db_dscr_h, key, klen, nbuf,
                                   paths.fmt_dscr);
            pkguinf_free(pkgu);
            if (!v) {
                nerr++;
                goto l_close;
            }
        }
    }

 l_close:
	if (db) {
		tndb_close(db);
		db = NULL;
	}

    if (db_dscr_h) {
        struct tndb *db;
        tn_array *langs;
        int i;

        langs = n_hash_keys(db_dscr_h);
        for (i=0; i < n_array_size(langs); i++) {
            const char *p, *lang = n_array_nth(langs, i);
            
            db = pndir_db_dscr_h_get(db_dscr_h, lang);
            n_assert(db);
            p = vf_url_slim_s(tndb_path(db), 0);
            msgn(2, _(" Writing '%s' descriptions %s..."), lang, p);
        }
        n_hash_free(db_dscr_h);
        db_dscr_h = NULL;
    }
    
    if ((pkgdir->flags & PKGDIR_DIFF) == 0 && nerr == 0) {
        struct pndir_digest dg;
        
        if (!pndir_digest_calc(&dg, keys))
            nerr++;
        else if (!pndir_digest_save(&dg, paths.path, pkgdir->name))
            nerr++;
    }
    
    
    if (pkgdir->flags & PKGDIR_DIFF)
        pndir_difftoc_update(pkgdir, &paths);
	
 l_end:
    if (nbuf)
        n_buf_free(nbuf);
    
    if (keys) 
        n_array_free(keys);

    if (db_dscr_h)
        n_hash_free(db_dscr_h);

    if (langstosave)
        n_array_free(langstosave);
    
    if (langstosave_h)
        n_hash_free(langstosave_h);

    MEMINF("END");
    return nerr == 0;
}


