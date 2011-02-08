/*-
 * Copyright (c) 2008-2010 Linpro AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Persistent storage method
 *
 * XXX: Before we start the client or maybe after it stops, we should give the
 * XXX: stevedores a chance to examine their storage for consistency.
 *
 * XXX: Do we ever free the LRU-lists ?
 */

#ifndef MAP_NOCORE
#define MAP_NOCORE 0 /* XXX Linux */
#endif

#ifndef MAP_NOSYNC
#define MAP_NOSYNC 0 /* XXX Linux */
#endif

#define ASSERT_SILO_THREAD(sc) \
    do {assert(pthread_self() == (sc)->thread);} while (0)

#define OC_F_NEEDFIXUP OC_F_PRIV

/*
 * Context for a signature.
 *
 * A signature is a sequence of bytes in the silo, signed by a SHA256 hash
 * which follows the bytes.
 *
 * The context structure allows us to append to a signature without
 * recalculating the entire SHA256 hash.
 */

struct smp_signctx {
	struct smp_sign		*ss;
	struct SHA256Context	ctx;
	uint32_t		unique;
	const char		*id;
};

struct smp_sc;

/* XXX: name confusion with on-media version ? */
struct smp_seg {
	unsigned		magic;
#define SMP_SEG_MAGIC		0x45c61895

	struct smp_sc		*sc;
	struct lru		*lru;

	VTAILQ_ENTRY(smp_seg)	list;		/* on smp_sc.smp_segments */

	struct smp_segptr	p;

	unsigned		flags;
#define SMP_SEG_MUSTLOAD	(1 << 0)
#define SMP_SEG_LOADED		(1 << 1)

	uint32_t		nobj;		/* Number of objects */
	uint32_t		nalloc;		/* Allocations */
	uint32_t		nfixed;		/* How many fixed objects */

	/* Only for open segment */
	struct smp_object	*objs;		/* objdesc array */
	struct smp_signctx	ctx[1];
};

VTAILQ_HEAD(smp_seghead, smp_seg);

struct smp_sc {
	unsigned		magic;
#define SMP_SC_MAGIC		0x7b73af0a
	struct stevedore	*parent;

	unsigned		flags;
#define SMP_SC_LOADED		(1 << 0)

	const struct stevedore	*stevedore;
	int			fd;
	const char		*filename;
	off_t			mediasize;
	uint64_t		align;		/* 64b to avoid casts */
	uint32_t		granularity;
	uint32_t		unique;

	uint8_t			*base;

	struct smp_ident	*ident;

	struct smp_seghead	segments;
	struct smp_seg		*cur_seg;
	uint64_t		next_bot;	/* next alloc address bottom */
	uint64_t		next_top;	/* next alloc address top */

	uint64_t		free_offset;

	pthread_t		thread;

	VTAILQ_ENTRY(smp_sc)	list;

	struct smp_signctx	idn;
	struct smp_signctx	ban1;
	struct smp_signctx	ban2;
	struct smp_signctx	seg1;
	struct smp_signctx	seg2;

	struct ban		*tailban;

	struct lock		mtx;

	/* Cleaner metrics */

	unsigned		min_nseg;
	unsigned		aim_nseg;
	unsigned		max_nseg;

	uint64_t		min_segl;
	uint64_t		aim_segl;
	uint64_t		max_segl;

	uint64_t		free_reserve;
};

/*--------------------------------------------------------------------*/

/* Generic power-2 rounding */
#define PWR2(x)     ((((x)-1)&(x))==0)		/* Is a power of two */
#define RDN2(x, y)  ((x)&(~((y)-1)))		/* if y is powers of two */
#define RUP2(x, y)  (((x)+((y)-1))&(~((y)-1)))	/* if y is powers of two */

/* Pointer round up/down & assert */
#define PRNDN(sc, x)	((void*)RDN2((uintptr_t)(x), sc->align))
#define PRNUP(sc, x)	((void*)RUP2((uintptr_t)(x), sc->align))
#define PASSERTALIGN(sc, x)	assert(PRNDN(sc, x) == (x))

/* Integer round up/down & assert */
#define IRNDN(sc, x)	RDN2(x, sc->align)
#define IRNUP(sc, x)	RUP2(x, sc->align)
#define IASSERTALIGN(sc, x)	assert(IRNDN(sc, x) == (x))

/*--------------------------------------------------------------------*/

#define ASSERT_PTR_IN_SILO(sc, ptr) \
	assert((const void*)(ptr) >= (const void*)((sc)->base) && \
	    (const void*)(ptr) < (const void *)((sc)->base + (sc)->mediasize))

/*--------------------------------------------------------------------*/

#define SIGN_DATA(ctx)	((void *)((ctx)->ss + 1))
#define SIGN_END(ctx)	((void *)((int8_t *)SIGN_DATA(ctx) + (ctx)->ss->length))