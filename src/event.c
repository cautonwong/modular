#include "edge/event.h"
#include "edge/module.h"
int edge_event_queue_init(edge_event_queue_t*q,edge_event_t*items,uint32_t cap){if(!q||!items||cap<2)return EDGE_EINVAL;q->items=items;q->capacity=cap;q->head=q->tail=0;return EDGE_OK;}
int edge_event_push_isr(edge_event_queue_t*q,const edge_event_t*e){uint32_t h=q->head,n=(h+1u)%q->capacity;if(n==q->tail)return EDGE_EOVERFLOW;q->items[h]=*e;q->head=n;return EDGE_OK;}
int edge_event_pop(edge_event_queue_t*q,edge_event_t*e){uint32_t t=q->tail;if(t==q->head)return EDGE_ENOENT;*e=q->items[t];q->tail=(t+1u)%q->capacity;return EDGE_OK;}
size_t edge_event_count(const edge_event_queue_t*q){return(q->head+q->capacity-q->tail)%q->capacity;}
