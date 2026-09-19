# Mirrors the CI runner image (see ci/toolchain.json and docs/toolchain.md).
#
# The point is not a rich shell; it is that `cmake && ctest`, the cross builds and
# every guard behave locally the way they behave in CI, with the same major tool
# versions.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake ninja-build pkg-config ccache \
        libcmocka-dev \
        clang clang-tidy clang-format cppcheck \
        gcc-arm-none-eabi binutils-arm-none-eabi libnewlib-arm-none-eabi \
        gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf \
        qemu-system-arm qemu-system-misc \
        git ca-certificates python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work
