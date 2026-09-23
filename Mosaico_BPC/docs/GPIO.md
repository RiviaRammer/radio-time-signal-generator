# 扩展 GPIO 映射

核对来源：`esp-mosaico-bsp` 提交 `05e067d` 的 `components/esp-mosaico-bsp/onboard/subboard.c` 和 `include/bsp/subboard.h`。复用功能另对照 `include/bsp/esp_mosaico.h`。

上游源码：[subboard.c](https://github.com/esp-mosaico/esp-mosaico-bsp/blob/05e067d/components/esp-mosaico-bsp/onboard/subboard.c)。左/右以 BSP 的槽位定义为准；右槽存在 180 度旋转，编号不是排针的物理顺序。

| 左侧 GPIO | 右侧 GPIO | BSP 说明 |
| --- | --- | --- |
| 53 | 46 | H2 |
| 48 | 47 | H4 |
| 13 | 11 | H6 |
| 12 | 10 | H8 |
| 14 | 39 | H10，标准模块 EEPROM 地址选择复用 |
| 4 | 5 | H12 |
| 16 | 40 | 扩展对；40 复用音频输入 |
| 15 | 38 | 扩展对；本项目默认 38 |
| 17 | 37 | 扩展对；37 复用音频位时钟 |
| 18 | 54 | 扩展对；54 复用音频主时钟 |
| 19 | 52 | 扩展对；52 复用音频数据输出 |
| 55 | 49 | 扩展对；49 复用音频左右声道时钟 |

这些 24 个引脚均允许输入。当前应用未启动音频、摄像头、NAND、扩展模块管理或 BOOT 按钮驱动。存在实际外接模块时仍需先确认是否占用；软件允许不代表可以与已接入的外设同时驱动。

GPIO0/1 虽出现在扩展 I²C 上，但 v1.0 还与主板/触摸 I²C 共用，因此不开放。GPIO6 为触摸中断；LCD 使用 GPIO9、35、36、42、43、44、50、51。Flash/NAND、电源、系统按钮等其他引脚不列入天线输入范围。

GPIO38 是首次启动的默认值；之后保存过的合法 GPIO 会从 NVS 恢复。清空配置测试用完整固件，它会重置 NVS 并恢复默认 38。

切换过程先停止旧载波、解绑旧引脚，再配置新引脚，并在下一个 20 秒边界启动完整帧。新引脚采用推挽输出，无载波时为低电平。
