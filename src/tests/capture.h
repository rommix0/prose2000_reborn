/* Reader for the RAM-snapshot captures ("PGR1" records) written by the scratch capture harness on native/. */
#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	uint32_t func;
	uint16_t ss, sp0, stack[8], ax;
	uint8_t far;
	uint8_t before[0x3000], after[0x3000];
} record;

static inline int read_record(FILE *f, record *r)
{
	char magic[4];
	uint32_t n;
	if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "PGR1", 4))
		return 0;
	if (fread(&r->func, 4, 1, f) != 1 || fread(&r->ss, 2, 1, f) != 1 || fread(&r->sp0, 2, 1, f) != 1 ||
	    fread(r->stack, 2, 8, f) != 8 || fread(r->before, 1, 0x3000, f) != 0x3000 || fread(&r->ax, 2, 1, f) != 1 ||
	    fread(&r->far, 1, 1, f) != 1 || fread(&n, 4, 1, f) != 1)
		return 0;
	memcpy(r->after, r->before, 0x3000);
	while (n--) {
		uint16_t a;
		uint8_t v;
		if (fread(&a, 2, 1, f) != 1 || fread(&v, 1, 1, f) != 1 || a >= 0x3000)
			return 0;
		r->after[a] = v;
	}
	return 1;
}

#endif
