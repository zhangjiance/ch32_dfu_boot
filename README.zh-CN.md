# ch32_dfu_boot

[English](README.md) | [简体中文](README.zh-CN.md)

基于 **CherryUSB** 的 **CH32V30x USB DFU 引导程序（DfuSe）**。

配套的 **APP 镜像**链接在应用分区 `0x00008000`，可通过自身的 DFU runtime 接口
（`dfu-util -e`）回到本 bootloader，带按键的板子也可以长按 BOOT 键回到这里。

---

## 设计目标与分层

Boot 需要面向**多个硬件 / 未来多个 CH32 芯片**，因此从一开始就做了分层解耦：

| 层 | 目录 | 职责 | 换硬件 | 换芯片 |
| --- | --- | --- | --- | --- |
| 契约 | `shared/` | 分区布局、握手指令（SDK 无关） | 不变 | 不变 |
| 板级 | `boards/<board>/`（`board_config.h` + `board.h` + `board.c`） | 时钟/串口/按键/LED 原语 + tick | **只加一个目录** | 不变 |
| 芯片端口 | `port/<chip>/` | flash 擦写、跨复位握手、USB 底层 | 不变 | **只加一个目录** |
| 固件 | `user/` | 启动判定、DFU 描述符、DfuSe 适配 | 不变 | 不变 |
| USB 栈 | `third_party_components/CherryUSB`（子仓） | 设备栈 + DFU 类 | — | — |

新增硬件：拷贝 `boards/` 下的一个目录，改 `board_config.h`（`BOARD_NAME` + `BOARD_*`
引脚/特性）即可。板目录是**自包含**的 —— `board_config.h`、`board.h`（原语接口）、
`board.c`（原语实现 + tick 中断），拷贝过去直接可用；`board.cmake` 可选，追加编译定义。
用 `-DBOARD=<name>` 选择。
新增芯片：在 `port/` 加一个目录，用 `-DCHIP_PORT=<name>` 选择。

---

## 目录结构

```
ch32_dfu_boot/
├── CMakeLists.txt
├── cmake/wch_riscv.cmake
├── shared/boot_protocol.h            # 分区 + BKP 握手（boot/APP 共用）
├── third_party_components/CherryUSB/ # git 子仓：自己的 fork，分支 ch32v30x-usbhs
├── SDK/                              # WCH 外设库 + 启动文件
├── boards/
│   └── ch32v30x_ob/                  # 当前板 BSP（自包含，拷贝即复用）
│       ├── board_config.h            # BOARD_* 宏：BOOT 按键 PA6 + LED PA5
│       ├── board.h                   # 板级原语接口
│       └── board.c                   # 板级原语 + 周期 tick 中断
├── port/
│   ├── boot_flash_port.h             # flash 抽象接口
│   ├── boot_trigger_port.h           # 跨复位握手接口
│   ├── boot_usb_port.h
│   └── ch32v30x/
│       ├── boot_flash_ch32v30x.c     # WCH 快速页擦写
│       ├── boot_trigger_ch32v30x.c   # BKP 触发
│       └── boot_usb_ch32v30x.c       # USBHS RCC + usb_dc_low_level_*
│                                     # （USBHS 设备驱动在子仓
│                                     #   third_party_components/CherryUSB/port/wch/ch32v30x/）
└── user/
    ├── main.c                        # 顶层流程
    ├── boot_entry.c/.h               # 启动判定 + 跳转
    ├── dfu_desc.c                    # DFU 描述符 + USB 初始化
    ├── dfu_port.c/.h                 # DfuSe → boot_flash_port 适配
    ├── system_ch32v30x.c/.h
    ├── ch32v30x_it.c / ch32v30x_it.h / ch32v30x_conf.h
    ├── boot_log.h / usb_config.h
    └── Link.ld                       # 0x00000000 + 32K
```

---

## 启动流程

`boot_check_and_run_app()`（`user/boot_entry.c`）依次判断：

1. **BOOT 按键**（board 有按键且按住）→ 停留 bootloader（并清除旧触发标记）
2. **BKP 触发标记**（APP 通过 `boot_trigger_reboot_to_boot()` 写入）→ 停留 bootloader
3. **APP 有效性**：`0x00008000` 处首字为 JAL（`& 0x7F == 0x6F`）→ 跳转到 APP

三者都不满足时初始化 USBHS，进入 DfuSe 模式等待主机。

- board **无按键**时第 1 步永远不成立，只能靠 APP detach（`dfu-util -e`）进入。
- CH32V30x 没有备份域的通用保留寄存器，跨复位触发用 **BKP `BKP_DR1`**（软复位保留）。

---

## 内存布局

| 项目 | 值 |
| --- | --- |
| Bootloader | `0x00000000` – `0x00008000`（**32 KB**） |
| APP | `0x00008000` – 结尾（128 KB flash → 96 KB） |
| DFU 扇区 | 4 KB |
| DfuSe 布局串 | `@Internal Flash /0x08008000/24*004Kg`（运行时生成） |

`BOOT_PARTITION_SIZE` / `BOOT_FLASH_SIZE` 在 `shared/boot_protocol.h`；
总 flash 可用 `-DBOOT_FLASH_SIZE=<bytes>` 覆盖（如 CH32V307 = 288 KB）。

---

## 编译

使用 CMake preset（见 `CMakePresets.json`）：

```bash
git submodule update --init --recursive     # 首次

cmake --list-presets                        # 列出可用 preset
cmake --preset ch32v30x_ob-debug            # 配置
cmake --build --preset ch32v30x_ob-debug    # 编译
```

| preset | 说明 | 输出目录 |
| --- | --- | --- |
| `ch32v30x_ob-debug` | Debug（`BOOT_PRINTF` 打开） | `build/ch32v30x_ob-debug/` |
| `ch32v30x_ob-release` | Release（日志关闭，体积更小） | `build/ch32v30x_ob-release/` |

产物：`<输出目录>/ch32_dfu_boot.elf | .hex | .bin`
（Debug ≈ 21.7 KB，Release ≈ 15.0 KB，分区 32 KB）。

新增 board 后照葫芦画瓢加一对 preset（`<board>-debug` / `<board>-release`），
也可以不用 preset 直接：

```bash
cmake -S . -B build -DBOARD=<board_name>
cmake --build build -j
```

> 需要 `riscv-wch-elf-` 工具链在 PATH 中（本机 `/opt/Toolchain/RISC-V_Embedded_GCC12`）。

---

## 烧录与升级

```bash
# 1) 首次：调试器烧 build/ch32v30x_ob-debug/ch32_dfu_boot.hex 到 0x00000000

# 2) 进入 DFU
#    - 带按键板：按住 BOOT 键复位
#    - 任意板：  dfu-util -e          （APP 的 DFU runtime 触发）

# 3) 下载（DfuSe 必须带 -s 物理地址）
dfu-util -a 0 -s 0x08008000:leave -D <app.bin>
```

Windows 下已内置 MS OS 1.0 (WCID) 描述符，正常情况下 `dfu-util` 可直接使用。

---

## 关键实现说明

- **DfuSe 协议适配**（`user/dfu_port.c`）：CherryUSB 的 DFU 类只把 `wValue`（块号）
  透传给 `usbd_dfu_write()`，DfuSe 特殊命令地址放在 payload 里——
  `wValue==0` 时 `data[0]` 为 `0x21`(SET_ADDRESS)/`0x41`(ERASE)，`data[1..4]` 小端地址；
  `wValue>=2` 时 `addr = base + (wValue-2) * wTransferSize`。
- **Flash 抽象**（`port/boot_flash_port.h`）：`erase/write/read/addr_in_app`，端口实现
  负责 WCH 快速页擦写（页 256 B，每次擦写前后 `RCC_HPRE_DIV2`）。
  即使主机不发 ERASE，DFU 端口也会在首次写入某扇区时自动擦除。
- **Bootloader 保护**：`boot_flash_addr_in_app()` 拒绝应用分区之外的擦写。
- **跳转**：软件中断 `SW_Handler` 中 `jr 0x8000`。
- **USBHS 高速**：`-DCONFIG_USB_HS`。注意 **CherryUSB master 没有 CH32V30x USBHS 端口**：
  它的 `port/wch/usbhs` 面向另一套 USBHS IP（CH32V205/V4x7/CH32X305/CH58x），寄存器映射与
  CH32V30x 的 `USBHSD` 完全不同（连 `R8_USB_CTRL` 的位定义都不一样），而 master 里
  CH32V30x 只被 USBFS 覆盖。上游在 `5c54ed49` 删掉了老的 `port/ch32/ch32hs` 且未提供替代，
  用错驱动会让初始化写进错误寄存器，**USB 完全无法枚举**。
  我们已把它恢复到自己的 fork：**`git@github.com:zhangjiance/CherryUSB.git`** 分支
  **`ch32v30x-usbhs`** 的 `port/wch/ch32v30x/`，工程直接引用子仓里的这一份
  （核心 / 类 / DFU 类仍用该分支的 master，DC API 一致可混用）。

---

## 已知限制 / 后续

- 目标默认 **CH32V305 类（128 KB）**；CH32V307（288 KB）改 `BOOT_FLASH_SIZE` 与链接脚本。
- VID/PID 为占位值（`0x1A86:0xDF11` boot / `0x1A86:0xDF12` app），正式产品请替换。
- 串口序列号为固定字符串，如需多设备区分可改为读取芯片唯一 ID。
- CherryUSB 固定于 master 提交 `51fef88`（子仓 gitlink）。需锁版本时在子仓切 tag/commit 后提交。
