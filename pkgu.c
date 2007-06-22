/*
  Copyright (C) 2000 - 2007 Pawel A. Gajda <mis@pld-linux.org>

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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <trurl/trurl.h>
#include <trurl/nstream.h>
#include <trurl/nhash.h>

#include <iconv.h>
#include <langinfo.h>

#include "i18n.h"
#include "log.h"
#include "pkgu.h"
#include "misc.h"
#include "pm/rpm/pm_rpm.h"

#define NA_OWNED             (1 << 0)
#define RECODE_SUMMMARY      (1 << 1) /* needs to be recoded */
#define RECODE_DESCRIPTION   (1 << 2)
#define RECODED_SUMMMARY     (1 << 3) /* already recoded     */
#define RECODED_DESCRIPTION  (1 << 4)

struct pkguinf {
    char              *license;
    char              *url;
    char              *_summary;
    char              *_description;
    const char        *_encoding; /* for iconv, NFY */
    char              *vendor;
    char              *buildhost;
    char              *distro;
    
    tn_hash           *_ht;
    tn_array          *_langs;
    tn_array          *_langs_rpmhdr; /* v018x legacy: for preserving
                                         the langs order */
    tn_alloc          *_na;
    int16_t           _refcnt;
    uint16_t          _flags;
};

struct pkguinf_i18n {
    char              *summary;
    char              *description;
    char              _buf[0];
};


static
struct pkguinf_i18n *pkguinf_i18n_new(tn_alloc *na, const char *summary,
                                      const char *description)
{
    int s_len, d_len;
    struct pkguinf_i18n *inf;
    
    if (summary == NULL)
        summary = "";
    
    if (description == NULL)
        description = "";
    
    s_len = strlen(summary) + 1;
    d_len = strlen(description) + 1;
    inf = na->na_malloc(na, sizeof(*inf) + s_len + d_len);

    memcpy(inf->_buf, summary, s_len);
    memcpy(&inf->_buf[s_len], description, d_len);
    inf->summary = inf->_buf;
    inf->description = &inf->_buf[s_len];
    return inf;
}

struct pkguinf *pkguinf_new(tn_alloc *na)
{
    struct pkguinf *pkgu;
    tn_alloc *_na = NULL;

    if (na == NULL)
        na = _na = n_alloc_new(8, TN_ALLOC_OBSTACK);
        
    
    pkgu = na->na_malloc(na, sizeof(*pkgu));
    memset(pkgu, 0, sizeof(*pkgu));
    pkgu->_na = na;
    if (_na)
        pkgu->_flags |= NA_OWNED;

    pkgu->license = NULL;
    pkgu->url = NULL;
    pkgu->_summary = NULL;
    pkgu->_description = NULL;
    pkgu->vendor = NULL;
    pkgu->buildhost = NULL;

    pkgu->_ht = NULL;
    pkgu->_langs = NULL;
    pkgu->_refcnt = 0;

    return pkgu;
}

void pkguinf_free(struct pkguinf *pkgu)
{
    if (pkgu->_refcnt > 0) {
        pkgu->_refcnt--;
        return;
    }
    
    if (pkgu->_summary) {
        if (pkgu->_flags & RECODED_SUMMMARY)
            free(pkgu->_summary);
        pkgu->_summary = NULL;
    }

    if (pkgu->_description) {
        if (pkgu->_flags & RECODED_DESCRIPTION)
            free(pkgu->_description);
        pkgu->_description = NULL;
    }
    
    if (pkgu->_langs)
        n_array_free(pkgu->_langs);

    if (pkgu->_langs_rpmhdr)
        n_array_free(pkgu->_langs_rpmhdr);
    
    if (pkgu->_ht)
        n_hash_free(pkgu->_ht);

    pkgu->_langs_rpmhdr = NULL;
    pkgu->_langs = NULL;
    pkgu->_ht = NULL;
    
    if (pkgu->_flags & NA_OWNED)
        n_alloc_free(pkgu->_na);
}

/*
  Set recodable pkgu member, set _flags accordinggly to trigger
  recoding in pkguinf_get()
 */
static void pkgu_set_recodable(struct pkguinf *pkgu, int tag, char *val,
                               const char *lang)
{
    char **member = NULL;
    unsigned flag = 0, needflag = 0;
    char *usrencoding = NULL;

    switch (tag) {
        case PKGUINF_SUMMARY:
            member = &pkgu->_summary;
            flag = RECODED_SUMMMARY;
            needflag = RECODE_SUMMMARY;
            break;

        case PKGUINF_DESCRIPTION:
            member = &pkgu->_description;
            flag = RECODED_DESCRIPTION;
            needflag = RECODE_SUMMMARY;
            break;
            
        default:
            n_assert(0);
            break;
    }

    if (*member && (pkgu->_flags & flag)) {
        free((char*)*member);
        *member = NULL;
    }

    *member = val;
    pkgu->_flags &= ~needflag;
    
    if (strstr(lang, "UTF-8") == NULL) {
        *member = val;
        return;
    }

    usrencoding = nl_langinfo(CODESET);
    DBGF("CODE %s\n", usrencoding);

    if (usrencoding && n_str_ne(usrencoding, "UTF-8"))
        pkgu->_flags |= needflag;
}


static char *recode(const char *val, const char *valencoding) 
{
    char *p, *val_utf8, *usrencoding;
    size_t vlen, u_vlen;
    iconv_t cd;

    usrencoding = nl_langinfo(CODESET);
    if (usrencoding == NULL)
        return (char*)val;

    valencoding = "UTF-8";   /* XXX, support for others needed? */

    u_vlen = vlen = strlen(val);
    p = val_utf8 = n_malloc(u_vlen + 1);

    cd = iconv_open(usrencoding, valencoding);
    if (iconv(cd, (char**)&val, &vlen, &p, &u_vlen) == (size_t)-1) {
        iconv_close(cd);
        free(val_utf8);
        return (char*)val;
    }
    iconv_close(cd);
    *p = '\0';
    return val_utf8;
}

struct pkguinf *pkguinf_link(struct pkguinf *pkgu)
{
    pkgu->_refcnt++;
    return pkgu;
}

static inline
void set_member(struct pkguinf *pkgu, char **m, const char *v, int len)
{
    char *mm;

    mm = pkgu->_na->na_malloc(pkgu->_na, len + 1);
    memcpy(mm, v, len + 1);
    *m = mm;
}

static char *na_strdup(tn_alloc *na, const char *v, int len)
{
    char *mm;

    mm = na->na_malloc(na, len + 1);
    memcpy(mm, v, len + 1);
    return mm;
}


static char *cp_tag(tn_alloc *na, Header h, int rpmtag)
{
    struct rpmhdr_ent  hdrent;
    char *t = NULL;
        
    if (pm_rpmhdr_ent_get(&hdrent, h, rpmtag)) {
        char *s = pm_rpmhdr_ent_as_str(&hdrent);
        int len = strlen(s) + 1;
        t = na->na_malloc(na, len + 1);
        memcpy(t, s, len);
    }
    
    pm_rpmhdr_ent_free(&hdrent);
    return t;
}

struct pkguinf *pkguinf_ldrpmhdr(tn_alloc *na, void *hdr)
{
    char               **langs, **summs, **descrs;
    int                nsumms, ndescrs;
    int                i, n, nlangs = 0;
    struct pkguinf     *pkgu;
    Header             h = hdr;
    
    pkgu = pkguinf_new(na);
    pkgu->_ht = n_hash_new(3, NULL);
    
    if ((langs = pm_rpmhdr_langs(h))) {
        tn_array *avlangs, *sl_langs;
        char *sl_lang;

        pm_rpmhdr_get_raw_entry(h, RPMTAG_SUMMARY, (void*)&summs, &nsumms);
        pm_rpmhdr_get_raw_entry(h, RPMTAG_DESCRIPTION, (void*)&descrs, &ndescrs);
        
        n = nsumms;
        if (n > ndescrs)
            n = ndescrs;

        avlangs = n_array_new(4, free, (tn_fn_cmp)strcmp);
        pkgu->_langs_rpmhdr = n_array_new(4, free, NULL);
        
        for (i=0; i < n; i++) {
            struct pkguinf_i18n *inf;
            
            if (langs[i] == NULL)
                break;
            
            n_array_push(avlangs, n_strdup(langs[i]));
            n_array_push(pkgu->_langs_rpmhdr, n_strdup(langs[i]));
            
            inf = pkguinf_i18n_new(pkgu->_na, summs[i], descrs[i]);
            n_hash_insert(pkgu->_ht, langs[i], inf);
        }
        nlangs = n;

        sl_langs = lc_lang_select(avlangs, lc_messages_lang());
        if (sl_langs == NULL)
            sl_lang = "C";
        else
            sl_lang = n_array_nth(sl_langs, 0); 

        if (sl_lang) {
            struct pkguinf_i18n *inf;
            
            inf = n_hash_get(pkgu->_ht, sl_lang);
            n_assert(inf);
            pkgu_set_recodable(pkgu, PKGUINF_SUMMARY, inf->summary, sl_lang);
            pkgu_set_recodable(pkgu, PKGUINF_DESCRIPTION, inf->description, sl_lang);
        }

        n_array_free(avlangs);
        if (sl_langs)
            n_array_free(sl_langs);
        
        free(langs);
        free(summs);
        free(descrs);
    }

    pkgu->vendor = cp_tag(pkgu->_na, h, RPMTAG_VENDOR);
    pkgu->license = cp_tag(pkgu->_na, h, PM_RPMTAG_LICENSE);
    pkgu->url = cp_tag(pkgu->_na, h, RPMTAG_URL);
    pkgu->distro = cp_tag(pkgu->_na, h, RPMTAG_DISTRIBUTION);
    pkgu->buildhost = cp_tag(pkgu->_na, h, RPMTAG_BUILDHOST);

    return pkgu;
}

tn_array *pkguinf_langs(struct pkguinf *pkgu)
{
    if (pkgu->_langs == NULL) 
        pkgu->_langs = n_hash_keys(pkgu->_ht);
    
    if (pkgu->_langs)
        n_array_sort(pkgu->_langs);
    return pkgu->_langs;
}

#define PKGUINF_TAG_LANG     'L'
#define PKGUINF_TAG_ENDCMN   'E'

tn_buf *pkguinf_store(const struct pkguinf *pkgu, tn_buf *nbuf,
                      const char *lang)
{
    struct pkguinf_i18n *inf;

    if (lang && strcmp(lang, "C") == 0) {
        if (pkgu->license) {
            n_buf_putc(nbuf, PKGUINF_LICENSE);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, pkgu->license);
            n_buf_putc(nbuf, '\0');
        }
    
        if (pkgu->url) {
            n_buf_putc(nbuf, PKGUINF_URL);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, pkgu->url);
            n_buf_putc(nbuf, '\0');
        }
    
        if (pkgu->vendor) {
            n_buf_putc(nbuf, PKGUINF_VENDOR);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, pkgu->vendor);
            n_buf_putc(nbuf, '\0');
        }
    
        if (pkgu->buildhost) {
            n_buf_putc(nbuf, PKGUINF_BUILDHOST);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, pkgu->buildhost);
            n_buf_putc(nbuf, '\0');
        }
    
        if (pkgu->distro) {
            n_buf_putc(nbuf, PKGUINF_DISTRO);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, pkgu->distro);
            n_buf_putc(nbuf, '\0');
        }
        
        n_buf_putc(nbuf, PKGUINF_TAG_ENDCMN);
        n_buf_putc(nbuf, '\0');
    }
    
    
    n_assert(lang);
    if (lang != NULL) {
        if ((inf = n_hash_get(pkgu->_ht, lang))) {
            n_buf_putc(nbuf, PKGUINF_SUMMARY);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, inf->summary);
            n_buf_putc(nbuf, '\0');
            
            n_buf_putc(nbuf, PKGUINF_DESCRIPTION);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, inf->description);
            n_buf_putc(nbuf, '\0');
        }
        
    } else {
        int i;
        tn_array *langs = n_hash_keys(pkgu->_ht);
        
        n_assert(0);
        
        for (i=0; i < n_array_size(langs); i++) {
            char *lang = n_array_nth(langs, i);
            n_buf_putc(nbuf, PKGUINF_TAG_LANG);
            n_buf_putc(nbuf, '\0');
            n_buf_puts(nbuf, lang);
            n_buf_putc(nbuf, '\0');
            
            if ((inf = n_hash_get(pkgu->_ht, lang))) {
                n_buf_putc(nbuf, PKGUINF_SUMMARY);
                n_buf_putc(nbuf, '\0');
                n_buf_puts(nbuf, inf->summary);
                n_buf_putc(nbuf, '\0');
                
                n_buf_putc(nbuf, PKGUINF_DESCRIPTION);
                n_buf_putc(nbuf, '\0');
                n_buf_puts(nbuf, inf->description);
                n_buf_putc(nbuf, '\0');
            }
        }
    }
    
    return nbuf;
}



struct pkguinf *pkguinf_restore(tn_alloc *na, tn_buf_it *it, const char *lang)
{
    struct pkguinf *pkgu;
    char *key = NULL, *val;
    size_t len = 0;

    pkgu = pkguinf_new(na);
    
    if (lang && strcmp(lang, "C") == 0) {
        while ((key = n_buf_it_getz(it, &len))) {
            if (len > 1)
                return NULL;
            
            if (*key == PKGUINF_TAG_ENDCMN)
                break;
            
            val = n_buf_it_getz(it, &len);
            switch (*key) {
                case PKGUINF_LICENSE:
                    set_member(pkgu, &pkgu->license, val, len);
                    break;

                case PKGUINF_URL:
                    set_member(pkgu, &pkgu->url, val, len);
                    break;

                case PKGUINF_VENDOR:
                    set_member(pkgu, &pkgu->vendor, val, len);
                    break;

                case PKGUINF_BUILDHOST:
                    set_member(pkgu, &pkgu->buildhost, val, len);
                    break;

                case PKGUINF_DISTRO:
                    set_member(pkgu, &pkgu->distro, val, len);
                    break;

                default:
                    n_assert(0);
            }
        }
    }
    
    n_assert(lang);
    
    pkguinf_restore_i18n(pkgu, it, lang);
    return pkgu;
}


int pkguinf_restore_i18n(struct pkguinf *pkgu, tn_buf_it *it, const char *lang)
{
    struct pkguinf_i18n *inf;
    char *summary, *description, *key;
    size_t slen = 0, dlen = 0, len = 0;
    

    if (pkgu->_ht == NULL)
        pkgu->_ht = n_hash_new(3, NULL);
    
    else if (n_hash_exists(pkgu->_ht, lang))
        return 1;

    key = n_buf_it_getz(it, &len);
    if (*key != PKGUINF_SUMMARY)
        return 0;
    
    summary = n_buf_it_getz(it, &slen);

    key = n_buf_it_getz(it, &len);
    if (*key != PKGUINF_DESCRIPTION)
        return 0;
    description = n_buf_it_getz(it, &dlen);

    inf = pkguinf_i18n_new(pkgu->_na, summary, description);
    n_hash_insert(pkgu->_ht, lang, inf);

    pkgu_set_recodable(pkgu, PKGUINF_SUMMARY, summary, lang);
    pkgu_set_recodable(pkgu, PKGUINF_DESCRIPTION, description, lang);
    return 1;
}

const char *pkguinf_get(const struct pkguinf *pkgu, int tag)
{
    char **val = NULL;     /* for summary, description recoding */
    unsigned flag = 0, needflag = 0;
    
    switch (tag) {
        case PKGUINF_LICENSE:
            return pkgu->license;

        case PKGUINF_URL:
            return pkgu->url;

        case PKGUINF_VENDOR:
            return pkgu->vendor;

        case PKGUINF_BUILDHOST:
            return pkgu->buildhost;

        case PKGUINF_DISTRO:
            return pkgu->distro;
            
        case PKGUINF_SUMMARY:
            val = (char**)&pkgu->_summary;
            flag = RECODED_SUMMMARY;
            needflag = RECODE_SUMMMARY;
            break;

        case PKGUINF_DESCRIPTION:
            val = (char**)&pkgu->_description;
            flag = RECODED_DESCRIPTION;
            needflag = RECODE_DESCRIPTION;
            break;
            
        default:
            if (poldek_VERBOSE > 2)
                logn(LOGERR, "%d: unknown tag", tag); 
            break;
    }

    if (val) { /* something to recode? */
        char *recoded = NULL;

        /* already recoded or no recoding needed */
        if ((pkgu->_flags & needflag) == 0) 
            return *val;

        recoded = recode(*val, NULL);
        if (recoded && recoded != *val) {
            ((struct pkguinf *)(pkgu))->_flags &= ~needflag; /* XXX ugly */
            ((struct pkguinf *)(pkgu))->_flags |= ~flag;
            *val = recoded;
        }
        
        return *val;
    }

    return NULL;
}

int pkguinf_set(struct pkguinf *pkgu, int tag, const char *val,
                const char *lang)
{
    int len;
    
    len = strlen(val);
    
    switch (tag) {
        case PKGUINF_LICENSE:
            set_member(pkgu, &pkgu->license, val, len);
            break;
            
        case PKGUINF_URL:
            set_member(pkgu, &pkgu->url, val, len);
            break;
                
        case PKGUINF_VENDOR:
            set_member(pkgu, &pkgu->vendor, val, len);
            break;
                
        case PKGUINF_BUILDHOST:
            set_member(pkgu, &pkgu->buildhost, val, len);
            break;

        case PKGUINF_DISTRO:
            set_member(pkgu, &pkgu->distro, val, len);
            break;

        case PKGUINF_SUMMARY:
        case PKGUINF_DESCRIPTION: 
        {
            struct pkguinf_i18n *inf;
            
            if (pkgu->_ht == NULL)
                pkgu->_ht = n_hash_new(3, NULL);

            if (lang == NULL)
                lang = "C";
            
            if ((inf = n_hash_get(pkgu->_ht, lang)) == NULL) {
                inf = pkguinf_i18n_new(pkgu->_na, NULL, NULL);
                n_hash_insert(pkgu->_ht, lang, inf);
            }

            if (tag == PKGUINF_SUMMARY) {
                inf->summary = na_strdup(pkgu->_na, val, len);
                pkgu_set_recodable(pkgu, PKGUINF_SUMMARY, inf->summary, lang);
                
            } else {
                inf->description = na_strdup(pkgu->_na, val, len);
                pkgu_set_recodable(pkgu, PKGUINF_DESCRIPTION, inf->description,
                                   lang);
            }
        }
        
        default:
            if (poldek_VERBOSE > 2)
                logn(LOGERR, "%d: unknown tag", tag);
            return 0;
            break;
    }
    
    return 1;
}

/*
 * DEPRECIATED, LEGACY 
 * pkguinf store/restore as rpm header; functions used by old-poor pdir index
 * format
 */
static Header make_pkguinf_hdr(struct pkguinf *pkgu, int *langs_cnt) 
{
    int                i, nlangs = 0;
    Header             hdr = NULL;
    unsigned           hdr_size;
    tn_array           *langs;
    

    hdr = headerNew();
    if ((langs = pkgu->_langs_rpmhdr) == NULL)
        langs = pkguinf_langs(pkgu);
    
    for (i=0; i < n_array_size(langs); i++) {
        const char *lang = n_array_nth(langs, i);
        struct pkguinf_i18n *inf = n_hash_get(pkgu->_ht, lang);
        
        headerAddI18NString(hdr, RPMTAG_SUMMARY, inf->summary, lang);
        headerAddI18NString(hdr, RPMTAG_DESCRIPTION, inf->description, lang);
    }

    if (pkgu->vendor)
        headerAddEntry(hdr, RPMTAG_VENDOR, RPM_STRING_TYPE, pkgu->vendor, 1);
    
    if (pkgu->license)
        headerAddEntry(hdr, PM_RPMTAG_LICENSE, RPM_STRING_TYPE, pkgu->license, 1);
    
    if (pkgu->url)
        headerAddEntry(hdr, RPMTAG_URL, RPM_STRING_TYPE, pkgu->url, 1);
    
    if (pkgu->distro)
        headerAddEntry(hdr, RPMTAG_DISTRIBUTION, RPM_STRING_TYPE, pkgu->distro, 1);
    
    if (pkgu->buildhost)
        headerAddEntry(hdr, RPMTAG_BUILDHOST, RPM_STRING_TYPE, pkgu->buildhost, 1);

    hdr_size = headerSizeof(hdr, HEADER_MAGIC_NO);
    
    if (hdr_size > UINT16_MAX) {
        logn(LOGERR, "internal: header size too large: %d", hdr_size);
        headerFree(hdr);
        hdr = NULL;
    }

    if (langs_cnt)
        *langs_cnt = nlangs;

    return hdr;
}

int pkguinf_store_rpmhdr(struct pkguinf *pkgu, tn_buf *nbuf) 
{
    Header   hdr = NULL;
    void     *rawhdr;
    int      rawhdr_size;
    
    int rc;


    hdr = make_pkguinf_hdr(pkgu, NULL);
    rawhdr_size = headerSizeof(hdr, HEADER_MAGIC_NO);
    rawhdr = headerUnload(hdr);

#if 0    
    printf("> %ld\t%d\n", ftell(stream), headerSizeof(pkgu->_hdr, HEADER_MAGIC_NO));
    headerDump(pkgu->_hdr, stdout, HEADER_DUMP_INLINE, rpmTagTable);
#endif     

	n_buf_write_int16(nbuf, n_hash_size(pkgu->_ht));
    n_buf_write_int16(nbuf, rawhdr_size);

    
    rc = (n_buf_write(nbuf, rawhdr, rawhdr_size) == rawhdr_size);
    
    free(rawhdr);
    headerFree(hdr);
    
    return rc;
}

struct pkguinf *pkguinf_restore_rpmhdr_st(tn_alloc *na,
                                          tn_stream *st, off_t offset)
{
    uint16_t nsize, nlangs;
    struct pkguinf *pkgu = NULL;
    void *rawhdr;
    Header hdr;

    if (offset > 0)
        if (n_stream_seek(st, offset, SEEK_SET) != 0) {
            logn(LOGERR, "pkguinf_restore: fseek %ld: %m", (long int)offset);
            return NULL;
        }

    
    if (!n_stream_read_uint16(st, &nlangs)) {
        logn(LOGERR, "pkguinf_restore: read error nlangs (%m) at %ld %p",
             n_stream_tell(st), st);
        return NULL;
    }
    
    if (!n_stream_read_uint16(st, &nsize)) {
        logn(LOGERR, "pkguinf_restore: read error nsize (%m) at %ld",
             n_stream_tell(st));
        return NULL;
    }
    
    rawhdr = alloca(nsize);
    
    if (n_stream_read(st, rawhdr, nsize) != nsize) {
        logn(LOGERR, "pkguinf_restore: read %d error at %ld", nsize,
             n_stream_tell(st));
        return NULL;
    }
    
    if ((hdr = headerLoad(rawhdr)) != NULL) {
        pkgu = pkguinf_ldrpmhdr(na, hdr);
        headerFree(hdr); //rpm's memleak
    }

    return pkgu;
}

int pkguinf_skip_rpmhdr(tn_stream *st) 
{
    uint16_t nsize, nlangs;

    n_stream_seek(st, sizeof(nlangs), SEEK_CUR);
    
    if (!n_stream_read_uint16(st, &nsize))
        nsize = 0;
	else
        n_stream_seek(st, nsize, SEEK_CUR);
    
    return nsize;
}
