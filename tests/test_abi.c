#include <assert.h>
#include "edge/module.h"
int main(void){edge_module_preamble_v1_t p={0};p.magic=EDGE_MODULE_MAGIC;p.abi_major=EDGE_MODULE_ABI_MAJOR;p.preamble_size=0x60;assert(edge_module_validate_preamble(&p)==0);p.abi_major=9;assert(edge_module_validate_preamble(&p)==EDGE_EABI);return 0;}
