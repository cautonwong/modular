#include "soc_mps2/soc_mps2.h"

/*
 * SoC-level register access lives in the SoC support package, not in a header
 * inline and not in `board/` (D49). This is deliberately the smallest possible
 * source file: the point of the package shape is that `soc/<soc>/` can carry
 * code - register drivers, a vendor HAL, SoC-level init - because a real package
 * always does, and a header-only package would have kept that path untested
 * until the first real SoC arrived (#147).
 */

void soc_mps2_nvic_set_priority(uint32_t irq, uint8_t library_priority) {
    SOC_MPS2_NVIC_IPR[irq] =
        (uint8_t)((uint32_t)library_priority << (8u - SOC_MPS2_NVIC_PRIO_BITS));
}
