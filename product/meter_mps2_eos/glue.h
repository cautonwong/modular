#ifndef PRODUCT_METER_MPS2_EOS_GLUE_H
#define PRODUCT_METER_MPS2_EOS_GLUE_H

#include "dlt645/dlt645.h"

void product_meter_mps2_eos_make_storage(dlt645_storage_if_t *out, void *flash_state);

#endif
