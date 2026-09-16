#include <assert.h>
#include "edge/event.h"
int main(void){edge_event_t a[4],e={1,2,3,4},o;edge_event_queue_t q;assert(edge_event_queue_init(&q,a,4)==0);assert(edge_event_push_isr(&q,&e)==0);assert(edge_event_pop(&q,&o)==0);assert(o.id==1);return 0;}
