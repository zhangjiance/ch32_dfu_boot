# ch32_dfu_boot

[English](README.md) | [简体中文](README.zh-CN.md)

基于 **ch32fun** + **CherryUSB** 的 **CH32 USB DFU 引导程序（DfuSe）**，不依赖任何厂商库。
目前有两个芯片端口：

| 芯片端口 | USB 控制器 | 板卡 |
| --- | --- | --- |
| `port/ch32v30x` | USBHS，高速 | `boards/ch32v30x_ob` |
| `port/ch32l103` | USBFS，全速 | `boards/ch32l103_candle`（`candleLight_fw_ch32_port` 的 USB-CANFD 适配板） |
| `port/ch32l103` | USBFS，全速 | `boards/ch32l103_bulepill`（bluepill 形态的 CH32L103C8T6 板，USB-C 走 PA11/PA12） |

配套的 **APP 镜像**链接在应用分区 `0x00003000`，可通过自身的 DFU runtime 接口
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

芯片端口是唯一知道“这颗芯片”的地方：`port.cmake` 给出 ISA 字符串（CH32V30x 用
`rv32imacxw`、CH32L103 用 `rv32imac`）、ch32fun 识别芯片用的宏、USB 控制器源码与速度
（是否 `CONFIG_USB_HS`）、ESIG 唯一 ID 地址和链接脚本；三个 `boot_*_<chip>.c` 分别实现
flash、跨复位握手和 USB 底层契约。`user/` 与 `shared/` 里不再出现任何具体芯片名。

---

## 目录结构

```
ch32_dfu_boot/
├── CMakeLists.txt
├── cmake/riscv.cmake                 # riscv-wch-elf- 工具链与编译选项
├── shared/boot_protocol.h            # 分区 + BKP 握手（boot/APP 共用）
├── third_party_components/
│   ├── CherryUSB/                    # git 子仓：自己的 fork，分支 ch32v30x-usbhs
│   └── ch32fun/                      # git 子仓：cnlohr/ch32fun，已固定提交
├── boards/
│   ├── ch32v30x_ob/                  # 板 BSP（自包含，拷贝即复用）
│   │   ├── board_config.h            # BOARD_* 宏：BOOT 按键 PA6 + LED PA5
│   │   ├── board.h                   # 板级原语接口
│   │   ├── board.c                   # 板级原语 + 周期 tick 中断
│   │   └── board.cmake               # 晶振 + 时钟树
│   ├── ch32l103_candle/              # CH32L103 USB-CANFD 适配板
│   │   ├── board_config.h            # LED1 PA0 / LED2 PA1 / 终端电阻 PA2，BOOT 键接 BOOT0
│   │   ├── board.h
│   │   ├── board.c
│   │   └── board.cmake               # 8 MHz HSE ×12 = 96 MHz + BOOT_FLASH_SIZE=64K
│   └── ch32l103_bulepill/            # bluepill 形态的 CH32L103C8T6 板
│       ├── board_config.h            # LED PB11/PB10/PB2（高电平点亮），BOOT 键接 BOOT0
│       ├── board.h
│       ├── board.c
│       └── board.cmake               # 8 MHz HSE ×12 = 96 MHz + BOOT_FLASH_SIZE=64K
├── port/
│   ├── boot_flash_port.h             # flash 抽象接口
│   ├── boot_trigger_port.h           # 跨复位握手接口
│   ├── boot_usb_port.h
│   ├── ch32v30x/
│   │   ├── port.cmake                # rv32imacxw、CONFIG_USB_HS、USBHS 驱动、Link.ld
│   │   ├── boot_flash_ch32v30x.c     # 快速页擦写
│   │   ├── boot_trigger_ch32v30x.c   # BKP 触发
│   │   ├── boot_usb_ch32v30x.c       # USBHS 时钟 + usb_dc_low_level_*
│   │   └── Link.ld                   # 0x00000000 + 12K，32K RAM
│   │                                 # （USBHS 设备驱动在子仓
│   │                                 #   third_party_components/CherryUSB/port/wch/ch32v30x/）
│   └── ch32l103/
│       ├── port.cmake                # rv32imac、USBFS 驱动、Link.ld
│       ├── boot_flash_ch32l103.c     # 带 BUF 的快速页擦写（256 B）
│       ├── boot_trigger_ch32l103.c   # BKP 触发
│       ├── boot_usb_ch32l103.c       # USBFS 时钟 + usb_dc_low_level_* + 中断向量
│       └── Link.ld                   # 0x00000000 + 12K，20K RAM
│                                     # （USBFS 驱动用 CherryUSB 自带
│                                     #   port/wch/usbfs/）
└── user/
    ├── main.c                        # 顶层流程
    ├── boot_entry.c/.h               # 启动判定 + 跳转
    ├── dfu_desc.c                    # DFU 描述符 + USB 初始化
    ├── dfu_port.c/.h                 # DfuSe → boot_flash_port 适配
    ├── funconfig.h                   # ch32fun 配置（printf、体积开关）
    └── boot_log.h / usb_config.h
```

---

## 启动流程

`boot_check_and_run_app()`（`user/boot_entry.c`）依次判断：

1. **接在 GPIO 上的 BOOT 按键**（board 有这种按键且按住）→ 停留 bootloader（并清除旧触发标记）
2. **BKP 触发标记**（APP 通过 `boot_trigger_reboot_to_boot()` 写入）→ 停留 bootloader
3. **APP 有效性**：`0x00003000` 处首字为 JAL（`& 0x7F == 0x6F`）→ 跳转到 APP

三者都不满足时初始化芯片端口的 USB 控制器（`boot_usb_reg_base()`：CH32V30x 用 USBHS，
CH32L103 用 USBFS），进入 DfuSe 模式等待主机。

- 第 1 步要求按键接在 **GPIO** 上。两块 L103 板都有 BOOT 键，但接的是 **BOOT0（第 44 脚）**：
  那是复位时由硬件采样的专用启动模式输入，固件读不到，所以对本 bootloader 而言它们等于无按键，
  只能靠 APP detach（`dfu-util -e`）或 APP 镜像无效时自动停留。按住该键复位进入的是 WCH
  系统存储区的 ISP 下载模式（正好用来第一次烧本 bootloader），不是这里描述的 DFU 模式。
- 这里用到的 CH32 都没有备份域的通用保留寄存器，跨复位触发用 **BKP `BKP_DR1`**（软复位保留）。

---

## 内存布局

| 项目 | 值 |
| --- | --- |
| Bootloader | `0x00000000` – `0x00003000`（**12 KB**） |
| APP | `0x00003000` – 芯片 flash 结尾，按扇区对齐 |
| DFU 扇区 | 4 KB |
| DfuSe 布局串 | 运行时生成：CH32V30x `@Internal Flash /0x08003000/29*004Kg`（116 KB）、CH32L103 `.../13*004Kg`（52 KB） |

`BOOT_PARTITION_SIZE` 在 `shared/boot_protocol.h`（固定 12 KB）；**flash 总容量是板级属性**，
由 `boards/<board>/board.cmake` 传 `-DBOOT_FLASH_SIZE=<bytes>`（`ch32v30x_ob` 为 128 KB，
`ch32l103_candle` 为 64 KB；CH32V307 = 288 KB）。

---

## 编译

使用 CMake preset（见 `CMakePresets.json`）：

```bash
git submodule update --init --recursive     # 首次

cmake --list-presets                        # 列出可用 preset
cmake --preset ch32l103_candle-release      # 配置（或 ch32v30x_ob-release）
cmake --build --preset ch32l103_candle-release
```

| preset | 说明 | 输出目录 |
| --- | --- | --- |
| `ch32v30x_ob-debug` | Debug（`BOOT_PRINTF` 打开） | `build/ch32v30x_ob-debug/` |
| `ch32v30x_ob-release` | Release（日志关闭，体积更小） | `build/ch32v30x_ob-release/` |
| `ch32l103_candle-debug` | Debug（`BOOT_PRINTF` 打开） | `build/ch32l103_candle-debug/` |
| `ch32l103_candle-release` | Release（日志关闭，体积更小） | `build/ch32l103_candle-release/` |
| `ch32l103_bulepill-debug` | Debug（`BOOT_PRINTF` 打开） | `build/ch32l103_bulepill-debug/` |
| `ch32l103_bulepill-release` | Release（日志关闭，体积更小） | `build/ch32l103_bulepill-release/` |

产物：`<输出目录>/ch32_dfu_boot.elf | .hex | .bin`。Release 实测（`-Os` + LTO）：
CH32V30x **9996 B** / 12 KB，CH32L103 **约 10.5 KB** / 12 KB（candle 板 10504 B、
bluepill 板 10492 B，板级差异只有几条指令）。Release 必须放进 12 KB 的 bootloader 分区；
Debug 版放不进去（V30x 14488 B、L103 16044 B），会故意链接失败。

### 镜像里有什么

[ch32fun](https://github.com/cnlohr/ch32fun) 提供启动、向量表、时钟树和 `printf`，
其余全部是 `board.c` 与芯片 port 里的寄存器操作——构建里没有任何厂商 HAL。
ch32fun 是 git 子仓并固定在验证过的提交，`git submodule update --init --recursive`
会自动拉取；也可用 `-DCH32FUN_ROOT=<path>` 指定别的副本。

`ch32v30x_ob` Release 实测体积（`-Os`，LTO 如配置）：

| 层 | 大小 |
| --- | --- |
| USB 栈（驱动 + core + DFU 类） | 7228 B |
| board + port + 应用 | 1236 B |
| 启动 / 时钟 / 向量表 | 646 B |
| ch32fun 辅助 | 632 B |
| libc / 格式化 | 58 B |
| 对齐填充 | 164 B |
| **`.bin` 合计** | **9964 B** |

其中 CherryUSB 约占 73%，再往下压只能从这里入手。`user/dfu_desc.c` 里 DfuSe
布局串改成手写而不用 `snprintf`，也是同一原因：光是格式化器就要约 0.9 KB。

新增 board 后照葫芦画瓢加一对 preset（`<board>-debug` / `<board>-release`），
也可以不用 preset 直接：

```bash
cmake -S . -B build -DBOARD=<board_name> -DCHIP_PORT=<chip_port>
cmake --build build -j
```

> 需要 `riscv-wch-elf-` 工具链在 PATH 中（本机 `/opt/Toolchain/RISC-V_Embedded_GCC12`）。

---

## 烧录与升级

```bash
# 1) 首次：调试器烧 build/<preset>/ch32_dfu_boot.hex 到 0x00000000

# 2) 进入 DFU
#    - 带按键板：按住 BOOT 键复位
#    - 任意板：  dfu-util -e          （APP 的 DFU runtime 触发）

# 3) 下载（DfuSe 必须带 -s 物理地址）
dfu-util -a 0 -s 0x08003000:leave -D <app.bin>
```

`ch32l103_candle` 的配套 APP 就是 `candleLight_fw_ch32_port`（在那里用
`cmake --preset ch32l103_candle-release` 编译，再用上面同一条 `dfu-util` 命令下载）。

Windows 下已内置 MS OS 1.0 (WCID) 描述符，正常情况下 `dfu-util` 可直接使用。

---

## 关键实现说明

- **DfuSe 协议适配**（`user/dfu_port.c`）：CherryUSB 的 DFU 类只把 `wValue`（块号）
  透传给 `usbd_dfu_write()`，DfuSe 特殊命令地址放在 payload 里——
  `wValue==0` 时 `data[0]` 为 `0x21`(SET_ADDRESS)/`0x41`(ERASE)，`data[1..4]` 小端地址；
  `wValue>=2` 时 `addr = base + (wValue-2) * wTransferSize`。
- **Flash 抽象**（`port/boot_flash_port.h`）：`erase/write/read/addr_in_app`。两个端口
  都用 WCH 快速页模式（页 256 B），但编程路径不同：CH32V30x 直接把一整页写进 flash 再
  启动，并在每次擦写前后 `RCC_HPRE_DIV2`；CH32L103 按 SDK 的 `FLASH_ROM_WRITE` 走——每个字
  先写目标地址再用 `BUFLOAD` 锁进控制器 256 B 缓冲，最后才写 `ADDR`+`STRT` 提交整页（照
  V30x 那样直接写并启动，缓冲没装数据，等于什么都没写），且官方例程就是在满速 96 MHz 下做的，
  所以不动时钟。即使主机不发 ERASE，DFU 端口也会在首次写入某扇区时自动擦除。
- **USBFS 侧的硬断开**：`boot_usb_port_deinit()` 先把 SIE 置复位
  （`R8_USB_CTRL.RB_UC_RST_SIE`，此时 PA11/PA12 变成普通 GPIO），再**把 D+ 拉低**
  保持整个断开窗口。只关内部上拉是把线放开，主机不一定采得到断开，就会保留旧设备实例、
  后面挂上来的设备再也不被枚举。既然能把线真正拉低，保持时间就压到 USB 主机能观察到的
  10 ms 附近（96 MHz 下约 15 ms），不让它拖慢二次枚举。
- **时钟交接**：`main()` 在 `SystemInit()` 之前先用 `clock_fallback_to_hsi()` 把 SYSCLK
  切回 HSI。ch32fun 是**赋值**方式重建时钟树，第一条写 `RCC->CTLR` 就清掉 `PLLON`——
  如果 SYSCLK 还挂在 PLL 上（APP 复位进来时很可能如此），芯片会在 `SystemInit()` 里
  直接死掉，而 USB 设备还挂着 D+，主机就看到一个"设备在但不响应"的死设备，永远等不到
  重新枚举。
- **Bootloader 保护**：`boot_flash_addr_in_app()` 拒绝应用分区之外的擦写。
- **跳转**：软件中断 `SW_Handler` 中 `jr 0x3000`。
- **USBHS 高速**：`-DCONFIG_USB_HS`。注意 **CherryUSB master 没有 CH32V30x USBHS 端口**：
  它的 `port/wch/usbhs` 面向另一套 USBHS IP（CH32V205/V4x7/CH32X305/CH58x），寄存器映射与
  CH32V30x 的 `USBHSD` 完全不同（连 `R8_USB_CTRL` 的位定义都不一样），而 master 里
  CH32V30x 只被 USBFS 覆盖。上游在 `5c54ed49` 删掉了老的 `port/ch32/ch32hs` 且未提供替代，
  用错驱动会让初始化写进错误寄存器，**USB 完全无法枚举**。
  我们已把它恢复到自己的 fork：**`git@github.com:zhangjiance/CherryUSB.git`** 分支
  **`ch32v30x-usbhs`** 的 `port/wch/ch32v30x/`，工程直接引用子仓里的这一份
  （核心 / 类 / DFU 类仍用该分支的 master，DC API 一致可混用）。
- **USBFS 全速（CH32L103）**：不定义 `CONFIG_USB_HS`，控制器驱动直接用 CherryUSB 自带的
  `port/wch/usbfs/usb_dc_usbfs.c`（其支持列表里就有 CH32L103）。芯片端口补上驱动无法知道的
  部分：`RCC->CFGR0.USBPRE = PLL/2`（96 MHz 下得到 48 MHz）、USBFS 的 AHB 时钟，以及把
  `USBFS_IRQHandler` 向量转到 `USBD_IRQHandler(0)`。DP/DM 是 PA12/PA11 专用引脚，无需配 GPIO。

---

## 已知限制 / 后续

- CH32V30x 端口面向 **CH32V305 类（128 KB）**；CH32V307（288 KB）需改
  `boards/ch32v30x_ob/board.cmake` 的 `BOOT_FLASH_SIZE` 和 `port/ch32v30x/Link.ld` 的 RAM 大小。
- VID/PID 为占位值（`0x1A86:0xDF11` boot / `0x1A86:0xDF12` app），正式产品请替换。
- 序列号取自 ESIG 芯片唯一 ID（`BOOT_UID_BASE`，两颗芯片都是 0x1FFFF7E8）；同批次芯片
  前若干字节相同，不要当作全局唯一。
- CherryUSB 固定于 master 提交 `51fef88`（子仓 gitlink）。需锁版本时在子仓切 tag/commit 后提交。
