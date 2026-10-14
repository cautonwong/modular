#!/usr/bin/env python3
"""Emit this port's IMU register maps from the reference's own headers.

A register map is data - hundreds of definitions that this port is in no position to improve on - so
each one is copied verbatim rather than transcribed. Comment lines among them are kept as they are,
because they are where the reference explains its own oddities. Only each header's own scaffolding is
written here, and the reference's device type is replaced by this port's own.

Usage: python3 tools/gen_imu_register_maps.py
"""

from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REFERENCE_DIR = Path("/workspaces/vendor/bldc/imu")

# Each entry: the reference header, where this port's comes from, its guard, the definitions to take,
# and whatever scaffolding that header needs around them.
SENSORS = [
    {
        "ref": "mpu9150.h",
        "out": "infra/imu/include/imu/mpu9150.h",
        "guard": "MPU9150_H",
        "prefix": "#define MPU9150_",
        "note": (
            " * The reference's own note at the top of its file says this driver also works for the\n"
            " * MPU9250, which is why the register map is the MPU9150's.\n"
        ),
        "scaffold": """/*
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
""",
    },
    {
        "ref": "icm20948.h",
        "out": "infra/imu/include/imu/icm20948.h",
        "guard": "ICM20948_H",
        "prefix": "#define ICM20948_",
        "note": "",
        "scaffold": """imu_device_t icm20948_device(imu_transport_t *transport);
""",
    },
    {
        "ref": "lsm6ds3.h",
        "out": "infra/imu/include/imu/lsm6ds3.h",
        "guard": "LSM6DS3_H",
        "prefix": "#define LSM6DS3",
        "note": "",
        "scaffold": """imu_device_t lsm6ds3_device(imu_transport_t *transport);
""",
    },
]


def emit(sensor: dict) -> str:
    """Copy the reference's map whole, from its first line to the end of its file.

    Some maps are all #define lines and some keep their values in enumerations; the region rule takes
    both, along with the comments the reference wrote between them, and leaves out only the file's own
    guard and includes.
    """
    lines = (REFERENCE_DIR / sensor["ref"]).read_text().splitlines()
    start = next(
        (i for i, line in enumerate(lines) if line.startswith(sensor["prefix"])),
        None,
    )
    if start is None:
        raise SystemExit(f"no map found in {sensor['ref']}")
    block = [
        line
        for line in lines[start:]
        if not line.startswith("#include") and not line.startswith("imu_device_t ")
    ]
    while block and not block[-1].strip():
        block.pop()
    if block and block[-1].startswith("#endif"):
        block.pop()
    while block and not block[-1].strip():
        block.pop()
    if not block:
        raise SystemExit(f"empty map in {sensor['ref']}")
    head = (
        f"#ifndef {sensor['guard']}\n"
        f"#define {sensor['guard']}\n\n"
        f"/*\n"
        f" * The register map below is the reference's own, emitted verbatim by\n"
        f" * tools/gen_imu_register_maps.py.\n"
        f"{sensor['note']}"
        f" */\n\n"
        f'#include "imu/device.h"\n\n'
        f"#include <stdbool.h>\n"
        f"#include <stdint.h>\n\n"
        f"{sensor['scaffold']}\n"
    )
    out = REPO / sensor["out"]
    out.write_text(head + "\n".join(block) + f"\n#endif /* {sensor['guard']} */\n")
    return f"{out.relative_to(REPO)}: {len(block)} definitions"


def main() -> int:
    for sensor in SENSORS:
        print(emit(sensor))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
