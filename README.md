# pad15
这是一个带有旋钮、摇杆和触摸板的有线蓝牙双模多功能小键盘项目，基于ZMK框架编写

This is a wireless dual-mode multifunction mini-keyboard project with EC11 knob XBOX joystick and a 4 channels touchpad. Coding baced on ZMK firmware.

<img width="767" height="729" alt="image" src="https://github.com/user-attachments/assets/d6e857e1-50d8-4e14-8bf3-5f4a70986501" />

# ZMK编写

首先本文的内容具有时效性，以本文最后的编辑时间为准。
将买来的promicro（supermini） nrf52840 开发板直接接到电脑上，原有的tf2文件是基于C++的，现在我们将着手编译ZMK框架的tf2文件，做好以后直接放入开发板对应的存储设备，可以自动完成烧录，非常方便。
现在的ZMK支持只写配置文件，配置文件的结构如下：
```
pad15/       ← 项目根目录
├── config/
│   ├── pad15.conf
│   ├── pad15.json
│   ├── pad15.zmk.yml
│   └── boards/shields/pad15/
│       ├── Kconfig.defconfig
│       ├── Kconfig.shield
│       ├── pad15.keymap
│       └── pad15.overlay
├── README.md
└── west.yml
```
这里建议项目名称用小写，因为在`Kconfig.shield` 中需定义大写的项目名称，其余各处均可使用小写，都用大写可能会导致混乱。

随后回到项目仓库主页，新建一个GitHub代码空间，在终端处输入：
```
# 1. 更新系统包
sudo apt-get update && sudo apt-get upgrade -y

# 2. 安装 Python 和 pipx（west 依赖）
sudo apt-get install -y python3 python3-pip python3-venv curl git

# 3. 安装 pipx 并设置环境
python3 -m pip install --user pipx
python3 -m pipx ensurepath

# 4. 安装 west（ZMK 官方构建工具）
pipx install west

# 5. 重载 shell（让 west 命令可用）
source ~/.bashrc

# 初始化 west（把当前仓库当作 zmk-config）
west init -l .

# 更新所有源码和模块（包括你 west.yml 里加的 analog-input-driver）
west update

# 正式编译命令
$ west build -s app -b nice_nano -p -- -DSHIELD=pad15 -DZMK_CONFIG="/workspaces/pad15/config"
```
