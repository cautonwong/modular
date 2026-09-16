#include "edge/module.h"
#define DEMO_ID 0x1001u
#define DEMO_RX 1u
static int demo_init(edge_module_context_t*c){(void)c;return EDGE_OK;}
static int demo_on(edge_module_context_t*c){(void)c;return EDGE_OK;}
static int demo_poll(edge_module_context_t*c){edge_event_t e;while(c->events&&edge_event_pop(c->events,&e)==EDGE_OK){if(e.id==DEMO_RX){}}return EDGE_OK;}
static int demo_off(edge_module_context_t*c){(void)c;return EDGE_OK;}
static int demo_deinit(edge_module_context_t*c){(void)c;return EDGE_OK;}
static const edge_module_preamble_v1_t demo_preamble={EDGE_MODULE_MAGIC,EDGE_MODULE_ABI_MAJOR,EDGE_MODULE_ABI_MINOR,0x60,DEMO_ID,0x00010000u,EDGE_MODULE_FLAG_STATIC|EDGE_MODULE_FLAG_NEEDS_POLL,{0}};
static edge_module_context_t ctx0,ctx1;
static edge_module_descriptor_t demo0={&demo_preamble,1,&ctx0,demo_init,demo_on,demo_poll,demo_off,demo_deinit,0,0,0,0,0,0,0};
static edge_module_descriptor_t demo1={&demo_preamble,2,&ctx1,demo_init,demo_on,demo_poll,demo_off,demo_deinit,0,0,0,0,0,0,0};
EDGE_MODULE_REGISTER(demo0,demo0); EDGE_MODULE_REGISTER(demo1,demo1);
void demo_isr(edge_event_queue_t*q,uint32_t source,uint32_t data){edge_event_t e={DEMO_RX,source,0,data};(void)edge_event_push_isr(q,&e);}
