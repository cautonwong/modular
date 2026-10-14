#!/bin/sh
# A stand-in for arm-none-eabi-objdump, for check_fast_loop_budget.py's own self-test. It ignores
# its arguments and prints a disassembly of two functions - the root and the one it calls - so both
# the pass and the fail arm can be pinned without an ARM build in the fixture.
cat <<'EOF'
08000000 <foc_core_fast_loop>:
 8000000:	push	{r4, lr}
 8000002:	bl	08000010 <foc_observer_update>
 8000006:	pop	{r4, pc}

08000010 <foc_observer_update>:
 8000010:	movs	r0, #0
 8000012:	bx	lr
EOF
