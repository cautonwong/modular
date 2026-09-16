#ifndef EDGE_MODULE_ABI_H
#define EDGE_MODULE_ABI_H
#include <stdint.h>
#define EDGE_MODULE_MAGIC 0x4544474Du
#define EDGE_MODULE_ABI_MAJOR 1u
#define EDGE_MODULE_ABI_MINOR 0u
enum { EDGE_MODULE_FLAG_STATIC=1u<<0, EDGE_MODULE_FLAG_LOADABLE=1u<<1, EDGE_MODULE_FLAG_NEEDS_POLL=1u<<2, EDGE_MODULE_FLAG_NO_INIT=1u<<3 };
typedef struct { uint32_t magic; uint16_t abi_major; uint16_t abi_minor; uint32_t preamble_size; uint32_t module_id; uint32_t module_version; uint32_t flags; uint32_t init_offset; uint32_t power_on_offset; uint32_t poll_offset; uint32_t power_off_offset; uint32_t deinit_offset; uint32_t suspend_offset; uint32_t resume_offset; uint32_t context_size; uint32_t stack_size; uint32_t dependency_count; uint32_t dependency_offset; uint32_t resource_count; uint32_t resource_offset; uint32_t event_count; uint32_t event_offset; uint32_t reserved[3]; } edge_module_preamble_v1_t;
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(edge_module_preamble_v1_t)==0x60,"Edge module ABI v1 preamble must be 0x60 bytes");
#endif
#endif
