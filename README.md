# pad15
这是一个带有旋钮、摇杆和触摸板的有线蓝牙双模多功能小键盘项目，基于ZMK框架v0.3版本开发。

This is a wireless dual-mode multifunction mini-keyboard project with EC11 knob XBOX joystick and a 4 channels touchpad. Coding based on ZMK firmware v0.3.

<img width="767" height="729" alt="image" src="https://github.com/user-attachments/assets/d6e857e1-50d8-4e14-8bf3-5f4a70986501" />

# ZMK编写

本文的内容具有时效性，以本文最后的编辑时间为准。

将买来的promicro（supermini） nrf52840 开发板直接接到电脑上，原有的tf2文件是基于C++的，现在我们将着手编译ZMK框架的tf2文件，做好以后直接放入开发板对应的存储设备，可以自动完成烧录，非常方便。

现在的ZMK支持只写配置文件，配置文件的结构如下：
```
pad15/       ← 项目根目录
├── config/
│   ├── pad15.json
│   ├── west.yml
│   └── boards/shields/pad15/
│       ├── Kconfig.defconfig
│       ├── Kconfig.shield
│       ├── pad15.keymap
│       ├── pad15.zmk.yml
│       ├── pad15.dtsi
│       ├── pad15.conf
│       └── pad15.overlay
└── README.md
```
这里建议项目名称用小写，因为在`Kconfig.shield` 中需定义大写的项目名称，其余各处均可使用小写，都用大写可能会导致混乱（比如不小心把该大写的也改成小写了）。

## 各文件的内容解释
暂无

## zmk keymap editor工具
https://nickcoutsos.github.io/keymap-editor/

一个还不错的keymap编辑工具，上手有些难度，因为资料太少了

## workflow自动编译
本项目使用GitHub的工作流自动编译，在此之前了解了别的作者本地编译和代码空间编译，尝试了一下，也没有搞成，太复杂了；后来发现可以通过写yml的方式执行自动编译的工作流，解放双手和颈椎，非常方便。

生成好的tf2文件可以在action中查看。

# 焊接
戴口罩，松香吸多了头晕。

一定要注意二极管和LED的方向，如果是有方向的电解电容，也需要注意方向。

焊锡不要上太多，多了会冒尖、挂到别的焊盘上、短路；上的太少会虚焊没反应；总之是个技术活。

# 外壳的设计和组装
通过SolidWorks设计外壳；还在设计中；
