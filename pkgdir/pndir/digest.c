/* 
  Copyright (C) 2000 - 2002 Pawel A. Gajda (mis@k2.net.pl)
 
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

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fnmatch.h>
#include <fcntl.h>

#include <openssl/evp.h>
#include <trurl/nassert.h>
#include <trurl/nstr.h>
#include <trurl/nbuf.h>
#include <trurl/nmalloc.h>
#include <trurl/n_snprintf.h>
#include <vfile/vfile.h>

#define PKGDIR_INTERNAL

#include "i18n.h"
#include "log.h"
#include "pndir.h"


const char *pndir_digest_ext = ".md";

static int pndir_digest_read(struct pndir_digest *pdg, struct vfile *vfmd);


struct pndir_digest *pndir_digest_new(const char *path, int vfmode) 
{
    struct pndir_digest  *pdg;
    struct vfile        *vf = NULL; 
    char                mdpath[PATH_MAX];
    
    if (path != NULL) {
        int n;
        const char *ext = pndir_digest_ext;

        n = pndir_mkdigest_path(mdpath, sizeof(mdpath), path, ext);
        vfmode |= VFM_NOEMPTY;
        if ((vf = vfile_open(mdpath, VFT_IO, vfmode)) == NULL) 
            return NULL;
    }
    
    pdg = n_malloc(sizeof(*pdg));
    memset(pdg, 0, sizeof(*pdg));

    pdg->vf = vf;

    if (vf) 
        if (!pndir_digest_read(pdg, NULL)) {
            pndir_digest_free(pdg);
            pdg = NULL;
        }
    
    return pdg;
}


int pndir_digest_fill(struct pndir_digest *pdg, char *mdbuf, int size) 
{
    int req_size;

    n_assert(*pdg->md == '\0');
    
    req_size = TNIDX_DIGEST_SIZE;
    
    if (size < req_size)
        return 0;

    memcpy(pdg->md, mdbuf, TNIDX_DIGEST_SIZE);
    pdg->md[TNIDX_DIGEST_SIZE] = '\0';

    return 1;
}

void pndir_digest_init(struct pndir_digest *pdg) 
{
    memset(pdg, 0, sizeof(*pdg));
    pdg->vf = NULL;
}

    
void pndir_digest_destroy(struct pndir_digest *pdg) 
{
    if (pdg->vf) {
        vfile_close(pdg->vf);
        pdg->vf = NULL;
    }
}



void pndir_digest_free(struct pndir_digest *pdg) 
{
    pndir_digest_destroy(pdg);
    memset(pdg, 0, sizeof(*pdg));
    free(pdg);
}


int pndir_digest_readfd(struct pndir_digest *pdg, int fd, const char *path) 
{
    char buf[TNIDX_DIGEST_SIZE];
    int md_size, req_size;
    
    if (lseek(fd, 0L, SEEK_SET) != 0) {
        logn(LOGERR, "%s: lseek(0): %m", path);
        return 0;
    }
    
    md_size = read(fd, buf, sizeof(buf));

    req_size = TNIDX_DIGEST_SIZE;
    
    if (md_size < req_size) {
        logn(LOGERR, _("%s: broken digest file (%d)"), path, md_size);
        return 0;
    }
    
    return pndir_digest_fill(pdg, buf, md_size);
}


static
int pndir_digest_read(struct pndir_digest *pdg, struct vfile *vfmd) 
{
    if (vfmd == NULL)
        vfmd = pdg->vf;
    
    if (vfmd == NULL)
        return 0;
    
    return pndir_digest_readfd(pdg, vfmd->vf_fd, vfmd->vf_path);
}

int pndir_mkdigest_path(char *path, int size, const char *pathname,
                        const char *ext)
{
    char *p; 
    int n;
    
    n = n_snprintf(path, size, "%s", pathname);
    if ((p = strrchr(n_basenam(path), '.')) == NULL)
        p = &path[n];

    /* don't touch .md[d] files */
    else if (strncmp(p, ".md", 3) == 0) 
        return n;
    
    else if (strcmp(p, ".gz") != 0)
        p = &path[n];
    
    else
        n -= 3;
    
    n += n_snprintf(p, size - (p - path), "%s", ext);
    return n;
}


int pndir_digest_save(struct pndir_digest *pdg, const char *pathname) 
{
    char            path[PATH_MAX];
    struct vfile    *vf;
    int             n;
    

    n = pndir_mkdigest_path(path, sizeof(path), pathname, pndir_digest_ext);
    if (n <= 4) {
        logn(LOGERR, "%s: path too short", path);
        return 0;
    }
    
    if ((vf = vfile_open(path, VFT_STDIO, VFM_RW)) == NULL)
        return 0;
    
    fprintf(vf->vf_stream, "%s", pdg->md);
    vfile_close(vf);
    return 1;
}


int pndir_digest_calc_pkgs(struct pndir_digest *pdg, tn_array *pkgs) 
{
    tn_array *keys;
    char     key[512];
    int      i, klen;

    
    keys = n_array_new(n_array_size(pkgs), free, (tn_fn_cmp)strcmp);
    
    for (i=0; i < n_array_size(pkgs); i++) {
        struct pkg *pkg = n_array_nth(pkgs, i);

        klen = pndir_make_pkgkey(key, sizeof(key), pkg);
        n_array_push(keys, n_strdupl(key, klen));
    }

    i = pndir_digest_calc(pdg, keys);
    n_array_free(keys);
    return i;
}


int pndir_digest_calc(struct pndir_digest *pdg, tn_array *keys)
{
    unsigned char md[256];
    EVP_MD_CTX ctx;
    int i, n, nn = 0;


    EVP_DigestInit(&ctx, EVP_sha1());
    EVP_DigestUpdate(&ctx, "md", strlen("md"));
    
    if (keys && n_array_size(keys)) {
        n_array_sort(keys);
    
        for (i=0; i < n_array_size(keys); i++) {
            char *key = n_array_nth(keys, i);
            DBGF("key = %s\n", key);
            EVP_DigestUpdate(&ctx, key, strlen(key));
        }
    }
    
    EVP_DigestFinal(&ctx, md, &n);

    if (n > (int)sizeof(pdg->md))
        return 0;
    DBGF("digest = %d, %d\n", n, sizeof(pdg->md));
    nn = bin2hex(pdg->md, sizeof(pdg->md), md, n);
    DBGF("digest = %s\n", pdg->md);
    return n;
}



