#include "edge/resource.h"
#include "edge/module.h"
int edge_resource_claim(edge_resource_t*r,uint32_t owner){if(!r||!owner)return EDGE_EINVAL;if(r->owner&&r->owner!=owner)return EDGE_EBUSY;r->owner=owner;return EDGE_OK;}
int edge_resource_release(edge_resource_t*r,uint32_t owner){if(!r||r->owner!=owner)return EDGE_EINVAL;r->owner=0;return EDGE_OK;}
int edge_resource_control(edge_resource_t*r,uint32_t op,void*arg){if(!r||!r->ops||!r->ops->control)return EDGE_ENOTSUP;return r->ops->control(r->impl,op,arg);}
