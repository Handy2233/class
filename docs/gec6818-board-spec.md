# GEC6818 Board Specification

本文件记录项目使用的 GEC6818/S5P6818 核心板规格、接口与引脚定义。

## 结构参数

| 项目 | 参数 |
| --- | --- |
| 核心板尺寸 | 75mm * 55mm |
| 引脚间距 | 2.0mm |
| 特点 | 易更换、易维护 |

## 系统配置

| 项目 | 参数 |
| --- | --- |
| CPU | S5P6818 |
| 主频 | 64 位八核 1.4GHz |
| 内存 | 标配 1GB，可定制 2GB |
| 存储器 | 4GB/8GB/16GB/32GB emmc 可选，标配 8GB |
| 电源 IC | 使用 AXP228，支持动态调频，库仑计等 |
| 以太网 | 使用 RTL8211E 千兆以太网 PHY |

## 接口参数

| 项目 | 参数 |
| --- | --- |
| LCD 接口 | 同时支持 TTL、LVDS、MIPI 接口输出 |
| Touch 接口 | 电容触摸，可使用 USB 或串口扩展电阻触摸 |
| 音频接口 | AC97/IIS 接口，支持录放音 |
| SD 卡接口 | 2 路 SDIO 输出通道 |
| emmc 接口 | 板载 emmc 接口，管脚不另外引出 |
| 以太网接口 | 支持千兆以太网 |
| USB HOST 接口 | 一路 HOST2.0，一路 HSIC |
| USB OTG 接口 | 一路 OTG2.0 |
| UART 接口 | 6 路串口，支持带流控串口 |
| PWM 接口 | 4 路 PWM 输出 |
| IIC 接口 | 2 路 IIC 输出 |
| SPI 接口 | 1 路 SPI 输出 |
| ADC 接口 | 2 路 ADC 输出 |
| Camera 接口 | 1 路 CIF，1 路 MIPI 输出 |
| HDMI 接口 | 高清音视频输出接口，音视频同步输出 |
| VGA 接口 | 使用 LCD 输出接口扩展 |
| 启动配置接口 | 无需启动配置，核心板自动适配 |

## 电气特性

| 项目 | 参数 |
| --- | --- |
| 输入电压 | 3.7~5.5V，推荐使用 5V 输入 |
| 输出电压 | 3.3V/4.2V，可用于底板供电及电池充电 |
| 工作温度 | -40~80 度 |
| 储存温度 | -10~80 度 |

## 核心板引脚定义 1

| 引脚编号 | 信号 | 引脚编号 | 信号 |
| --- | --- | --- | --- |
| 1 | LCD_PWM | 28 | LCD_CLK |
| 2 | LCD_EN | 29 | LCD_DE |
| 3 | LCD_RESET | 30 | LCD_HSYNC |
| 4 | LCD_R0 | 31 | LCD_VSYNC |
| 5 | LCD_R1 | 32 | GPIOE13 |
| 6 | LCD_R2 | 33 | MCU_SDA_0 |
| 7 | LCD_R3 | 34 | MCU_SCL_0 |
| 8 | LCD_R4 | 35 | MCU_HDMI_CEC |
| 9 | LCD_R5 | 36 | MCU_HDMI_HPD |
| 10 | LCD_R6 | 37 | MCU_HDMI_TXCN |
| 11 | LCD_R7 | 38 | MCU_HDMI_TXCP |
| 12 | LCD_G0 | 39 | MCU_HDMI_TX0N |
| 13 | LCD_G1 | 40 | MCU_HDMI_TX0P |
| 14 | LCD_G2 | 41 | MCU_HDMI_TX1N |
| 15 | LCD_G3 | 42 | MCU_HDMI_TX1P |
| 16 | LCD_G4 | 43 | MCU_HDMI_TX2N |
| 17 | LCD_G5 | 44 | MCU_HDMI_TX2P |
| 18 | LCD_G6 | 45 | GND |
| 19 | LCD_G7 | 46 | MCU_LVDS_CLKM |
| 20 | LCD_B0 | 47 | MCU_LVDS_CLKP |
| 21 | LCD_B1 | 48 | MCU_LVDS_Y3M |
| 22 | LCD_B2 | 49 | MCU_LVDS_Y3P |
| 23 | LCD_B3 | 50 | MCU_LVDS_Y2M |
| 24 | LCD_B4 | 51 | MCU_LVDS_Y2P |
| 25 | LCD_B5 | 52 | MCU_LVDS_Y1M |
| 26 | LCD_B6 | 53 | MCU_LVDS_Y1P |
| 27 | LCD_B7 | 54 | MCU_LVDS_Y0M |

## 核心板引脚定义 2

| 引脚编号 | 信号 | 引脚编号 | 信号 |
| --- | --- | --- | --- |
| 55 | MCU_LVDS_Y0P | 73 | MIPICSI_DN0 |
| 56 | MIPIDSI_DP3 | 74 | MIPICSI_DP0 |
| 57 | MIPIDSI_DN3 | 75 | MIPICSI_DNCLK |
| 58 | MIPIDSI_DP2 | 76 | MIPICSI_DPCLK |
| 59 | MIPIDSI_DN2 | 77 | CAM_H |
| 60 | MIPIDSI_DP1 | 78 | CAM_V |
| 61 | MIPIDSI_DN1 | 79 | CAM_CLK |
| 62 | MIPIDSI_DP0 | 80 | CAM_D0 |
| 63 | MIPIDSI_DN0 | 81 | CAM_D1 |
| 64 | MIPIDSI_DPCLK | 82 | CAM_D2 |
| 65 | MIPIDSI_DNCLK | 83 | CAM_D3 |
| 66 | MIPIDSI_VREG | 84 | CAM_D4 |
| 67 | MIPICSI_DN3 | 85 | CAM_D5 |
| 68 | MIPICSI_DP3 | 86 | CAM_D6 |
| 69 | MIPICSI_DN2 | 87 | CAM_D7 |
| 70 | MIPICSI_DP2 | 88 | MCU_CAM1_MCLK |
| 71 | MIPICSI_DN1 | 89 | CAM_PN |
| 72 | MIPICSI_DP1 | 90 | CAM_RST |

## 核心板引脚定义 3

| 引脚编号 | 信号 | 引脚编号 | 信号 |
| --- | --- | --- | --- |
| 91 | CAM_PD | 118 | UARTRXD1 |
| 92 | GPIOB8 | 119 | UARTRXD0 |
| 93 | MCU_CAM1_D7 | 120 | UARTTXD0 |
| 94 | MCU_CAM1_D4 | 121 | GND |
| 95 | MCU_CAM1_D3 | 122 | VBAT |
| 96 | MCU_CAM1_D2 | 123 | VBAT |
| 97 | MCU_CAM1_D1 | 124 | +5V_IN |
| 98 | MCU_CAM1_D0 | 125 | +5V_IN |
| 99 | MCU_I2S_MCLK | 126 | VBAT_SYS |
| 100 | MCU_I2S_BCK | 127 | GND |
| 101 | MCU_I2S_SDIN | 128 | LINK_LED |
| 102 | MCU_I2S_SDOUT | 129 | SPEED_LED |
| 103 | MCU_I2S_LRCK | 130 | MDIO_P |
| 104 | MCU_HP_DET | 131 | MDIO_N |
| 105 | SPDIF_TX | 132 | MDI1_P |
| 106 | SPDIF_RX | 133 | MDI1_N |
| 107 | MCU_KEY_VOLDN | 134 | MDI2_P |
| 108 | MCU_KEY_VOLUP | 135 | MDI2_N |
| 109 | MCU_NRESETIN | 136 | MDI3_P |
| 110 | MCU_PWRKEY | 137 | MDI3_N |
| 111 | GPIOA28 | 138 | USBHSIC_DATA |
| 112 | GPIOB9 | 139 | USBHSIC_STROBE |
| 113 | UARTRXD3 | 140 | USB_HOST_D- |
| 114 | UARTTXD3 | 141 | USB_HOST_D+ |
| 115 | UARTRXD2 | 142 | OTG_USB- |
| 116 | UARTTXD2 | 143 | OTG_USB+ |
| 117 | UARTRXD1 | 144 | USB_ID |

## 核心板引脚定义 4

| 引脚编号 | 信号 | 引脚编号 | 信号 |
| --- | --- | --- | --- |
| 145 | DC5V_OTG | 163 | MCU_SD1_D0 |
| 146 | SEND_INT | 164 | MCU_SD1_D1 |
| 147 | MCU_OTG_PWRON | 165 | MCU_SD1_D2 |
| 148 | GPIOC11 | 166 | MCU_SD1_D3 |
| 149 | GPIOC7 | 167 | MCU_SD0_CD |
| 150 | GPIOC12 | 168 | MCU_SD0_D3 |
| 151 | ADC1 | 169 | MCU_SD0_D2 |
| 152 | ADC0 | 170 | MCU_SD0_D1 |
| 153 | PWM2 | 171 | MCU_SD0_D0 |
| 154 | SPI_WP | 172 | MCU_SD0_CMD |
| 155 | SPIFRM0 | 173 | MCU_SD0_CLK |
| 156 | SPIRXD0 | 174 | RTC |
| 157 | SPITXD0 | 175 | VCC3P3_SYS |
| 158 | SPICLK0 | 176 | MCU_SCL_2 |
| 159 | IR | 177 | MCU_SDA_2 |
| 160 | MCU_SD1_CD | 178 | MCU_SCL_1 |
| 161 | MCU_SD1_CLK | 179 | MCU_SDA_1 |
| 162 | MCU_SD1_CMD | 180 | TOUCH_INT |

## 硬件接口

| 标号 | 名称 | 说明 |
| --- | --- | --- |
| 【1】 | UART4 | 通用串口 4，TTL 电平 |
| 【2】 | UART3 | 通用串口 3，TTL 电平 |
| 【3】 | UART2 | 通用串口 2，TTL 电平 |
| 【4】 | UART1 | 通用串口 1，RS232 电平 |
| 【5】 | UART0 | 调试串口 0，默认调试口，RS232 电平 |
| 【6】 | HDMI 接口 | HDMI 输出接口 |
| 【7】 | LVDS 接口 | 接 LVDS 接口的液晶屏 |
| 【8】 | LCD 接口 | RGB 输出接口 |
| 【9】 | SD 卡，CH0 | SD 卡，使用通道 0 |
| 【10】 | SD 卡，CH1 | SD 卡，使用通道 1 |
| 【11】 | POWER 开关 | 电源控制开关，K5 |
| 【12】 | 按键，返回 | 独立按键，K2 |
| 【13】 | 按键，音量减 | 独立按键，K3 |
| 【14】 | 按键，音量加 | 独立按键，K4 |
| 【15】 | 按键，菜单 | 独立按键，K6 |
| 【16】 | 蜂鸣器 | 支持有源蜂鸣器 |
| 【17】 | 硬复位按钮 | 硬复位，K1 |
| 【18】 | 电池接口 | 单节 4.2V 锂电池接口 |
| 【19】 | 5V 电源接口 | 直流电源输入口 |
| 【20】 | USB OTG | USB OTG 接口 |
| 【21】 | USB HOST1 | HUB 芯片扩展，HOST |
| 【22】 | USB HOST2 | HUB 芯片扩展，HOST |
| 【23】 | 千兆以太网接口 | RT8211E 接口 |
| 【24】 | GPIO 接口 | SPI、UART、ADC 设备扩展 |
| 【25】 | 摄像头接口 | 标准 24PIN 并口摄像头接口 |
| 【26】 | 摄像头接口 | 26PIN MIPI CSI 摄像头接口 |
| 【27】 | 音频接口 | MIC 输入接口、音频输出接口 |
| 【28】 | 锂电池座 | 3V 锂电电池 |
| 【29】 | 红外接收头 | HS0038 红外一体化接收头 |
| 【30】 | 扩展板 GPIO | GPIO、LCD、总线等 |
