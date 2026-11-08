#include "config.h"
#include <stdint.h>
#include <ppu-types.h>

// These are firmware import-table pointers, not native ELFv1 pointers.
typedef const void *prx_pointer ATTRIBUTE_PRXPTR;
typedef const char *prx_string ATTRIBUTE_PRXPTR;

typedef struct {
	uint32_t header1;
	uint16_t header2;
	uint16_t imports;
	uint32_t zero1;
	uint32_t zero2;
	prx_string name;
	prx_pointer fnid;
	prx_pointer fstub;
	uint32_t zero3;
	uint32_t zero4;
	uint32_t zero5;
	uint32_t zero6;
} __attribute__((__packed__)) prx_header;
_Static_assert(sizeof(prx_pointer)==4 && sizeof(prx_header)==44,"PRX import wire ABI");

extern uint32_t LIBRARY_SYMBOL __attribute__((section(".rodata.sceFNID")));
static prx_pointer scefstub[0] __attribute__((used, section(".data.sceFStub." LIBRARY_NAME)));

static const uint32_t version __attribute__((used, section(".rodata.sceResident"))) = 0;
static const char name[] __attribute__((used, section(".rodata.sceResident"))) = LIBRARY_NAME;

static prx_header header __attribute__((used, section(".lib.stub"))) = {
	LIBRARY_HEADER_1,
	LIBRARY_HEADER_2,
	0,
	0,
	0,
	name,
	&LIBRARY_SYMBOL,
	scefstub,
	0,
	0,
	0,
	0
};

#define EXPORT(name, fnid) \
	extern void* __##name; \
	prx_pointer name##_stub __attribute__((used, section(".data.sceFStub." LIBRARY_NAME))) = &__##name; \
	const uint32_t name##_fnid __attribute__((used, section(".rodata.sceFNID"))) = fnid

// duplicate the macro impl, as i don't know whether macro substitution works properly, with strigify, or not.
#define EXPORT_VA(name, fnid, argc) \
	extern void* __##name; \
	prx_pointer name##_stub __attribute__((used, section(".data.sceFStub." LIBRARY_NAME))) = &__##name; \
	const uint32_t name##_fnid __attribute__((used, section(".rodata.sceFNID"))) = fnid

#include "exports.h"
