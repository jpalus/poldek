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

#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <trurl/nassert.h>
#include <vfile/vfile.h>

#include "pkgset.h"
#include "pkgdir/source.h"
#include "log.h"
#include "misc.h"
#include "i18n.h"
#include "depdirs.h"

int pkgset_load(struct pkgset *ps, int ldflags, tn_array *sources)
{
    int i, j, iserr = 0;
    

    n_array_isort_ex(sources, (tn_fn_cmp)source_cmp_pri);
    
    for (i=0; i < n_array_size(sources); i++) {
        struct source *src = n_array_nth(sources, i);
        struct pkgdir *pkgdir = NULL;
        

        if (src->flags & PKGSOURCE_NOAUTO)
            continue;

        if (src->type == NULL)
            source_set_type(src, pkgdir_DEFAULT_TYPE);

        
        pkgdir = pkgdir_srcopen(src, 0);

        /* trying dir */
        if (pkgdir == NULL && !source_is_type(src, "dir") && is_dir(src->path)) {
            logn(LOGNOTICE, _("trying to scan directory %s..."), src->path);
            
            source_set_type(src, "dir");
            pkgdir = pkgdir_srcopen(src, 0);
        }
            
        if (pkgdir == NULL) {
            if (n_array_size(sources) > 1)
                logn(LOGWARN, _("%s: load failed, skipped"),
                     vf_url_slim_s(src->path, 0));
            continue;
        }

        
        n_array_push(ps->pkgdirs, pkgdir);
    }


    /* merge pkgdis depdirs into ps->depdirs */
    for (i=0; i < n_array_size(ps->pkgdirs); i++) {
        struct pkgdir *pkgdir = n_array_nth(ps->pkgdirs, i);
        
        if (pkgdir->depdirs) {
            for (j=0; j < n_array_size(pkgdir->depdirs); j++)
                n_array_push(ps->depdirs, n_array_nth(pkgdir->depdirs, j));
        }
    }

    n_array_sort(ps->depdirs);
    n_array_uniq(ps->depdirs);

    
    for (i=0; i < n_array_size(ps->pkgdirs); i++) {
        struct pkgdir *pkgdir = n_array_nth(ps->pkgdirs, i);

        if ((pkgdir->flags & PKGDIR_LOADED) == 0) {
            if (!pkgdir_load(pkgdir, ps->depdirs, ldflags)) {
                logn(LOGERR, _("%s: load failed"), pkgdir->idxpath);
                iserr = 1;
            }
        }
    }
    
    if (!iserr) {
        /* merge pkgdirs packages into ps->pkgs */
        for (i=0; i < n_array_size(ps->pkgdirs); i++) {
            struct pkgdir *pkgdir = n_array_nth(ps->pkgdirs, i);
            for (j=0; j < n_array_size(pkgdir->pkgs); j++)
                n_array_push(ps->pkgs, pkg_link(n_array_nth(pkgdir->pkgs, j)));
        }

        init_depdirs(ps->depdirs);
    }
    
    if (n_array_size(ps->pkgs)) {
        int n = n_array_size(ps->pkgs);
        msgn(1, ngettext("%d package read",
                         "%d packages read", n), n);
    }
    return n_array_size(ps->pkgs);
}
