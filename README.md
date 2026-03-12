# YAFFS for ESP-IDF

This repository contains two things:

- an ESP-IDF component for the YAFFS filesystem in [`components/yaffs`](components/yaffs)
- a sample ESP32 application in [`main`](main) that mounts YAFFS on a Winbond W25N04KV SPI NAND device

## Current Status

The project is already in a working state for the current reference setup:

- ESP-IDF target: `esp32`
- ESP-IDF build used here: `v5.5-dev-2881-ga25e7ab59e`
- NAND device: `W25N04KV` (4 Gbit / 512 MB SPI NAND)
- YAFFS mount, format, file create, file read, and directory listing are implemented in the sample app

The reusable YAFFS code lives in the component. The hardware-specific NAND driver remains in the sample application.

## Repository Layout

```text
.
|- components/yaffs/      YAFFS component for ESP-IDF
|- main/                  Sample application and W25N driver
|- datasheets/            Hardware reference material
`- sdkconfig.defaults     Sample configuration defaults
```

### Component

[`components/yaffs`](components/yaffs) contains:

- YAFFS core sources
- ESP-IDF / FreeRTOS OS glue
- public headers
- an example W25N driver under `components/yaffs/examples/`

### Sample Application

[`main`](main) contains:

- [`main.c`](main/main.c): mount / format / read / write smoke test
- [`w25n_yaffs_drv.c`](main/w25n_yaffs_drv.c): W25N04KV driver for YAFFS
- [`w25n_yaffs_drv.h`](main/w25n_yaffs_drv.h): driver interface

The sample app is deliberately simple. It demonstrates the minimum sequence needed to:

1. initialize the YAFFS OS layer
2. create a `yaffs_dev`
3. mount the filesystem
4. format on first use if needed
5. perform file and directory operations

## Hardware Assumptions

The current sample setup is an ESP32-S3 connected to Winbond W25N04KV devices on a shared SPI bus.

Current GPIO mapping:

| ESP32-S3 GPIO | W25N04KV Signal     |
|---------------|---------------------|
| 11            | D0 / MOSI / DI      |
| 13            | D1 / MISO / DO      |
| 14            | D2 / WP             |
| 9             | D3 / HOLD           |
| 12            | CLK / SCLK          |
| 10            | CS0 (chip 0 select) |
| 15            | CS1 (chip 1 select) |

Notes:

- `D0..D3` follow the usual SPI NAND naming where `D2` is `WP` and `D3` is `HOLD`.
- `CS0` selects the first NAND device and `CS1` selects the second NAND device.
- The sample driver uses half-duplex SPI mode.
- The sample assumes the W25N internal ECC is enabled and used by YAFFS.
- The main hardware architecture is the ESP32 plus external SPI NAND reference design. If the hardware changes materially, the driver should be reviewed.

## Building the Sample

Prerequisites:

- ESP-IDF installed and exported in your shell
- an ESP32 board wired to the reference SPI NAND design

Build:

```bash
idf.py build
```

Flash and monitor:

```bash
idf.py flash monitor
```

The sample uses [`sdkconfig.defaults`](sdkconfig.defaults), including:

```text
CONFIG_ESP_MAIN_TASK_STACK_SIZE=16384
```

The larger main task stack is intentional. YAFFS operations and the current sample flow need more stack than the ESP-IDF default.

## Expected Sample Flow

On boot the sample application:

1. Initializes YAFFS OS support
2. Creates YAFFS mounts for both flash chips
3. Tests file read, write, copy and runs a quick benchmark
4. calls `yaffs_sync()`

Successful output includes total space and free space reports for the W25N04KV device, plus read and write speeds

## Using the Component in Another Project

- keep `components/yaffs` as the reusable ESP-IDF component
- keep board-specific NAND drivers outside the component

The current W25N04KV driver should be treated as a reference implementation, not as a generic SPI NAND layer.

## Driver Responsibilities

YAFFS expects the hardware driver to provide functions for:

- chunk read
- chunk write
- block erase
- bad block mark / check
- device initialization
- device deinitialization

The current sample implements those hooks in [`main/w25n_yaffs_drv.c`](main/w25n_yaffs_drv.c).

## Limitations of the Current Sample

- the sample NAND driver is hard-coded for one flash family and one GPIO assignment

## Licensing

YAFFS is dual-licensed:

- GPL v2 for open source use
- commercial licensing from Aleph One Limited for closed-source or commercial distribution

See the YAFFS project site for licensing details:

- <https://yaffs.net/>
- <https://yaffs.net/documents/>

