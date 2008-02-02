/* $Id$ */
#ifndef POLDEK_CAPREQ_H
#define POLDEK_CAPREQ_H

#ifdef HAVE_CONFIG_H            /* for inline */
# include "config.h"
#endif

#include <stdint.h>

#include <trurl/narray.h>
#include <trurl/nbuf.h>

#define REL_EQ	    (1 << 0)
#define REL_GT	    (1 << 1)
#define REL_LT	    (1 << 2)
#define REL_ALL     (REL_EQ | REL_GT | REL_LT)

#if 0  /* unused */
/* types */
#define CAPREQ_PROV     (1 << 0)
#define CAPREQ_REQ      (1 << 1)
#endif /* end unused */
#define CAPREQ_CNFL     (1 << 2)

/* sub types */
#define CAPREQ_PREREQ      (1 << 3)        /* Requires(pre) */
#define CAPREQ_PREREQ_UN   (1 << 4)        /* Requires(un)  */
#define CAPREQ_OBCNFL      CAPREQ_PREREQ   /* Obsoletes     */

#define CAPREQ_RPMLIB      (1 << 5)  /* rpmlib(...) */

#define CAPREQ_ISDIR       (1 << 6)  /* */
#define CAPREQ_BASTARD     (1 << 7)  /* capreq added by poldek */

/* 'runtime' i.e. not storable flags  */
#define CAPREQ_RT_FLAGS    (CAPREQ_ISDIR | CAPREQ_BASTARD)

struct capreq {
    uint8_t  cr_flags;
    uint8_t  cr_relflags;
	/* XXX: Ignore warning (Setting a const char * variable may leak memory). */
    const char *name;
    uint8_t  cr_ep_ofs;
    uint8_t  cr_ver_ofs;         /* 0 if capreq hasn't version */
    uint8_t  cr_rel_ofs;         /* 0 if capreq hasn't release */
    char    _buff[0];            /* for evr, first byte is always '\0' */
};

/* CAUTION: side effects! */
#define capreq_name(cr)     (cr)->name

#ifdef SWIG
# define extern__inline
#else
# define extern__inline extern inline
#endif

extern__inline int32_t capreq_epoch_(const struct capreq *cr);

#define capreq_epoch(cr) \
    ((cr)->cr_ep_ofs ? capreq_epoch_(cr) : 0)

#define capreq_ver(cr)  (&(cr)->_buff[(cr)->cr_ver_ofs])
#define capreq_rel(cr)  (&(cr)->_buff[(cr)->cr_rel_ofs])

#define capreq_has_epoch(cr)    (cr)->cr_ep_ofs   
#define capreq_has_ver(cr)      (cr)->cr_ver_ofs
#define capreq_has_rel(cr)      (cr)->cr_rel_ofs
#define capreq_versioned(cr)    ((cr)->cr_relflags & (REL_ALL))

#define capreq_is_cnfl(cr)      ((cr)->cr_flags & CAPREQ_CNFL)
#define capreq_is_prereq(cr)    ((cr)->cr_flags & CAPREQ_PREREQ)
#define capreq_is_prereq_un(cr) ((cr)->cr_flags & CAPREQ_PREREQ_UN)
#define capreq_is_obsl(cr)        capreq_is_prereq((cr))

#define capreq_is_file(cr)        (*(cr)->name == '/')
#define capreq_isnot_file(cr)     (*(cr)->name != '/')

#define capreq_isdir(cr)        ((cr)->cr_flags & CAPREQ_ISDIR)
#define capreq_set_isdir(cr)    ((cr)->cr_flags |= CAPREQ_ISDIR)

#define capreq_is_bastard(cr)     ((cr)->cr_flags & CAPREQ_BASTARD)
#define capreq_is_autodirreq(cr)  (capreq_is_bastard(cr) && capreq_is_file(cr))

#define capreq_is_rpmlib(cr)     ((cr)->cr_flags & CAPREQ_RPMLIB)

#define capreq_revrel(cr) ((cr)->cr_relflags = (cr)->cr_relflags ? \
                          (((uint8_t)~cnfl->cr_relflags) & REL_ALL) : (cr)->cr_relflags)

struct capreq *capreq_new_evr(tn_alloc *na, const char *name, char *evr,
                              int32_t relflags, int32_t flags);

struct capreq *capreq_new(tn_alloc *na, const char *name, int32_t epoch,
                          const char *version, const char *release,
                          int32_t relflags, int32_t flags);
#ifndef SWIG
const char *capreq__alloc_name(const char *name);
#endif

#define capreq_new_name_a(nam, crptr)                              \
    {                                                              \
        struct capreq *__cr;                                       \
        const char *name = capreq__alloc_name(nam);                \
        __cr = alloca(sizeof(*__cr) + 2);                          \
        __cr->cr_flags = __cr->cr_relflags = 0;                    \
        __cr->cr_ep_ofs = __cr->cr_ver_ofs = __cr->cr_rel_ofs = 0; \
        __cr->_buff[0] = '\0';                                     \
        __cr->name = name;                                         \
        crptr = __cr;                                              \
    }

void capreq_free_na(tn_alloc *na, struct capreq *cr);
void capreq_free(struct capreq *cr);

struct capreq *capreq_clone(tn_alloc *na, const struct capreq *cr);

int capreq_strcmp_evr(const struct capreq *pr1, const struct capreq *pr2);
int capreq_strcmp_name_evr(const struct capreq *pr1, const struct capreq *pr2);

int capreq_cmp_name(const struct capreq *cr1, const struct capreq *cr2);
int capreq_cmp_name_evr(const struct capreq *cr1, const struct capreq *cr2);

#ifndef SWIG
tn_array *capreq_arr_new(int size);
int capreq_arr_find(tn_array *capreqs, const char *name); /* returns index */
int capreq_arr_contains(tn_array *capreqs, const char *name); /* returns bool */
tn_buf *capreq_arr_join(tn_array *capreqs, tn_buf *nbuf, const char *sep);

int capreq_arr_store_n(tn_array *arr);
tn_buf *capreq_arr_store(tn_array *arr, tn_buf *nbuf, int n);

tn_array *capreq_arr_restore(tn_alloc *na, tn_buf *nbuf);
tn_array *capreq_arr_restore_st(tn_alloc *na, tn_stream *st);
#endif

int capreq_snprintf(char *str, size_t size, const struct capreq *cr);
char *capreq_snprintf_s(const struct capreq *cr);
char *capreq_snprintf_s0(const struct capreq *cr);

char *capreq_str(char *str, size_t size, const struct capreq *cr);

/* const char *capreq_stra(struct capreq) */
#define capreq_stra(c) \
    (capreq_versioned((c))? capreq_str(alloca(256), 256, (c)): capreq_name((c)))


#endif /* POLDEK_CAPREQ_H */
