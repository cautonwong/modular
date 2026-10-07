#!/usr/bin/env python3
"""Emit infra/imu/include/imu/mpu9150.h from the reference's own register map.

The map is data - several hundred lines of MPU9150_* definitions that this port is in no position to
improve on - so it is copied verbatim rather than transcribed, comment lines and all. Only the
header's own scaffolding is written here, and the one line the reference includes for its device type
is replaced by this port's own.

Usage: python3 tools/gen_imu_mpu9150_from_reference.py
"""

from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REFERENCE = Path("/workspaces/vendor/bldc/imu/mpu9150.h")
OUT = REPO / "infra/imu/include/imu/mpu9150.h"

HEADER = """#ifndef MPU9150_H
#define MPU9150_H

/*
 * The register map below is the reference's own, emitted verbatim by
 * tools/gen_imu_mpu9150_from_reference.py. This driver also works for the MPU9250, which is what the
 * reference's own note at the top of that file says.
 */

#include "imu/device.h"

#include <stdbool.h>
#include <stdint.h>

/*
 * applications/imu/mpu9150.c keeps its decoder state in a file-scope static, which this port does not:
 * the caller provides it, as every other driver here does. The fields are the reference's own.
 */
typedef struct mpu9150_state {
    int16_t prev_raw[6]; /* the previous accelerometer and gyroscope sample, to spot a stuck sensor */
    uint32_t identical_reads;
    uint8_t mag_cnt; /* the magnetometer's decimation counter */
    int16_t mag_raw[3]; /* its last reading, reused between refreshes */
    bool use_magnetometer;
} mpu9150_state_t;

imu_device_t mpu9150_device(imu_transport_t *transport, mpu9150_state_t *state);

"""

FOOTER = "\n#endif /* MPU9150_H */\n"


def main() -> int:
    lines = REFERENCE.read_text().splitlines()
    block = [
        line
        for line in lines
        if line.startswith("#define MPU9150_") or line.lstrip().startswith("//#")
    ]
    if not block:
        raise SystemExit("no MPU9150 definitions found in the reference")
    OUT.write_text(HEADER + "\n".join(block) + "\n" + FOOTER)
    print(f"wrote {OUT} with {len(block)} definitions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
