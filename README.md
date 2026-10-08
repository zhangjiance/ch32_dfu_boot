# ch32_dfu_boot

[English](README.md) | [简体中文](README.zh-CN.md)

A **CH32V30x USB DFU bootloader (DfuSe)** built on **CherryUSB**.

Companion **application images** link at `0x00008000` (the application partition) and
come back here with `dfu-util -e` through their DFU runtime interface, or by holding
the BOOT button on boards that have one.

---

## Design goals and layering

The bootloader has to serve **several boards and, later, several CH32 chips**, so it
is decoupled from the start:

| Layer | Directory | Responsibility | New board | New chip |
| --- | --- | --- | --- | --- |
| Contract | `shared/` | partition layout, hand-shake (SDK independent) | unchanged | unchanged |
| Board | `boards/<board>/board_config.h` + `boards/boot_board.c` | clock/UART/button/LED | **one header** | unchanged |
| Chip port | `port/<chip>/` | flash erase/write, cross-reset hand-shake, USB low level | unchanged | **one directory** |
| Firmware | `user/` | boot decision, DFU descriptors, DfuSe adaption | unchanged | unchanged |
| USB stack | `third_party_components/CherryUSB` (submodule) | device stack + DFU class | — | — |

New board: add a directory under `boards/` (`board_config.h` required; `board.c`
optional, for a whole-board implementation; `board.cmake` optional, for extra compile
definitions) and select it with `-DBOARD=<name>`.
New chip: add a directory under `port/` and select it with `-DCHIP_PORT=<name>`.

---

## Layout

```
ch32_dfu_boot/
├── CMakeLists.txt
├── cmake/wch_riscv.cmake
├── shared/boot_protocol.h            # partition + BKP hand-shake (shared with the app)
├── third_party_components/CherryUSB/ # git submodule: our fork, branch ch32v30x-usbhs
├── SDK/                              # WCH peripheral library + startup files
├── boards/
│   ├── boot_board.h / boot_board.c   # generic board implementation (used when a
│   │                                 # board has no board.c of its own)
│   └── ch32v30x_ob/                  # current board BSP
│       └── board_config.h            # BOOT button PA6 + LED PA5
├── port/
│   ├── boot_flash_port.h             # flash abstraction
│   ├── boot_trigger_port.h           # cross-reset hand-shake interface
│   ├── boot_usb_port.h
│   └── ch32v30x/
│       ├── boot_flash_ch32v30x.c     # WCH fast page erase/write
│       ├── boot_trigger_ch32v30x.c   # BKP trigger
│       └── boot_usb_ch32v30x.c       # USBHS RCC + usb_dc_low_level_*
│                                     # (the USBHS device driver lives in the
│                                     #  submodule, under
│                                     #  third_party_components/CherryUSB/port/wch/ch32v30x/)
└── user/
    ├── main.c                        # top level flow
    ├── boot_entry.c/.h               # boot decision + jump
    ├── dfu_desc.c                    # DFU descriptors + USB init
    ├── dfu_port.c/.h                 # DfuSe -> boot_flash_port adaption
    ├── system_ch32v30x.c/.h
    ├── ch32v30x_it.c / ch32v30x_it.h / ch32v30x_conf.h
    ├── boot_log.h / usb_config.h
    └── Link.ld                       # 0x00000000 + 32K
```

---

## Boot flow

`boot_check_and_run_app()` (`user/boot_entry.c`) decides in this order:

1. **BOOT button** (board has one and it is held) → stay in the bootloader (and clear
   a stale trigger flag)
2. **BKP trigger flag** (written by the application through
   `boot_trigger_reboot_to_boot()`) → stay in the bootloader
3. **Application validity**: the first word at `0x00008000` is a JAL
   (`& 0x7F == 0x6F`) → jump to the application

If none of them applies, USBHS is initialised and the device waits for the host in
DfuSe mode.

- On a board **without a button** step 1 never applies, so the bootloader can only be
  entered by an application detach (`dfu-util -e`).
- CH32V30x has no general-purpose retention register in a backup domain, so the
  cross-reset trigger uses **BKP `BKP_DR1`** (it survives a software reset).

---

## Memory layout

| Item | Value |
| --- | --- |
| Bootloader | `0x00000000` – `0x00008000` (**32 KB**) |
| Application | `0x00008000` – end (128 KB flash → 96 KB) |
| DFU sector | 4 KB |
| DfuSe layout string | `@Internal Flash /0x08008000/24*004Kg` (generated at run time) |

`BOOT_PARTITION_SIZE` / `BOOT_FLASH_SIZE` live in `shared/boot_protocol.h`; the total
flash size can be overridden with `-DBOOT_FLASH_SIZE=<bytes>` (e.g. CH32V307 = 288 KB).

---

## Build

Use the CMake presets (see `CMakePresets.json`):

```bash
git submodule update --init --recursive     # first time only

cmake --list-presets                        # list the available presets
cmake --preset ch32v30x_ob-debug            # configure
cmake --build --preset ch32v30x_ob-debug    # build
```

| preset | description | output directory |
| --- | --- | --- |
| `ch32v30x_ob-debug` | Debug (`BOOT_PRINTF` enabled) | `build/ch32v30x_ob-debug/` |
| `ch32v30x_ob-release` | Release (logging off, smaller) | `build/ch32v30x_ob-release/` |

Artifacts: `<output dir>/ch32_dfu_boot.elf | .hex | .bin`
(Debug ≈ 21.7 KB, Release ≈ 15.0 KB, out of the 32 KB partition).

For a new board, add a pair of presets (`<board>-debug` / `<board>-release`) the same
way, or configure directly:

```bash
cmake -S . -B build -DBOARD=<board_name>
cmake --build build -j
```

> The `riscv-wch-elf-` toolchain has to be in PATH (on this machine:
> `/opt/Toolchain/RISC-V_Embedded_GCC12`).

---

## Flashing and upgrading

```bash
# 1) first time: with a debugger, flash build/ch32v30x_ob-debug/ch32_dfu_boot.hex to 0x00000000

# 2) enter DFU
#    - board with a button: hold BOOT while resetting
#    - any board:           dfu-util -e          (the application's DFU runtime)

# 3) download (DfuSe requires the physical address with -s)
dfu-util -a 0 -s 0x08008000:leave -D <app.bin>
```

Microsoft OS 1.0 (WCID) descriptors are built in, so `dfu-util` works on Windows
without a manual driver step.

---

## Implementation notes

- **DfuSe adaption** (`user/dfu_port.c`): the CherryUSB DFU class only passes
  `wValue` (the block number) through to `usbd_dfu_write()` while DfuSe keeps its
  special command address in the payload - with `wValue == 0`, `data[0]` is
  `0x21` (SET_ADDRESS) or `0x41` (ERASE) and `data[1..4]` is the address in little
  endian; with `wValue >= 2`, `addr = base + (wValue - 2) * wTransferSize`.
- **Flash abstraction** (`port/boot_flash_port.h`): `erase/write/read/addr_in_app`;
  the port implements the WCH fast page mode (page 256 B, `RCC_HPRE_DIV2` around every
  erase/program).  Even if the host never sends ERASE, the DFU port erases a sector on
  its first write to it.
- **Bootloader protection**: `boot_flash_addr_in_app()` refuses erases and writes
  outside the application partition.
- **Jump**: `jr 0x8000` from the software-interrupt handler `SW_Handler`.
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

---

## Known limitations / next steps

- Targets CH32V305-class parts (128 KB) by default; for a CH32V307 (288 KB) change
  `BOOT_FLASH_SIZE` and the linker script.
- VID/PID are placeholders (`0x1A86:0xDF11` boot / `0x1A86:0xDF12` app) - replace them
  for a real product.
- The serial number is a fixed string; read the chip's unique ID instead if several
  devices have to be told apart.
- CherryUSB is pinned to master commit `51fef88` (submodule gitlink).  To pin a
  release, check out a tag/commit in the submodule and commit the new gitlink.
