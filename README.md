# ch32_dfu_boot

[English](README.md) | [简体中文](README.zh-CN.md)

A **CH32 USB DFU bootloader (DfuSe)** built on **ch32fun** + **CherryUSB**, with no
vendor library.  Two chip ports are supported today:

| Chip port | USB controller | Board |
| --- | --- | --- |
| `port/ch32v30x` | USBHS, high speed | `boards/ch32v30x_ob` |
| `port/ch32l103` | USBFS, full speed | `boards/ch32l103_candle` (the USB-CANFD adapter of `candleLight_fw_ch32_port`) |
| `port/ch32l103` | USBFS, full speed | `boards/ch32l103_bulepill` (bluepill-shaped CH32L103C8T6 board, USB-C on PA11/PA12) |

Companion **application images** link at `0x00003000` (the application partition) and
come back here with `dfu-util -e` through their DFU runtime interface, or by holding
the BOOT button on boards that have one.

---

## Design goals and layering

The bootloader has to serve **several boards and, later, several CH32 chips**, so it
is decoupled from the start:

| Layer | Directory | Responsibility | New board | New chip |
| --- | --- | --- | --- | --- |
| Contract | `shared/` | partition layout, hand-shake (SDK independent) | unchanged | unchanged |
| Board | `boards/<board>/` (`board_config.h` + `board.h` + `board.c`) | clock/UART/button/LED primitives + tick | **one directory** | unchanged |
| Chip port | `port/<chip>/` | flash erase/write, cross-reset hand-shake, USB low level | unchanged | **one directory** |
| Firmware | `user/` | boot decision, DFU descriptors, DfuSe adaption | unchanged | unchanged |
| Library | `third_party_components/ch32fun` (submodule) | startup, clock tree, printf, register definitions | — | — |
| USB stack | `third_party_components/CherryUSB` (submodule) | device stack + DFU class | — | — |

New board: copy a directory under `boards/` and edit `board_config.h` (`BOARD_NAME` +
the `BOARD_*` pins/features). A board directory is **self-contained** - `board_config.h`,
`board.h` (primitives) and `board.c` (primitives + tick ISR) - so a copied directory is
reusable as is; `board.cmake` is optional for extra compile definitions. Select it with
`-DBOARD=<name>`. Nothing outside the folder has to change for a new board: the
crystal and the clock tree live in `boards/<board>/board.cmake`, the pins and the
peripheral instances in `board_config.h`.
New chip: add a directory under `port/` and select it with `-DCHIP_PORT=<name>`.
A chip port is the only place that knows the part: its `port.cmake` sets the ISA
string (CH32V30x `rv32imacxw`, CH32L103 `rv32imac`), the chip define ch32fun keys
off, the USB controller source and speed (`CONFIG_USB_HS` or not), the ESIG
unique-ID address and the linker script; the three `boot_*_<chip>.c` files
implement the flash, hand-shake and USB low-level contracts.  Nothing in `user/`
or `shared/` names a chip.

---

## Layout

```
ch32_dfu_boot/
├── CMakeLists.txt
├── cmake/riscv.cmake                 # riscv-wch-elf- toolchain + flags
├── shared/boot_protocol.h            # partition + BKP hand-shake (shared with the app)
├── third_party_components/
│   ├── CherryUSB/                    # git submodule: our fork, branch ch32v30x-usbhs
│   └── ch32fun/                      # git submodule: cnlohr/ch32fun, pinned
├── boards/
│   ├── ch32v30x_ob/                  # board BSP (self-contained, copy to reuse)
│   │   ├── board_config.h            # BOARD_* macros: BOOT button PA6 + LED PA5
│   │   ├── board.h                   # board primitive interface
│   │   ├── board.c                   # board primitives + the periodic tick ISR
│   │   └── board.cmake               # crystal + clock tree
│   ├── ch32l103_candle/              # CH32L103 USB-CANFD adapter
│   │   ├── board_config.h            # LED1 PA0 / LED2 PA1 / termination PA2, BOOT switch is BOOT0
│   │   ├── board.h
│   │   ├── board.c
│   │   └── board.cmake               # 8 MHz HSE x12 = 96 MHz + BOOT_FLASH_SIZE=64K
│   └── ch32l103_bulepill/            # bluepill-shaped CH32L103C8T6 board
│       ├── board_config.h            # LEDs PB11/PB10/PB2 (active high), BOOT switch is BOOT0
│       ├── board.h
│       ├── board.c
│       └── board.cmake               # 8 MHz HSE x12 = 96 MHz + BOOT_FLASH_SIZE=64K
├── port/
│   ├── boot_flash_port.h             # flash abstraction
│   ├── boot_trigger_port.h           # cross-reset hand-shake interface
│   ├── boot_usb_port.h
│   ├── ch32v30x/
│   │   ├── port.cmake                # rv32imacxw, CONFIG_USB_HS, USBHS driver, Link.ld
│   │   ├── boot_flash_ch32v30x.c     # fast page erase/write
│   │   ├── boot_trigger_ch32v30x.c   # BKP trigger
│   │   ├── boot_usb_ch32v30x.c       # USBHS clock + usb_dc_low_level_*
│   │   └── Link.ld                   # 0x00000000 + 12K, 32K RAM
│   │                                 # (the USBHS device driver lives in the
│   │                                 #  submodule, port/wch/ch32v30x/)
│   └── ch32l103/
│       ├── port.cmake                # rv32imac, USBFS driver, Link.ld
│       ├── boot_flash_ch32l103.c     # buffered fast page erase/write (256 B)
│       ├── boot_trigger_ch32l103.c   # BKP trigger
│       ├── boot_usb_ch32l103.c       # USBFS clock + usb_dc_low_level_* + vector
│       └── Link.ld                   # 0x00000000 + 12K, 20K RAM
│                                     # (the USBFS driver is CherryUSB's own,
│                                     #  port/wch/usbfs/)
└── user/
    ├── main.c                        # top level flow
    ├── boot_entry.c/.h               # boot decision + jump
    ├── dfu_desc.c                    # DFU descriptors + USB init
    ├── dfu_port.c/.h                 # DfuSe -> boot_flash_port adaption
    ├── funconfig.h                   # ch32fun configuration (printf, size knobs)
    └── boot_log.h / usb_config.h
```

---

## Boot flow

`boot_check_and_run_app()` (`user/boot_entry.c`) decides in this order:

1. **BOOT button on a GPIO** (board has one and it is held) → stay in the bootloader
   (and clear a stale trigger flag)
2. **BKP trigger flag** (written by the application through
   `boot_trigger_reboot_to_boot()`) → stay in the bootloader
3. **Application validity**: the first word at `0x00003000` is a JAL
   (`& 0x7F == 0x6F`) → jump to the application

If none of them applies, the chip port's USB device controller is initialised
(`boot_usb_reg_base()`: USBHS on the CH32V30x, USBFS on the CH32L103) and the device
waits for the host in DfuSe mode.

- Step 1 needs a button on a **GPIO**.  Both L103 boards have a BOOT switch, but it
  is wired to **BOOT0 (pin 44)**, a dedicated boot-mode input the chip samples in
  hardware reset and firmware cannot read - so for this bootloader they are
  button-less, and DFU is entered by an application detach (`dfu-util -e`) or
  automatically when the application image is invalid.  Holding that switch while
  resetting selects WCH's system-memory ISP loader instead (that is how this
  bootloader gets flashed the first time), not this DFU mode.
- The CH32 parts used here have no general-purpose retention register in a backup
  domain, so the cross-reset trigger uses **BKP `BKP_DR1`** (it survives a software
  reset).

---

## Memory layout

| Item | Value |
| --- | --- |
| Bootloader | `0x00000000` – `0x00003000` (**12 KB**) |
| Application | `0x00003000` – end of the part, sector aligned |
| DFU sector | 4 KB |
| DfuSe layout string | generated at run time, e.g. CH32V30x `@Internal Flash /0x08003000/29*004Kg` (116 KB), CH32L103 `.../13*004Kg` (52 KB) |

`BOOT_PARTITION_SIZE` lives in `shared/boot_protocol.h` (12 KB, fixed).  The **total
flash size is a board property**: `boards/<board>/board.cmake` passes
`-DBOOT_FLASH_SIZE=<bytes>` (128 KB for `ch32v30x_ob`, 64 KB for `ch32l103_candle`;
CH32V307 = 288 KB).

---

## Build

Use the CMake presets (see `CMakePresets.json`):

```bash
git submodule update --init --recursive     # first time only

cmake --list-presets                        # list the available presets
cmake --preset ch32l103_candle-release      # configure (or ch32v30x_ob-release)
cmake --build --preset ch32l103_candle-release
```

| preset | description | output directory |
| --- | --- | --- |
| `ch32v30x_ob-debug` | Debug (`BOOT_PRINTF` enabled) | `build/ch32v30x_ob-debug/` |
| `ch32v30x_ob-release` | Release (logging off, smaller) | `build/ch32v30x_ob-release/` |
| `ch32l103_candle-debug` | Debug (`BOOT_PRINTF` enabled) | `build/ch32l103_candle-debug/` |
| `ch32l103_candle-release` | Release (logging off, smaller) | `build/ch32l103_candle-release/` |
| `ch32l103_bulepill-debug` | Debug (`BOOT_PRINTF` enabled) | `build/ch32l103_bulepill-debug/` |
| `ch32l103_bulepill-release` | Release (logging off, smaller) | `build/ch32l103_bulepill-release/` |

Artifacts: `<output dir>/ch32_dfu_boot.elf | .hex | .bin`.  Measured Release sizes
(`-Os` + LTO): CH32V30x **9996 B** of 12 KB, CH32L103 **10.5 KB** of 12 KB
(10504 B on the candle board, 10492 B on the bluepill board - the board layer
differs by a few instructions).  The Release image has to fit the 12 KB bootloader
partition; the Debug build does not (V30x 14488 B, L103 16044 B) and fails to link
on purpose.

### What is in the image

[ch32fun](https://github.com/cnlohr/ch32fun) supplies the startup, the vector
table, the clock tree and `printf`; everything else is register access in
`board.c` and the chip port.  There is no vendor HAL in the build.  ch32fun is a
submodule pinned to a known-good commit, so
`git submodule update --init --recursive` fetches it; point `-DCH32FUN_ROOT=<path>`
at another checkout to override.

Measured size of the `ch32v30x_ob` Release build (`-Os`, LTO as configured):

| layer | size |
| --- | --- |
| USB stack (driver + core + DFU class) | 7228 B |
| board + port + application | 1236 B |
| startup / clock / vector table | 646 B |
| ch32fun helpers | 632 B |
| libc / formatter | 58 B |
| padding | 164 B |
| **`.bin` total** | **9964 B** |

CherryUSB is ~73 % of that, so it is where any further saving has to come from.
The DfuSe layout string in `user/dfu_desc.c` is formatted by hand rather than
with `snprintf` for the same reason: the formatter would be ~0.9 KB.

For a new board, add a pair of presets (`<board>-debug` / `<board>-release`) the same
way, or configure directly:

```bash
cmake -S . -B build -DBOARD=<board_name> -DCHIP_PORT=<chip_port>
cmake --build build -j
```

> The `riscv-wch-elf-` toolchain has to be in PATH (on this machine:
> `/opt/Toolchain/RISC-V_Embedded_GCC12`).

---

## Flashing and upgrading

```bash
# 1) first time: with a debugger, flash build/<preset>/ch32_dfu_boot.hex to 0x00000000

# 2) enter DFU
#    - board with a button: hold BOOT while resetting
#    - any board:           dfu-util -e          (the application's DFU runtime)

# 3) download (DfuSe requires the physical address with -s)
dfu-util -a 0 -s 0x08003000:leave -D <app.bin>
```

For `ch32l103_candle`, the matching application is `candleLight_fw_ch32_port`
(`cmake --preset ch32l103_candle-release` there, then the same `dfu-util` line).

Microsoft OS 1.0 (WCID) descriptors are built in, so `dfu-util` works on Windows
without a manual driver step.

---

## Implementation notes

- **DfuSe adaption** (`user/dfu_port.c`): the CherryUSB DFU class only passes
  `wValue` (the block number) through to `usbd_dfu_write()` while DfuSe keeps its
  special command address in the payload - with `wValue == 0`, `data[0]` is
  `0x21` (SET_ADDRESS) or `0x41` (ERASE) and `data[1..4]` is the address in little
  endian; with `wValue >= 2`, `addr = base + (wValue - 2) * wTransferSize`.
- **Flash abstraction** (`port/boot_flash_port.h`): `erase/write/read/addr_in_app`.
  Both ports use the WCH fast page mode (256 B pages), but the program path differs:
  the CH32V30x writes the page straight into flash and starts it, and halves HCLK
  around every erase/program; the CH32L103 follows the SDK's `FLASH_ROM_WRITE`
  (each word latched into the controller's 256 B buffer with `BUFLOAD`, `ADDR`/`STRT`
  written only afterwards) and is specified for the full 96 MHz, so it does not touch
  the clock.  Even if the host never sends ERASE, the DFU port erases a sector on its
  first write to it.
- **Hard disconnect on the USBFS parts**: `boot_usb_port_deinit()` puts the SIE in
  reset (`R8_USB_CTRL.RB_UC_RST_SIE`), which routes PA11/PA12 to the GPIO block, and
  then *drives D+ low* for the settle window.  Releasing the internal pull-up alone
  only floats the line, and a host that misses the disconnect keeps the old device
  instance and ignores whatever attaches next.
- **Clock hand-over**: `main()` puts SYSCLK back on HSI (`clock_fallback_to_hsi()`)
  before `SystemInit()`.  ch32fun rebuilds the tree by *assigning* `RCC->CTLR`, and
  that write clears `PLLON` - if SYSCLK is still on the PLL (the state a software
  reset from the application may not have cleared) the chip dies inside
  `SystemInit()` with the USB device still attached, which the host then reports as
  a dead device that never re-enumerates.
- **Bootloader protection**: `boot_flash_addr_in_app()` refuses erases and writes
  outside the application partition.
- **Jump**: `jr 0x3000` from the software-interrupt handler `SW_Handler`.
- **USBHS high speed**: `-DCONFIG_USB_HS`.  Note that **CherryUSB master has no
  CH32V30x USBHS port**: its `port/wch/usbhs` targets another USBHS IP
  (CH32V205/V4x7/CH32X305/CH58x) whose register map is completely different from the
  CH32V30x `USBHSD` (even the `R8_USB_CTRL` bit assignments differ), and master only
  covers CH32V30x through USBFS.  Upstream deleted the old `port/ch32/ch32hs` in
  `5c54ed49` without a replacement, and the wrong driver writes to the wrong
  registers and leaves **USB unable to enumerate**.
  It has been restored in our own fork: **`git@github.com:zhangjiance/CherryUSB.git`**,
  branch **`ch32v30x-usbhs`**, under `port/wch/ch32v30x/`; the project uses that copy
  from the submodule directly (core / class / DFU still come from that branch's
  master, the DC API is compatible).
- **USBFS full speed (CH32L103)**: no `CONFIG_USB_HS`; the controller is CherryUSB's
  own `port/wch/usbfs/usb_dc_usbfs.c` (its support list includes CH32L103).  The chip
  port supplies what the driver cannot know: `RCC->CFGR0.USBPRE = PLL/2` -> 48 MHz at
  SYSCLK 96 MHz, the USBFS AHB clock, and the `USBFS_IRQHandler` vector that forwards
  to `USBD_IRQHandler(0)`.  DP/DM are the dedicated PA12/PA11 pins, no GPIO setup.

---

## Known limitations / next steps

- The CH32V30x port targets CH32V305-class parts (128 KB); for a CH32V307 (288 KB)
  change `BOOT_FLASH_SIZE` in `boards/ch32v30x_ob/board.cmake` and the RAM size in
  `port/ch32v30x/Link.ld`.
- VID/PID are placeholders (`0x1A86:0xDF11` boot / `0x1A86:0xDF12` app) - replace them
  for a real product.
- The serial number is derived from the chip's ESIG unique ID (`BOOT_UID_BASE`,
  0x1FFFF7E8 on both parts); boards from the same batch share the first bytes, so do
  not treat it as unique there.
- CherryUSB is pinned to master commit `51fef88` (submodule gitlink).  To pin a
  release, check out a tag/commit in the submodule and commit the new gitlink.
