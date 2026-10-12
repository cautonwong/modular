#ifndef EDGE_MODULES_H
#define EDGE_MODULES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Layer block definitions for module IDs (D55, decentralized).
 *
 * App/infra/sys/board modules declare their own `*_MODULE_ID` macros
 * in their own public domain headers. They must sit on a 0xNN00 segment
 * within their layer's assigned block.
 *
 *   0x1000 - 0x5FFF   app modules (protocol, domain, actuation)
 *   0x6000 - 0x6FFF   infra device drivers
 *   0x7000 - 0x7FFF   sys / runner
 *   0x8000 - 0x8FFF   board and soc
 *   0x0000, 0x9000 - 0xFFFF   reserved: not allocatable
 */
#define EDGE_MODULE_BLOCK_app_LO 0x1000u
#define EDGE_MODULE_BLOCK_app_HI 0x5FFFu
#define EDGE_MODULE_BLOCK_infra_LO 0x6000u
#define EDGE_MODULE_BLOCK_infra_HI 0x6FFFu
#define EDGE_MODULE_BLOCK_sys_LO 0x7000u
#define EDGE_MODULE_BLOCK_sys_HI 0x7FFFu
#define EDGE_MODULE_BLOCK_board_LO 0x8000u
#define EDGE_MODULE_BLOCK_board_HI 0x8FFFu

/* High byte of a module/event segment, used by EDGE_ERR() in edge/errors.h. */
#define EDGE_MODULE_SEGMENT(id) (((uint32_t)(id)) & 0xFF00u)

#ifdef __cplusplus
}
#endif

#endif
