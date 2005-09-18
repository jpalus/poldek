/*
  $Id$
*/
/*
  Copyright (C) 2000 - 2004 Pawel A. Gajda <mis@pld.org.pl>

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
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

int poldek_util_parse_evr(char *evrstr, int32_t *epoch, const char **version,
                          const char **release)
{
    char *p;

    while (isspace(*evrstr))
        evrstr++;
    
    if (*evrstr == '\0')
        return 0;
    
    if ((p = strchr(evrstr, ':')) == NULL) {
        *epoch = 0;
        
    } else {
        *p = '\0';
        
        if (*evrstr == '\0') {
            *epoch = 0;
            
        } else {
            *epoch = (int32_t)strtol(evrstr, (char **)NULL, 10);
            if (*epoch == 0 && (*evrstr != '0' || *(evrstr + 1) != '\0'))
                return 0;
        }

        evrstr = p+1;
        if (*evrstr == '\0')   /* empty version strig */
            return 0;
    }

    if ((p = strchr(evrstr, '-')) == NULL) {
        *version = evrstr;
        *release = NULL;
        
    } else {
        *p = '\0';
        *version = evrstr;
        *release = p+1;

        if (**version == '\0' || **release == '\0')
            return 0;
    }
    
    return 1;
}


int poldek_util_parse_nevr(char *nevrstr, const char **name, int32_t *epoch,
                           const char **version, const char **release)
{
    char *p, *q;

    while (isspace(*nevrstr))
        nevrstr++;
    
    if (*nevrstr == '\0')
        return 0;
    
    if ((p = strrchr(nevrstr, '-')) == NULL) 
        return 0;
    
    q = p;
    *q = '\0';
    
    if ((p = strrchr(nevrstr, '-')) == NULL) 
        return 0;
    
    *q = '-';
    *p = '\0';
    p++;
    *name = nevrstr;
    
    return poldek_util_parse_evr(p, epoch, version, release);
}

