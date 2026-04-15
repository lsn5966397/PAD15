# Pad15
这是一个带有旋钮、摇杆和触摸板的有线蓝牙双模多功能小键盘项目，基于ZMK框架v0.3版本开发。

This is a wireless dual-mode multifunction mini-keyboard project with EC11 knob XBOX joystick and a 4 channels touchpad. Coding based on ZMK firmware v0.3.

<img width="767" height="729" alt="image" src="https://github.com/user-attachments/assets/d6e857e1-50d8-4e14-8bf3-5f4a70986501" />

# ZMK编写

本文的内容具有时效性，以本文最后的编辑时间为准。

将买来的promicro（supermini） nrf52840 开发板直接接到电脑上，原有的tf2文件是基于C++的，现在我们将着手编译ZMK框架的tf2文件，做好以后直接放入开发板对应的存储设备，可以自动完成烧录，非常方便。

现在的ZMK支持只写配置文件，配置文件的结构如下：
```
Pad15/       ← 项目根目录
├── config/
│   ├── Pad15.json
│   ├── west.yml
│   └── boards/shields/pad15/
│       ├── Kconfig.defconfig
│       ├── Kconfig.shield
│       ├── Pad15.keymap
│       ├── Pad15.zmk.yml
│       ├── Pad15.dtsi
│       ├── Pad15.conf
│       └── Pad15.overlay
└── README.md
```

## 各文件的内容解释
keymap 具体的按键功能和行为，包括宏和组合按键

conf 系统配置，比如蓝牙功率，灯光的最高亮度

overlay 在这里描述设备的配置和矩阵网络形状，还有MCU引脚配置

## 编写中遇到的问题
1、 开机后会莫名卡死，按了没反应，但是灯光还亮\蓝牙没断

这是一个非常典型的 ZMK 固件电源管理（PM）与外设协同工作的问题。既然灯光还在闪烁，说明 MCU 核心并没有完全进入 “深度休眠”（Deep Sleep），而是很可能进入了某种低功耗挂起状态，或者是某个外设（如 I2C 触摸传感器、EC11 或摇杆）在电源状态切换时 “掉链子” 了，导致主循环卡死。

系统电源管理配置有问题，在conf文件中设置 `CONFIG_PM=n` 禁用所有电源管理和自动休眠，然后自行设置休眠，主要是灯光的休眠时长。

2、 LED灯闪烁

扫描频率开太低导致的，在overlay文件中设置灯珠的扫描频率`spi-max-frequency = <3000000>;`就可以纵享丝滑

3、切换层怎么设置好

&to 设置两个相邻且不经常按到的键，避免误触又方便，延迟调到150ms

4、为什么切换到第2 3层时会出现第一层的按键，导致误触

因为你在第二层及以后的层没有全部设置上按键，有的地方是&trans，意思是transparent透明的，而第0层初始层（默认层）是始终激活的，切换到其他层以后系统发现这里没有东西又是透明的，就看到最下边初始层的键位去执行了。

所以想要这里没东西还不透明，改用&none来设置键位。

## zmk keymap editor工具
https://nickcoutsos.github.io/keymap-editor/

一个还不错的keymap编辑工具，上手有些难度，因为资料太少了；我后续要自己出一个zmk设置功能的中文视频

## zmk studio 在线改键工具
https://zmk.studio/

## 键盘测试工具
https://www.zfrontier.com/lab/keyboardTester

## workflow自动编译
本项目使用GitHub的工作流自动编译，在此之前了解了别的作者本地编译和代码空间编译，尝试了一下，也没有搞成，太复杂了；后来发现可以通过写yml的方式执行自动编译的工作流，解放双手和颈椎，非常方便。

生成好的tf2文件可以在action中查看。

# 焊接
戴口罩，松香吸多了头晕。

一定要注意二极管和LED的方向，如果是有方向的电解电容，也需要注意方向。

焊锡不要上太多，多了会冒尖、挂到别的焊盘上、短路；上的太少会虚焊没反应；总之是个技术活。

助焊膏很好用~
# 外壳的设计和组装
通过SolidWorks设计外壳；还在设计中；
