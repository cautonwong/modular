#include "edge/service.h"
int edge_service_register(edge_service_registry_t*r,const edge_service_t*s){if(!r||!s||!r->items)return -1;return -2;}
const edge_service_t*edge_service_get(const edge_service_registry_t*r,edge_service_id_t id,uint16_t major,uint16_t min_minor){if(!r)return 0;for(size_t i=0;i<r->count;i++){const edge_service_t*s=&r->items[i];if(s->id==id&&s->major==major&&s->minor>=min_minor)return s;}return 0;}
