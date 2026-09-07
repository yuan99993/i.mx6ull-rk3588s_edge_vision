# 基于 i.MX6ULL + Orange Pi 5（RK3588S）的异构嵌入式边缘智能视觉系统
## 三个月从现有硬件到稳定演示的完整实施路线

> **项目定位**：嵌入式 Linux 应用软件 / C++ 系统开发 / 多媒体 / 网络通信 / Edge AI  
> **主线刻意偏软件**：不以重写驱动、BSP、DDR 初始化、复杂 Device Tree 适配为核心。  
> **最终目标**：做成一套可稳定演示、可写进校招简历、自己能够解释完整数据链路和软件架构的双板异构视觉系统。
>
> 文档基线日期：2026-09-06

---

# 0. 你的实际硬件基线

本方案不再按“假设硬件”设计，后续全部按下面这套实际设备执行。

## 0.1 i.MX6ULL 侧

开发板：

```text
正点原子 I.MX6U-ALPHA
SoC：NXP i.MX6ULL
核心板：eMMC 版本
eMMC：8 GB
板载 TF / MicroSD 卡槽
```

当前状态：

```text
已经完成过：
- 镜像烧录
- U-Boot 烧录
- Linux 启动验证
- RGB LCD 显示验证
```

因此：

> i.MX6ULL **不再从“完全空板”重复学习 U-Boot**。

本方案会先记录和备份现有可用环境，然后直接把重点转到：

```text
C++
CMake
V4L2
Qt
触摸 UI
Socket
多线程
软件架构
```

---

## 0.2 摄像头

型号确认：

```text
OV5640
```

注意：

> **OV5640 不支持热插拔。**

安装或拔出摄像头前：

```text
必须先给 i.MX6ULL 断电。
```

正点原子官方当前 i.MX6ULL ALPHA 资料明确支持：

```text
OV5640
OV2640
OV7725
```

并且官方出厂系统为 OV5640 提供 V4L2 使用链路。

官方资料：

https://wiki.alientek.com/docs/Boards/Linux/IMX6U/I.MX6U%20%E5%BF%AB%E9%80%9F%E4%BD%93%E9%AA%8C%E6%89%8B%E5%86%8C/function%20test/ov5640_test/

---

## 0.3 LCD

你的屏幕：

```text
正点原子 ATK 系列
TFTLCD-v1.3
7 寸
1024 × 600
RGB TFT LCD
电容触摸
```

ALPHA 底板 RGB LCD 接口：

```text
RGB888
```

官方 7 寸 1024×600 屏：

```text
Screen ID：0x02
Touch：ft5x06 / cst340（兼容 ft5x06 驱动）
```

官方资料：

https://wiki.alientek.com/docs/Boards/Linux/IMX6U/I.MX6U%20%E7%A1%AC%E4%BB%B6%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C/baseboard/

LCD/触摸测试：

https://wiki.alientek.com/docs/Boards/Linux/IMX6U/I.MX6U%20%E5%BF%AB%E9%80%9F%E4%BD%93%E9%AA%8C%E6%89%8B%E5%86%8C/function%20test/lcd_test/

---

## 0.4 Orange Pi 5

型号：

```text
Orange Pi 5
普通版
SoC：Rockchip RK3588S
```

当前：

```text
以前运行过 AI 检测项目
说明硬件及 NPU 大概率正常

但本次项目：
从系统重新烧录开始
重新建立完整开发环境
```

Orange Pi 官方构建系统目前明确支持：

```text
RK3588S
Orange Pi 5
```

官方 Build：

https://github.com/orangepi-xunlong/orangepi-build

---

## 0.5 现有存储

```text
MicroSD 8 GB × 1
MicroSD 32 GB × 1
U盘 × 1
读卡器 × 1
```

建议分配：

```text
32 GB MicroSD
    → Orange Pi 5 系统

8 GB MicroSD
    → i.MX6ULL 恢复 / SD 启动 / 实验

i.MX6ULL 正式运行
    → 板载 8 GB eMMC
```

---

# 1. 最终项目一句话定义

项目名称建议：

> **基于 i.MX6ULL + RK3588S 的异构嵌入式边缘智能视觉终端**

英文仓库名称建议：

```text
heterogeneous-edge-vision
```

最终系统：

```text
              OV5640
                 │
                 ▼
        ┌─────────────────┐
        │    i.MX6ULL     │
        │                 │
        │ V4L2 Camera     │
        │ Qt Touch UI     │
        │ Local Preview   │
        │ Frame Sender    │
        │ Result Receiver │
        └────────┬────────┘
                 │
              Ethernet
                 │
                 ▼
        ┌─────────────────┐
        │  Orange Pi 5    │
        │   RK3588S       │
        │                 │
        │ Frame Receiver  │
        │ RGA             │
        │ RKNN Runtime    │
        │ YOLO11          │
        │ ByteTrack       │
        │ AI Result       │
        └────────┬────────┘
                 │
                 │ Detection Result
                 ▼
        ┌─────────────────┐
        │    i.MX6ULL     │
        │                 │
        │ Bounding Boxes  │
        │ Track ID        │
        │ AI Status       │
        │ Touch Control   │
        └─────────────────┘
```

---

# 2. 项目设计原则

整个三个月始终遵守四条原则。

## 原则 1：i.MX6ULL 必须能够脱离 Orange Pi 独立工作

Orange Pi 关机时，i.MX6ULL 仍然能够：

```text
启动
↓
打开摄像头
↓
实时预览
↓
操作触摸屏
↓
拍照
↓
显示 Camera FPS
↓
显示系统/网络状态
```

AI 按钮显示：

```text
AI Offline
```

而不是整个软件不可用。

---

## 原则 2：Orange Pi 是“可插拔 AI 节点”

Orange Pi 开机并连网以后：

```text
i.MX6ULL
   ↓
自动发现/连接 AI Server
   ↓
上传 Frame
   ↓
收到 Detection
   ↓
LCD 叠加检测结果
```

这样整个系统拥有：

```text
Standalone Mode
+
AI Enhanced Mode
```

---

## 原则 3：先实现，再优化

不要：

```text
一开始就 DMA-BUF
一开始就 RTSP
一开始就 H.264
一开始就多核 NPU
```

顺序：

```text
正确性
 ↓
完整链路
 ↓
稳定性
 ↓
测量
 ↓
优化
```

---

## 原则 4：每个阶段都能单独验收

不会出现：

```text
做了一个月
但是没有任何东西能独立运行
```

每个阶段都有一个：

```text
Milestone
```

---

# 3. 最终项目为什么适合你的基础

当前基础：

```text
C：比较熟
C++：基础
CMake：了解一点，但基本没自己写工程
Linux：基础命令
Socket：不熟
Qt：修改 QGC 时接触过 QML
V4L2：未接触
OpenCV：做过简单颜色识别
模型部署：了解概念，没有完整实践
```

所以项目学习顺序专门设计成：

```text
C++
 ↓
CMake
 ↓
V4L2
 ↓
Qt Widgets
 ↓
Thread
 ↓
Socket
 ↓
Protocol
 ↓
RKNN
 ↓
RGA
 ↓
Integration
 ↓
Optimization
```

而不是一次同时学习所有内容。

---

# 4. 项目最终能力画像

完成以后，你应该能把自己的技术栈描述成：

```text
Linux C/C++
├── CMake
├── RAII
├── STL
├── std::thread
├── mutex
├── condition_variable
├── mmap
├── ioctl
├── poll
└── Socket

Embedded Multimedia
├── V4L2
├── YUYV
├── RGB
├── Qt
├── RGA
└── Buffer Pipeline

Networking
├── TCP
├── Message Framing
├── Partial Send/Recv
├── Heartbeat
├── Reconnect
└── Binary Protocol + JSON

Edge AI
├── PyTorch Model
├── ONNX
├── RKNN Toolkit2
├── RKNN Runtime
├── YOLO11
├── INT8 / FP16
└── ByteTrack

Engineering
├── Git
├── Logging
├── Config
├── systemd
├── Benchmark
├── Module Design
└── Failure Recovery
```

---

# 5. 最终软件架构

## 5.1 i.MX6ULL 程序

程序名：

```text
edge_terminal
```

模块：

```text
edge_terminal
│
├── CameraManager
│     └── V4L2Camera
│
├── PreviewManager
│
├── NetworkClient
│     ├── FrameSender
│     └── ResultReceiver
│
├── AIResultManager
│
├── CaptureManager
│     └── Snapshot
│
├── SystemMonitor
│
├── AppStateMachine
│
└── UI
      ├── MainWindow
      ├── CameraWidget
      ├── OverlayWidget
      ├── SettingsPage
      └── SystemPage
```

---

## 5.2 Orange Pi 程序

程序名：

```text
edge_ai_server
```

模块：

```text
edge_ai_server
│
├── TcpServer
│
├── FrameReceiver
│
├── FrameQueue
│
├── ImagePreprocessor
│     ├── CpuPreprocessor
│     └── RgaPreprocessor
│
├── RknnDetector
│
├── ObjectTracker
│
├── ResultServer
│
├── SystemMonitor
│
├── WebDashboard
│
└── Logger
```

---

# 6. i.MX6ULL UI 最终设计

建议使用：

```text
Qt Widgets + C++
```

而不是把 QML 作为主实现。

原因：

你的这次学习重点之一就是：

```text
C++ 应用开发
```

---

# 6.1 主界面

目标布局：

```text
┌──────────────────────────────────────────────┐
│ Edge Vision Terminal      AI ● Connected    │
├──────────────────────────────────────────────┤
│                                              │
│                                              │
│            Camera Preview                    │
│                                              │
│        ┌─────────────────────┐               │
│        │ Person #4  0.93     │               │
│        │                     │               │
│        └─────────────────────┘               │
│                                              │
├──────────────────────────────────────────────┤
│ CAM 10 FPS | AI 9.8 FPS | RTT 65ms | Obj 2 │
├──────────────────────────────────────────────┤
│ [预览] [拍照] [AI] [设置] [系统]            │
└──────────────────────────────────────────────┘
```

屏幕：

```text
1024 × 600
```

---

# 6.2 页面

## Camera 页面

显示：

```text
实时视频
Camera FPS
AI Detection
Track ID
```

---

## Settings 页面

允许触摸修改：

```text
Camera FPS
AI 开关
Confidence
Server IP
Server Port
显示检测框
显示 Track ID
```

---

## System 页面

显示：

```text
i.MX6ULL IP
Orange Pi IP
Network Status
Uptime
Memory
Storage
Camera Status
AI Status
Dropped Frames
```

---

# 6.3 拍照

不做录像。

保留：

```text
拍照
```

原因：

- 实现简单；
- 有实际交互；
- 能学习图片编码和文件系统；
- 不把项目带入 i.MX6ULL 视频编码性能问题。

保存：

```text
/data/captures/
```

文件：

```text
capture_20260906_153012.jpg
```

---

# 7. Orange Pi 5 功能边界

最终做：

```text
TCP AI Server
RGA
RKNN
YOLO11
ByteTrack
Web Dashboard
Logging
Config
systemd
```

不作为主线：

```text
ROS
SLAM
PX4
数据库大系统
复杂前端
人脸识别
自己训练大型模型
```

项目保持独立。

---

# 8. 网络环境：没有路由器怎么办

你的 PC：

```text
Windows 10
R7-5800H
16 GB RAM
```

可以通过 Windows Internet Connection Sharing：

```text
Wi-Fi Internet
      │
      ▼
Windows 10
      │
    Ethernet
      │
      ▼
开发板
```

让开发板访问互联网。

---

# 8.1 但是双板同时联网的问题

如果电脑只有：

```text
1 个 RJ45
```

那么：

```text
Windows PC
    │
    └──── 一根网线 ──── 开发板
```

一次只能物理直连一个设备。

因此强烈建议新增：

```text
5口千兆非管理交换机 × 1
```

价格一般不高。

拓扑：

```text
                Wi-Fi
                  │
                  ▼
             Windows 10
                  │
               Ethernet
                  │
                  ▼
             ┌─ Switch ─┐
             │          │
             ▼          ▼
         i.MX6ULL    Orange Pi 5
```

---

# 8.2 如果暂时不买交换机

开发前期完全没问题。

因为前六周主要是：

```text
先独立开发两块板
```

需要双板联调时再准备交换机。

---

# 9. PC 软件环境

建议继续：

```text
Windows 10
+
VMware
+
Ubuntu VM
+
VS Code
```

---

# 9.1 保留 Ubuntu 20.04

你已经有：

```text
Ubuntu 20.04 VM
```

保留。

主要用于：

```text
i.MX6ULL
旧工具链
Qt 交叉编译
ARM 32-bit 软件
```

---

# 9.2 新建 Ubuntu 22.04 VM

另外新建：

```text
Ubuntu 22.04 x86_64
```

用于：

```text
Orange Pi build
RKNN Toolkit2
Model conversion
RK3588 development
```

Orange Pi 官方 `orangepi-build` 当前明确声明支持 Host：

```text
Ubuntu 22.04
```

GitHub：

https://github.com/orangepi-xunlong/orangepi-build

---

# 9.3 VM 资源建议

16 GB 总内存，不要给 VM 太多。

Ubuntu 22.04：

```text
CPU：4~6 vCPU
RAM：6~8 GB
Disk：80 GB 动态
```

Ubuntu 20.04：

```text
CPU：4 vCPU
RAM：4~6 GB
```

不要同时大量编译两个 VM。

---

# 10. 三个月总路线

建议按 12 周执行。

```text
Week 1
硬件基线 + 环境整理 + CMake

Week 2
C++ 必要知识 + i.MX cross compile

Week 3
OV5640 + V4L2

Week 4
Qt Widgets + 触摸 UI

Week 5
Socket + TCP Protocol

Week 6
Orange Pi 5 重装 + RK3588 环境

Week 7
YOLO → ONNX → RKNN → NPU

Week 8
C++ RKNN Server + TCP Frame

Week 9
双板 AI 闭环

Week 10
RGA + 多线程 Pipeline

Week 11
ByteTrack + 重连 + 状态机 + systemd

Week 12
Benchmark + Web + README + Demo
```

---

# 第一阶段：建立项目基线

# 11. Week 1 — i.MX6ULL 现有系统不要立即重刷

由于你的 i.MX6ULL：

```text
已经能够正常启动
LCD 已验证
U-Boot/镜像已经烧过
```

所以第一步不是：

```text
重新 dd
```

而是：

> **把现有的工作环境保存成 Golden Baseline。**

---

# 11.1 保存板卡信息

i.MX6ULL：

```bash
mkdir -p ~/edge_project_info
```

执行：

```bash
{
    echo "===== DATE ====="
    date

    echo "===== MODEL ====="
    tr -d '\0' < /proc/device-tree/model
    echo

    echo "===== KERNEL ====="
    uname -a

    echo "===== CPU ====="
    cat /proc/cpuinfo

    echo "===== MEMORY ====="
    free -h

    echo "===== STORAGE ====="
    df -h

    echo "===== BLOCK ====="
    lsblk

    echo "===== NETWORK ====="
    ip addr

    echo "===== VIDEO ====="
    ls -l /dev/video* 2>/dev/null

    echo "===== FB ====="
    ls -l /dev/fb* 2>/dev/null
} | tee ~/edge_project_info/imx6ull_baseline.txt
```

保存到 PC。

---

# 11.2 保存 U-Boot 信息

串口进入 U-Boot。

记录：

```bash
version
printenv
```

把输出保存到：

```text
docs/hardware/imx6ull_uboot_env.txt
```

你已经学过这部分，所以不再深入。

---

# 11.3 确认 LCD

执行：

```bash
cat /sys/class/graphics/fb0/virtual_size
```

预期：

```text
1024,600
```

再：

```bash
cat /sys/class/graphics/fb0/bits_per_pixel
```

保存。

---

# 11.4 确认触摸

查看：

```bash
cat /proc/bus/input/devices
```

找到：

```text
ft5x06
```

或：

```text
cst340
```

相关 input node。

查看：

```bash
ls /dev/input/
```

后面 Qt 使用系统已有 input 驱动。

---

# 12. Week 1 — 验证 OV5640

这是第一件真正需要验证的硬件。

---

# 12.1 安全连接

关机：

```bash
poweroff
```

断电。

插 OV5640。

检查：

```text
接口方向
摄像头 PCB 丝印
开发板 CAMERA 丝印
```

正点原子明确提示：

> OV5640 不支持热插拔。

---

# 12.2 开机查看日志

启动：

```bash
dmesg | grep -Ei "ov5640|camera|csi|video"
```

查看：

```bash
ls -l /dev/video*
```

官方文档提示：

```text
OV5640 常见是 /dev/video1
也可能因为驱动加载顺序成为 video2 等。
```

所以不要代码写死 `/dev/video1`。

---

# 12.3 查看格式

如果有：

```bash
v4l2-ctl
```

执行：

```bash
v4l2-ctl --list-devices
```

然后：

```bash
v4l2-ctl \
  --device=/dev/video1 \
  --list-formats-ext
```

按实际节点替换。

官方新版本内核支持：

```text
RGB565
JPEG
```

并针对正点原子屏加入：

```text
480×272
800×480
1024×600
1280×800
```

等采集分辨率。

我们的屏幕就是：

```text
1024×600
```

这对本地预览非常有利。

---

# 12.4 第一个 Camera 验收

首先：

> **只运行正点原子官方 Camera Demo。**

目的：

```text
确认硬件
+
确认驱动
+
确认系统配置
```

看到：

```text
OV5640 → LCD
```

即可。

这一步不要写自己的代码。

---

# 13. 正点原子资料优先级

GitHub：

https://github.com/alientek-openedv/imx6ull-document

这个仓库虽然现在以“存档”为主，但非常适合你当前板子的传统 BSP。

---

## 必读 1

```text
I.MX6U用户快速体验
```

用于：

```text
Camera
LCD
Touch
系统功能
```

---

## 必读 2

```text
I.MX6U 出厂系统Qt交叉编译环境搭建
```

你的 Qt 应用优先使用：

```text
出厂系统已经配置好的 Qt
```

而不是重新移植 Qt。

正点原子文档也明确建议快速开发用户直接搭建出厂 Qt 交叉编译环境。

---

## 必读 3

```text
I.MX6U嵌入式Qt开发指南
```

用于补：

```text
Qt Widgets
signal/slot
QThread
QPainter
```

---

## 选读

```text
I.MX6U嵌入式Linux C应用编程指南
```

重点找：

```text
文件
线程
进程
网络
mmap
ioctl
```

---

## 暂时不要深入

```text
Linux驱动开发指南
Yocto
Linux 6.x 移植
LCD Driver
Camera Driver
```

---

# 14. GitHub：i.MX6ULL 相关

文档：

https://github.com/alientek-openedv/imx6ull-document

正点原子组织：

https://github.com/alientek-openedv

U-Boot：

https://github.com/alientek-openedv/uboot-imx-rel_imx_4.1.15_2.1.0_ga_alientek

Linux 4.1.15：

https://github.com/alientek-openedv/linux-imx-4.1.15-2.1.0

Qt Demo：

https://github.com/alientek-openedv/imx6ull-qtdemo

Qt 教程：

https://github.com/alientek-openedv/Embedded-Qt-Tutorial

---

# 第二阶段：C++ / CMake 必要训练

# 15. Week 1~2 — 为什么先补 C++

你 C 比较熟。

所以不用：

```text
重新学变量
for
if
pointer
```

重点只学“项目立即需要”的 C++。

---

# 15.1 必须掌握

```text
class
constructor
destructor
private/public
reference
const
std::string
std::vector
std::array
std::unique_ptr
std::shared_ptr（理解）
RAII
std::thread
std::mutex
std::condition_variable
std::atomic
```

---

# 15.2 不需要前期深挖

```text
模板元编程
concept
coroutine
复杂 STL allocator
C++20 ranges
```

---

# 15.3 CMake 必须掌握

第一阶段只学：

```cmake
cmake_minimum_required()
project()
add_executable()
add_library()
target_include_directories()
target_link_libraries()
set()
find_package()
```

足够。

---

# 15.4 PC 上先写小工程

创建：

```text
cpp-practice/
├── CMakeLists.txt
├── include/
│   └── timer.hpp
└── src/
    ├── main.cpp
    └── timer.cpp
```

自己完成：

```text
Timer class
```

然后：

```bash
mkdir build
cd build
cmake ..
make -j
```

---

# 15.5 CMake 验收

你必须能自己解决：

```text
header not found
undefined reference
library not found
```

至少知道：

```text
编译错误
```

和：

```text
链接错误
```

区别。

---

# 16. 创建正式 Git 仓库

仓库：

```text
heterogeneous-edge-vision
```

初始：

```text
heterogeneous-edge-vision/
├── README.md
├── docs/
├── common/
├── imx6ull-terminal/
└── rk3588-ai-server/
```

---

# 16.1 Git

```bash
git init
git add .
git commit -m "docs: initialize heterogeneous edge vision project"
```

---

# 16.2 不要第一天创建几十个空文件

随着 Phase 进展再创建。

---

# 第三阶段：自己实现 V4L2

# 17. Week 3 — V4L2 是项目第一个核心

你以前没接触 V4L2。

这一步非常重要。

你需要理解：

```text
OV5640 Sensor
      │
      ▼
Camera Driver
      │
      ▼
CSI
      │
      ▼
V4L2
      │
      ▼
/dev/videoX
      │
      ▼
你的 C++ Application
```

---

# 18. Linux 官方 V4L2 文档

必读入口：

https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html

不用一次全部读。

第一轮只看：

```text
Video Capture
Streaming I/O
mmap
ioctl
Pixel Format
```

---

# 19. 第一版 V4L2 程序

不要 Qt。

程序：

```text
v4l2_capture
```

功能：

```text
打开 Camera
 ↓
配置
 ↓
抓一帧
 ↓
保存
 ↓
退出
```

---

# 19.1 调用流程

```text
open()
   │
   ▼
VIDIOC_QUERYCAP
   │
   ▼
VIDIOC_S_FMT
   │
   ▼
VIDIOC_REQBUFS
   │
   ▼
VIDIOC_QUERYBUF
   │
   ▼
mmap()
   │
   ▼
VIDIOC_QBUF
   │
   ▼
VIDIOC_STREAMON
   │
   ▼
poll()
   │
   ▼
VIDIOC_DQBUF
   │
   ▼
save
   │
   ▼
VIDIOC_QBUF
```

---

# 19.2 V4L2 类

```cpp
class V4L2Camera
{
public:
    V4L2Camera();
    ~V4L2Camera();

    bool openDevice(const std::string& device);
    bool configure(int width,
                   int height,
                   uint32_t pixelFormat);
    bool start();
    bool grab(Frame& frame);
    void stop();

private:
    int fd_;
};
```

重点：

```text
fd_ 生命周期
mmap buffer 生命周期
异常路径释放资源
```

这里就是 RAII 的最好训练。

---

# 20. Frame 定义

共同定义：

```cpp
struct Frame
{
    uint64_t id = 0;
    uint64_t timestamp_us = 0;

    int width = 0;
    int height = 0;

    uint32_t pixel_format = 0;

    uint8_t* data = nullptr;
    size_t size = 0;
};
```

后期再改成更安全的 ownership。

---

# 21. 第一个推荐分辨率

本地 LCD：

```text
1024 × 600
```

但网络 AI 初期不建议 1024×600 raw。

建议 Camera 先测试：

```text
640 × 480 YUYV
```

理由：

```text
数据小
处理简单
网络容易
AI 足够
```

本地显示时可以缩放。

---

# 21.1 后期再测试

```text
1024 × 600
```

做性能对比。

---

# 22. V4L2 验收

程序：

```bash
./v4l2_capture --device /dev/videoX --width 640 --height 480
```

成功生成：

```text
frame_0001.yuv
```

PC：

```bash
ffplay \
  -f rawvideo \
  -pixel_format yuyv422 \
  -video_size 640x480 \
  frame_0001.yuv
```

能正常显示。

---

# 23. 再做连续抓帧

```text
Camera
 ↓
10 FPS
 ↓
1000 Frames
```

统计：

```text
captured
dropped
average FPS
```

不要立刻显示 UI。

---

# 第四阶段：Qt 完整触摸 UI

# 24. Week 4 — Qt Widgets

你以前用过 QML。

这次建议主实现：

```text
Qt Widgets
```

---

# 24.1 学习点

```text
QObject
QWidget
QMainWindow
QImage
QPixmap
QPainter
QPushButton
QLabel
QTimer
QThread
signal
slot
```

---

# 25. 先做纯 UI

第一版不 Camera。

做：

```text
1024×600
```

固定全屏 UI。

页面：

```text
Camera
Settings
System
```

测试：

```text
电容触摸
页面切换
按钮
```

---

# 25.1 UI 验收

i.MX 板上：

```text
触摸按钮
 ↓
切换页面
```

稳定。

---

# 26. Camera 接 Qt

架构：

```text
CameraWorker Thread
       │
       ▼
     QImage
       │
    signal
       │
       ▼
GUI Thread
       │
       ▼
CameraWidget
```

---

# 26.1 绝对不要

```cpp
while (true) {
    camera.grab();
}
```

直接放 Qt Main Thread。

否则：

```text
触摸卡死
界面冻结
```

---

# 27. Image 转换

OV5640 可能得到：

```text
YUYV
```

Qt 显示最终需要：

```text
RGB
```

第一版可以写 CPU 转换。

或者使用现成转换方式。

这一阶段：

> **先保证正确，不追求性能。**

---

# 28. OverlayWidget

最终叠加：

```cpp
QPainter painter(this);
```

画：

```text
bbox
class name
score
track id
```

---

# 29. 独立工作模式

到 Week 4 结束时：

Orange Pi 完全不用开。

i.MX 应该已经能：

```text
Boot
 ↓
edge_terminal
 ↓
Camera Preview
 ↓
Touch UI
 ↓
Snapshot
```

这就是项目第一个真正作品。

---

# 第五阶段：网络与 Socket

# 30. Week 5 — Socket 单独学习

不要直接让 Camera 发网络。

先写：

```text
Hello TCP
```

---

# 30.1 PC Server

PC / Ubuntu：

```text
TCP Server
```

i.MX：

```text
TCP Client
```

发送：

```text
hello from imx6ull
```

Server 返回：

```text
hello from pc
```

---

# 30.2 必须理解

```text
socket()
bind()
listen()
accept()
connect()
send()
recv()
close()
```

---

# 30.3 TCP 最重要概念

TCP 是：

```text
byte stream
```

不是：

```text
message protocol
```

因此：

```cpp
send(packet1)
send(packet2)
```

接收端不保证：

```cpp
recv()
```

刚好一次一个 packet。

---

# 31. 设计应用层协议

我们自己设计：

```text
Edge Vision Protocol
```

版本：

```text
EVP v1
```

---

# 31.1 Header

建议：

```text
Magic          4 bytes
Version        2 bytes
MessageType    2 bytes
PayloadSize    4 bytes
FrameID        8 bytes
Timestamp      8 bytes
Width          4 bytes
Height         4 bytes
PixelFormat    4 bytes
```

---

# 31.2 Message Type

```text
FRAME       = 1
RESULT      = 2
HEARTBEAT   = 3
CONTROL     = 4
STATUS      = 5
```

---

# 31.3 序列化

不要：

```cpp
send(fd, &header, sizeof(header))
```

然后认为一定跨平台稳定。

正式版自己做：

```text
serialize
deserialize
```

考虑：

```text
alignment
padding
endianness
```

---

# 32. recv_exact

自己实现：

```cpp
bool recvExact(int fd, void* buffer, size_t size);
```

循环直到：

```text
size bytes
```

全部收到。

---

# 33. send_all

同理：

```cpp
bool sendAll(int fd, const void* data, size_t size);
```

不要假定：

```text
send()
```

一次发完全部 payload。

---

# 34. Frame 传输

第一版：

```text
640×480
YUYV
```

单帧：

```text
640 × 480 × 2
≈ 614400 bytes
≈ 600 KiB
```

---

# 34.1 带宽

5 FPS：

```text
≈ 3 MB/s
≈ 24 Mbit/s
```

10 FPS：

```text
≈ 6 MB/s
≈ 48 Mbit/s
```

i.MX6ULL 百兆网卡环境下：

> 10 FPS raw YUYV 已经属于需要实际测量的范围。

所以第一版建议：

```text
5 FPS AI 上传
```

本地 UI 仍可以更高 FPS。

---

# 35. 非常关键：Camera FPS 与 AI Upload FPS 分离

例如：

```text
Local Camera Preview
    = 20 FPS

AI Upload
    = 5 FPS
```

这样：

```text
LCD 看起来流畅
```

而网络和 AI 压力较低。

---

# 36. 网络阶段验收

i.MX：

```text
Camera
 ↓
YUYV
 ↓
TCP
```

PC Server：

```text
TCP
 ↓
received.yuv
```

PC：

```bash
ffplay ...
```

能看见 Camera。

此时还没有 Orange Pi AI。

---

# 第六阶段：Orange Pi 5 从零重建

# 37. Week 6 — 清空思路，重新建立 Orange Pi

Orange Pi 以前跑过什么：

```text
本次不依赖。
```

按新项目重新搭建。

---

# 37.1 第一版系统

建议：

```text
Orange Pi 官方 Ubuntu 22.04 系统
```

优先 Vendor 环境。

原因：

```text
RKNPU
RGA
MPP
```

配套最重要。

---

# 37.2 32 GB SD

使用：

```text
32 GB
```

作为第一版系统。

如果后期空间明显不足，再购买：

```text
64/128 GB SD
```

或：

```text
NVMe
```

现在不强制。

---

# 37.3 系统烧录

流程：

```text
Orange Pi 官方下载
 ↓
解压 Image
 ↓
Etcher / Win32 Disk Imager 等
 ↓
32GB SD
 ↓
Orange Pi 5
```

先用官方镜像建立 Golden Baseline。

---

# 38. 第一次启动

执行：

```bash
mkdir -p ~/edge_project_info
```

```bash
{
    echo "===== MODEL ====="
    tr -d '\0' < /proc/device-tree/model
    echo

    echo "===== KERNEL ====="
    uname -a

    echo "===== OS ====="
    cat /etc/os-release

    echo "===== ARCH ====="
    uname -m

    echo "===== CPU ====="
    lscpu

    echo "===== MEM ====="
    free -h

    echo "===== DISK ====="
    df -h

    echo "===== IP ====="
    ip addr
} | tee ~/edge_project_info/orangepi5_baseline.txt
```

---

# 39. 安装基础工具

```bash
sudo apt update
```

```bash
sudo apt install -y \
  git \
  gcc \
  g++ \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  gdb \
  vim \
  htop \
  tree \
  wget \
  curl \
  unzip \
  openssh-server \
  iperf3
```

---

# 40. SSH

```bash
sudo systemctl enable ssh
sudo systemctl start ssh
```

PC：

```bash
ssh user@ORANGE_PI_IP
```

后面：

```text
VS Code Remote SSH
```

---

# 41. 检查 NPU

```bash
sudo dmesg | grep -Ei "rknpu|npu"
```

```bash
find /usr -name "librknnrt.so*" 2>/dev/null
```

```bash
ldconfig -p | grep -i rknn
```

记录版本。

---

# 42. Orange Pi Build

Ubuntu 22.04 VM：

```bash
git clone https://github.com/orangepi-xunlong/orangepi-build.git
```

```bash
cd orangepi-build
sudo ./build.sh
```

目的：

> **完整 build 一次系统，理解构建链。**

不是为了立即替换稳定开发镜像。

---

# 42.1 U-Boot 学习深度

你最终只要能解释：

```text
BootROM
 ↓
Rockchip Boot Stage
 ↓
U-Boot
 ↓
Kernel
 ↓
DTB
 ↓
rootfs
```

并且：

```text
orangepi-build 能构建一次完整 image
```

即可。

不要把三个月消耗在 RK3588 启动链细节。

---

# 第七阶段：AI 部署

# 43. Week 7 — AI 的学习范围

你的目标：

```text
理解并实际走一次部署流程。
```

不是训练算法专家。

---

# 43.1 模型路线

```text
YOLO11n
   │
   ▼
PyTorch .pt
   │
   ▼
ONNX
   │
   ▼
RKNN Toolkit2
   │
   ▼
.rknn
   │
   ▼
RKNN Runtime
   │
   ▼
RK3588S NPU
```

---

# 44. RKNN Toolkit2

官方：

https://github.com/airockchip/rknn-toolkit2

当前官方说明：

```text
RKNN Toolkit2
→ PC 模型转换、性能评估

RKNN Toolkit Lite2
→ 板端 Python

RKNN Runtime
→ 板端 C/C++
```

支持：

```text
RK3588 Series
```

---

# 45. RKNN Model Zoo

官方：

https://github.com/airockchip/rknn_model_zoo

YOLO11：

https://github.com/airockchip/rknn_model_zoo/blob/main/examples/yolo11/README.md

当前 YOLO11 示例支持：

```text
RK3588
FP16 / INT8
Python
Linux C/C++
```

---

# 46. 第一遍绝对不要自己训练

直接：

```text
官方 yolo11n.onnx
```

目的：

```text
验证工具链
```

---

# 47. ONNX → RKNN

Ubuntu 22.04 VM。

使用当前 Model Zoo README 指令。

典型：

```bash
cd rknn_model_zoo/examples/yolo11/python
```

```bash
python convert.py \
  ../model/yolo11n.onnx \
  rk3588
```

输出：

```text
yolo11.rknn
```

---

# 47.1 先用 INT8 默认

官方转换默认支持量化路线。

后面再比较：

```text
FP16
INT8
```

---

# 48. 模型版本一定记录

创建：

```text
docs/rknn_environment.md
```

记录：

```text
RKNN Toolkit2:
RKNN Runtime:
RKNPU Driver:
Kernel:
Model Zoo commit:
Model:
Precision:
```

RKNN 问题非常容易来自：

```text
版本不匹配
```

---

# 49. 静态图片第一验收

输入：

```text
bus.jpg
```

输出：

```text
person
bus
bbox
```

Orange Pi NPU 成功运行。

---

# 50. 官方 Demo 成功后立即自己封装

不要长期改官方 `main.cpp`。

自己的：

```cpp
class RknnDetector
{
public:
    bool initialize(const std::string& modelPath);

    std::vector<Detection> detect(const ImageView& image);

    void shutdown();

private:
    rknn_context context_;
};
```

---

# 51. Detection

```cpp
struct Detection
{
    int classId;
    float confidence;

    float x1;
    float y1;
    float x2;
    float y2;
};
```

---

# 51.1 坐标统一为 normalized

最终结果：

```text
0.0 ~ 1.0
```

例如：

```text
x1 = 0.20
y1 = 0.14
x2 = 0.53
y2 = 0.81
```

因为：

```text
Camera
AI Input
LCD
```

分辨率不同。

---

# 第八阶段：Orange Pi AI Server

# 52. Week 8 — 先接 PC 图片

Server：

```text
edge_ai_server
```

先：

```text
PC
 ↓
发送一张 JPG
 ↓
Orange Pi
 ↓
RKNN
 ↓
JSON
 ↓
PC
```

不要第一天直接连接 i.MX。

---

# 52.1 这样做的原因

把：

```text
Network
```

和：

```text
i.MX Camera
```

问题解耦。

---

# 53. JSON Result

可以使用：

https://github.com/nlohmann/json

输出：

```json
{
  "frame_id": 1024,
  "inference_ms": 12.3,
  "objects": [
    {
      "class_id": 0,
      "name": "person",
      "score": 0.93,
      "x1": 0.21,
      "y1": 0.13,
      "x2": 0.49,
      "y2": 0.86
    }
  ]
}
```

---

# 54. 第一版 Server 架构

```text
accept
  │
  ▼
receive frame
  │
  ▼
convert image
  │
  ▼
RKNN
  │
  ▼
JSON
  │
  ▼
send result
```

单线程即可。

---

# 第九阶段：双板闭环

# 55. Week 9 — MVP

正式连：

```text
i.MX6ULL
+
Orange Pi 5
```

网络：

```text
i.MX
  │
  ▼
Frame
  │
  ▼
Orange Pi
  │
  ▼
AI
  │
  ▼
Result
  │
  ▼
i.MX LCD
```

---

# 56. i.MX 网络线程

不要让 Camera Thread 阻塞。

推荐：

```text
Camera Thread
     │
     ▼
Latest Frame
     │
     ├────► UI
     │
     ▼
AI Send Queue
     │
     ▼
Network Thread
```

---

# 57. AI 上传帧率

配置：

```text
Preview FPS = 20
AI Upload FPS = 5
```

如果系统余量大：

```text
AI Upload FPS = 8 / 10
```

用 Benchmark 决定。

---

# 58. Result 回传

Orange Pi：

```text
frame_id=123
```

i.MX：

```text
ResultManager
```

保存：

```text
latestResult
```

---

# 59. LCD 画框

UI：

```text
最新 Camera Frame
+
最新 Detection
```

第一版允许：

```text
bbox 与画面有轻微时间差
```

---

# 60. RTT

i.MX 发送 Frame：

```text
frame_id
send_time
```

Result 返回：

```text
now - send_time
```

得到：

```text
AI RTT
```

显示：

```text
RTT 68 ms
```

这比只显示 FPS 更有工程价值。

---

# 61. MVP 验收

达到：

```text
i.MX 开机
Camera Preview 正常
Touch UI 正常

Orange Pi 开机
AI 自动连接

人在 Camera 前
LCD 出现 Person Bounding Box

拔掉 Orange Pi
i.MX 仍继续 Preview
AI Offline

Orange Pi 恢复
自动重连
AI Online
```

如果做到这些：

> 项目主线已经成功。

---

# 第十阶段：RGA

# 62. Week 10 — 为什么引入 RGA

第一版 Orange Pi：

```text
YUYV
 ↓
CPU/OpenCV
 ↓
RGB
 ↓
Resize
 ↓
RKNN
```

接下来换：

```text
YUYV
 ↓
RGA
 ↓
RGB / Resize
 ↓
RKNN
```

---

# 63. 官方 RGA

https://github.com/airockchip/librga

当前仓库提供：

```text
Linux aarch64 library
RK3588 support
include/
docs/
samples/
```

C++ API：

```cpp
#include "im2d.hpp"
```

---

# 64. RGA 只先做两个操作

```text
Color Space Conversion
Resize
```

不要一开始用所有 API。

---

# 65. Benchmark

同一帧：

```text
OpenCV
```

记录：

```text
YUYV → RGB
Resize
CPU Usage
```

再：

```text
RGA
```

记录。

表：

| Pipeline | CSC | Resize | CPU | Total |
|---|---:|---:|---:|---:|
| CPU/OpenCV | | | | |
| RGA | | | | |

---

# 第十一阶段：多线程

# 66. Week 10 — Orange Pi Pipeline

串行：

```text
recv
 ↓
RGA
 ↓
RKNN
 ↓
send
```

升级：

```text
Network Receiver
       │
       ▼
   Frame Queue
       │
       ▼
Preprocess Thread
       │
       ▼
  Infer Queue
       │
       ▼
Inference Thread
       │
       ▼
 Result Queue
       │
       ▼
Network Sender
```

---

# 67. BlockingQueue

自己实现：

```cpp
template<typename T>
class BlockingQueue
{
public:
    explicit BlockingQueue(size_t capacity);

    bool push(T item);
    bool pop(T& item);

    void stop();

private:
    std::deque<T> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;

    size_t capacity_;
    bool stopped_;
};
```

---

# 68. 不能无限 Queue

如果：

```text
Camera = 10FPS
AI = 5FPS
```

无限 Queue：

```text
延迟越来越大
```

所以：

```text
capacity = 1~3
```

满时：

```text
Drop Oldest
```

---

# 69. 核心实时系统概念

你面试时应该能说：

> 对实时视觉系统来说，“最新帧”往往比“每一帧都不丢”更重要，因此使用有限长度队列并在过载时丢弃旧帧，保证端到端延迟不会持续累积。

---

# 第十二阶段：ByteTrack

# 70. Week 11 — Tracking

GitHub：

https://github.com/Vertical-Beach/ByteTrack-cpp

只部署：

```text
Orange Pi
```

---

# 71. 链路

```text
YOLO
 ↓
Detection
 ↓
ByteTrack
 ↓
Track
```

输出：

```text
track_id
```

---

# 72. Result

```json
{
  "frame_id": 1500,
  "objects": [
    {
      "track_id": 7,
      "class": "person",
      "score": 0.95,
      "x1": 0.21,
      "y1": 0.12,
      "x2": 0.51,
      "y2": 0.89
    }
  ]
}
```

---

# 73. i.MX

只负责：

```text
显示 ID
```

不做 Tracking。

---

# 第十三阶段：断线重连

# 74. Network State

i.MX：

```text
DISCONNECTED
 ↓
CONNECTING
 ↓
CONNECTED
 ↓
AI_READY
```

---

# 75. Reconnect

失败：

```text
sleep 1s
 ↓
retry
```

不要快速 busy loop。

---

# 76. Heartbeat

每：

```text
1 second
```

发送：

```text
HEARTBEAT
```

---

# 76.1 Heartbeat i.MX → Server

```json
{
  "camera": true,
  "preview_fps": 19.8,
  "upload_fps": 5.0
}
```

---

# 76.2 Server → i.MX

```json
{
  "ai": true,
  "model": "yolo11n_int8",
  "temperature": 54.0
}
```

---

# 第十四阶段：配置

# 77. 不要把 IP 写死在代码

i.MX：

```text
config/terminal.json
```

```json
{
  "camera": {
    "device": "/dev/video1",
    "width": 640,
    "height": 480,
    "preview_fps": 20,
    "ai_fps": 5
  },
  "ai": {
    "enabled": true,
    "host": "192.168.137.20",
    "port": 9000
  }
}
```

实际 video node 以后按板子确定。

---

# 78. Orange Pi

```text
config/server.json
```

```json
{
  "server": {
    "port": 9000
  },
  "model": {
    "path": "models/yolo11n_int8.rknn",
    "confidence": 0.5
  },
  "pipeline": {
    "queue_size": 2
  }
}
```

---

# 第十五阶段：systemd

# 79. Orange Pi

```text
edge-ai.service
```

目标：

```text
Boot
 ↓
Network
 ↓
AI Server
```

---

# 80. i.MX

如果当前 rootfs 使用 systemd：

```text
edge-terminal.service
```

否则使用当前系统对应的 startup 方式。

---

# 第十六阶段：Web Dashboard

# 81. Week 11~12 可选但推荐

Orange Pi Web：

```text
不是主操作界面
```

用于：

```text
Debug
Monitoring
Demo
```

---

# 82. 显示

```text
Client Connected
Frame RX FPS
AI FPS
Inference ms
RGA ms
RTT
Queue
Drops
CPU
Memory
Temperature
Objects
```

---

# 83. 不做复杂 React

第一版：

```text
简单 HTML
+
轻量 HTTP server
```

即可。

重点不是前端。

---

# 第十七阶段：日志

# 84. spdlog

GitHub：

https://github.com/gabime/spdlog

Orange Pi 推荐使用。

---

# 85. 日志示例

```text
[INFO] AI model loaded
[INFO] client 192.168.x.x connected
[INFO] frame=1025 recv=614400
[WARN] frame queue full, drop 1026
[WARN] client disconnected
[INFO] client reconnected
```

---

# 86. i.MX

Qt 侧可以先做简单 Logger。

不要求一定把 spdlog 交叉编译到老系统。

---

# 第十八阶段：后期通信升级

# 87. 为什么第一版 Raw YUYV

因为：

```text
最容易看懂
最容易 Debug
协议简单
不需要 Codec
```

---

# 88. 第一升级：JPEG

如果网络成为瓶颈：

```text
YUYV
 ↓
JPEG
 ↓
TCP
```

优点：

```text
网络流量明显降低
```

代价：

```text
i.MX CPU Encode
Orange Pi Decode
Latency
```

必须 Benchmark。

---

# 89. 第二升级：H.264 / RTSP

只有前面全部完成以后再做。

未来：

```text
i.MX / Video Source
 ↓
H264
 ↓
RTSP/TCP
 ↓
Orange Pi
 ↓
MPP
 ↓
NV12
 ↓
RGA
 ↓
RKNN
```

---

# 90. MPP

官方：

https://github.com/rockchip-linux/mpp

MPP 支持 RK3588 系列。

它是：

```text
Rockchip Media Process Platform
```

负责：

```text
Hardware Video Decode / Encode
```

---

# 91. MPP 不属于 MVP

三个月如果时间紧：

```text
可以不做。
```

RGA + RKNN + TCP 已足够作为稳定项目。

---

# 第十九阶段：高级优化

# 92. DMA-BUF / Zero Copy

如果 Week 12 还有余力：

了解：

```text
DMA-BUF
```

目标：

```text
减少 memcpy
```

---

# 92.1 先测 memcpy

不要为了关键词强行做。

先统计：

```text
Frame copy ms
```

如果不是瓶颈：

```text
Zero Copy 不是当前最高优先级
```

---

# 93. NPU 多核

RK3588 Series 支持更复杂的 NPU 调度能力。

但：

```text
只在单实例已经稳定
```

以后研究。

---

# 94. AI 模型学习深度

只需要掌握以下流程。

---

# 94.1 PyTorch

理解：

```text
.pt
```

表示模型权重/模型。

---

# 94.2 ONNX

理解：

```text
通用模型交换格式
```

---

# 94.3 RKNN

理解：

```text
Rockchip NPU 部署格式
```

---

# 94.4 Quantization

理解：

```text
FP32
FP16
INT8
```

以及：

```text
速度
内存
精度
```

之间 trade-off。

---

# 94.5 不需要

```text
自己从零实现 CNN
深入反向传播数学
训练几百 epoch
```

除非以后求职方向变化。

---

# 95. 三个月具体计划

## Week 1

目标：

```text
硬件基线
OV5640 官方验证
CMake Hello World
Git 仓库
```

成果：

```text
Camera 官方 Demo 正常
```

---

## Week 2

目标：

```text
C++ 必备
CMake
Cross Compile
```

成果：

```text
自己的 C++ 程序部署到 i.MX
```

---

## Week 3

目标：

```text
V4L2
mmap
poll
```

成果：

```text
自己的 V4L2 C++ Capture
```

---

## Week 4

目标：

```text
Qt Widgets
Touch
Camera Preview
Snapshot
```

成果：

```text
i.MX 独立视觉终端
```

---

## Week 5

目标：

```text
TCP
Protocol
PC Server
```

成果：

```text
Camera Frame 通过网络传到 PC
```

---

## Week 6

目标：

```text
Orange Pi 重装
SSH
开发环境
NPU
orangepi-build
```

成果：

```text
干净、可重复的 RK3588 环境
```

---

## Week 7

目标：

```text
YOLO11
ONNX
RKNN
官方 C++ Demo
```

成果：

```text
NPU 检测 bus.jpg
```

---

## Week 8

目标：

```text
自己的 RknnDetector
TCP AI Server
```

成果：

```text
PC → OrangePi → AI → JSON
```

---

## Week 9

目标：

```text
双板联调
```

成果：

```text
LCD AI bbox
```

---

## Week 10

目标：

```text
RGA
Multithreading
Bounded Queue
```

成果：

```text
稳定实时 Pipeline
```

---

## Week 11

目标：

```text
ByteTrack
Heartbeat
Reconnect
Config
systemd
```

成果：

```text
产品化稳定性
```

---

## Week 12

目标：

```text
Benchmark
Web
README
Demo
Resume
```

成果：

```text
稳定校招作品
```

---

# 96. 每周建议学习比例

因为你是：

```text
边学边做
```

建议：

```text
30% 原理
60% 实现
10% 文档整理
```

不要：

```text
80% 视频教程
20% 写代码
```

---

# 97. 项目仓库最终结构

```text
heterogeneous-edge-vision/
│
├── README.md
├── LICENSE
├── .gitignore
│
├── docs/
│   ├── 00_hardware.md
│   ├── 01_imx_environment.md
│   ├── 02_v4l2.md
│   ├── 03_qt_ui.md
│   ├── 04_protocol.md
│   ├── 05_rk3588_environment.md
│   ├── 06_rknn.md
│   ├── 07_rga.md
│   ├── 08_pipeline.md
│   ├── 09_benchmark.md
│   └── troubleshooting.md
│
├── common/
│   ├── include/
│   │   ├── protocol.hpp
│   │   ├── detection.hpp
│   │   └── frame.hpp
│   └── src/
│       └── protocol.cpp
│
├── imx6ull-terminal/
│   ├── CMakeLists.txt
│   ├── config/
│   │   └── terminal.json
│   ├── include/
│   └── src/
│       ├── main.cpp
│       ├── camera/
│       │   ├── v4l2_camera.cpp
│       │   └── camera_worker.cpp
│       ├── network/
│       │   └── ai_client.cpp
│       ├── core/
│       │   ├── state_machine.cpp
│       │   └── result_manager.cpp
│       └── ui/
│           ├── main_window.cpp
│           ├── camera_widget.cpp
│           ├── overlay_widget.cpp
│           └── settings_page.cpp
│
├── rk3588-ai-server/
│   ├── CMakeLists.txt
│   ├── config/
│   │   └── server.json
│   ├── include/
│   └── src/
│       ├── main.cpp
│       ├── network/
│       │   └── tcp_server.cpp
│       ├── image/
│       │   ├── cpu_preprocessor.cpp
│       │   └── rga_preprocessor.cpp
│       ├── inference/
│       │   └── rknn_detector.cpp
│       ├── tracking/
│       │   └── object_tracker.cpp
│       ├── pipeline/
│       │   └── ai_pipeline.cpp
│       └── monitor/
│           └── system_monitor.cpp
│
├── pc-debug-client/
│
├── scripts/
│   ├── deploy_imx.sh
│   ├── deploy_rk3588.sh
│   └── benchmark.sh
│
└── third_party/
```

---

# 98. Git Commit 路线

```text
docs: initialize system architecture

build: add imx6ull terminal cmake project

feat: add v4l2 camera capture

feat: add qt camera preview

feat: add touch settings page

feat: add snapshot support

feat: define edge vision protocol

feat: add tcp frame sender

build: initialize rk3588 ai server

feat: add rknn yolo11 detector

feat: add tcp frame receiver

feat: return json detection result

feat: render ai result on lcd

feat: add rga preprocessing

feat: add bounded blocking queue

feat: add multithreaded ai pipeline

feat: integrate bytetrack

feat: add connection heartbeat

feat: add automatic reconnect

feat: add systemd service

perf: add end-to-end benchmark

docs: add final performance results
```

---

# 99. Benchmark 必须做

这是这个项目从：

```text
能运行 Demo
```

升级到：

```text
校招项目
```

的关键。

---

# 100. 网络 Benchmark

| Camera | Upload | Bandwidth | RTT | Drop |
|---|---:|---:|---:|---:|
| 640×480 YUYV | 5 FPS | | | |
| 640×480 YUYV | 8 FPS | | | |
| 640×480 YUYV | 10 FPS | | | |
| 1024×600 YUYV | 5 FPS | | | |

---

# 101. AI Benchmark

| Model | Precision | Infer ms | FPS |
|---|---|---:|---:|
| YOLO11n | FP16 | | |
| YOLO11n | INT8 | | |

---

# 102. Preprocess Benchmark

| Backend | CSC | Resize | CPU |
|---|---:|---:|---:|
| CPU/OpenCV | | | |
| RGA | | | |

---

# 103. Architecture Benchmark

| Architecture | AI FPS | RTT | Drop |
|---|---:|---:|---:|
| Single Thread | | | |
| Multi Thread | | | |

---

# 104. 运行稳定性

至少完成：

```text
连续运行 1 小时
```

记录：

```text
crash
memory
temperature
disconnect
dropped frame
```

最终再挑战：

```text
4 小时
```

---

# 105. UI Demo Checklist

最终触摸屏：

```text
[ ] 开始/停止 Preview
[ ] Snapshot
[ ] AI On/Off
[ ] Settings
[ ] System Page
[ ] Camera FPS
[ ] AI FPS
[ ] RTT
[ ] Network Status
[ ] Bounding Box
[ ] Confidence
[ ] Track ID
```

---

# 106. 故障 Demo

演示时主动：

```text
拔掉 Orange Pi 网线
```

LCD：

```text
AI Disconnected
```

Preview：

```text
继续工作
```

重新插：

```text
AI Reconnecting
AI Connected
```

这个效果非常值得做。

---

# 107. i.MX6ULL 面试知识点

项目结束以后应该能回答：

### V4L2 是什么？

### `/dev/videoX` 是什么？

### 为什么用 ioctl？

### mmap Capture 怎么工作？

### QBUF/DQBUF 是什么？

### 为什么 GUI 与 Camera 不放同一线程？

### Qt signal/slot 跨线程怎么使用？

### YUYV 和 RGB 有什么区别？

---

# 108. 网络面试知识点

### TCP 与 UDP 区别？

### TCP 为什么没有 Message Boundary？

### 为什么 send/recv 可能不完整？

### 怎么解决应用层粘包？

### 为什么使用 frame_id？

### 为什么需要 Heartbeat？

### 断线怎么重连？

### Queue 为什么不能无限？

---

# 109. Orange Pi 面试知识点

### RK3588S 的 NPU 是什么？

### RKNN Toolkit2 和 Runtime 区别？

### ONNX 是干什么的？

### INT8 为什么更快？

### 为什么需要 RGA？

### RGA 与 OpenCV resize 有什么区别？

### 为什么做 Multi-thread Pipeline？

### 吞吐量和延迟有什么区别？

---

# 110. C++ 面试知识点

### RAII 是什么？

### unique_ptr 和 shared_ptr 区别？

### mutex 是什么？

### condition_variable 为什么存在？

### atomic 能不能替代 mutex？

### move semantics 对 Frame Buffer 有什么价值？

---

# 111. 项目最终演示顺序

## Part 1 — Hardware

镜头：

```text
OV5640
i.MX6ULL
7 inch 1024×600 LCD
Ethernet
Orange Pi 5
```

---

## Part 2 — Standalone

Orange Pi 不开。

i.MX：

```text
Camera Preview
Touch
Snapshot
```

---

## Part 3 — AI Online

Orange Pi 开机。

LCD：

```text
AI Connected
```

目标进入画面：

```text
Person #1
```

---

## Part 4 — Monitoring

Orange Pi Web：

```text
AI FPS
Inference ms
RGA ms
RTT
```

---

## Part 5 — Failure Recovery

断网：

```text
Offline
```

恢复：

```text
Reconnect
```

---

# 112. 最终简历写法

完成并拿到真实性能数据后可以写：

> **基于 i.MX6ULL + RK3588S 的异构嵌入式边缘智能视觉系统**  
> 设计 i.MX6ULL 视觉终端与 RK3588S AI 计算节点的双机软件架构；基于 Linux V4L2 mmap 实现 OV5640 图像采集，并采用 Qt/C++ 在 1024×600 电容触摸屏实现实时预览、交互配置和 AI 检测结果叠加；设计基于 TCP 的视频帧与检测结果协议，实现消息分帧、frame_id、心跳及断线自动重连；在 RK3588S 上使用 RKNN Runtime 将 YOLO11 部署至 NPU，并利用 RGA 完成图像格式转换与缩放，集成 ByteTrack 实现目标持续跟踪；采用有界生产者-消费者队列构建多线程实时处理 Pipeline，并对网络吞吐、端到端 RTT、图像预处理及 NPU 推理耗时进行性能分析。

最后一定加入真实数据，例如：

```text
端到端 RTT 从 xxx ms 优化至 xxx ms
RGA 预处理从 xxx ms 降至 xxx ms
系统稳定运行 xxx 小时
```

---

# 113. 项目完成标准

## 基础完成

```text
i.MX Camera
Qt LCD
TCP
RKNN YOLO
bbox
```

---

## 校招稳定版本

必须：

```text
V4L2
Qt Touch UI
TCP Protocol
Auto Reconnect
RKNN
RGA
Multithread
Bounded Queue
ByteTrack
Benchmark
systemd
README
Demo Video
```

---

## 进阶

可选：

```text
JPEG
MPP
RTSP
DMA-BUF
Zero Copy
Multi NPU Core
```

---

# 114. 目前不需要购买的东西

不需要：

```text
USB Camera
新 CSI Camera
新 LCD
传感器
额外 MCU
```

你的：

```text
OV5640
+
RGB LCD
```

已经够。

---

# 115. 建议新增的硬件

第一优先：

```text
5口千兆交换机
```

第二：

```text
至少再准备一根网线
```

第三：

```text
Orange Pi 5 散热片 + 风扇
```

如果已有，不必买。

---

# 116. Orange Pi SD

现有：

```text
32 GB
```

先用。

如果后面：

```text
剩余 < 5 GB
```

再考虑升级。

---

# 117. 正式开工时第一周 Checklist

## i.MX

```text
[ ] 保存现有系统信息
[ ] 保存 U-Boot env
[ ] 确认 1024×600
[ ] 确认 Touch Device
[ ] 断电安装 OV5640
[ ] dmesg 看 OV5640
[ ] 确认 /dev/videoX
[ ] v4l2-ctl 查看格式
[ ] 官方 Camera Demo
```

---

## PC

```text
[ ] 保留 Ubuntu 20.04
[ ] 新建 Ubuntu 22.04
[ ] Git
[ ] VS Code
[ ] SSH
[ ] CMake
```

---

## Git

```text
[ ] 建仓库
[ ] README
[ ] docs
[ ] 首次 commit
```

---

# 118. 第二周 Checklist

```text
[ ] C++ class
[ ] constructor/destructor
[ ] RAII
[ ] vector
[ ] unique_ptr
[ ] CMake executable
[ ] library
[ ] cross compile
[ ] deploy to i.MX
```

---

# 119. 第三周 Checklist

```text
[ ] open video device
[ ] QUERYCAP
[ ] S_FMT
[ ] REQBUFS
[ ] mmap
[ ] STREAMON
[ ] poll
[ ] DQBUF
[ ] QBUF
[ ] save yuv
[ ] 1000 frame test
```

---

# 120. 第四周 Checklist

```text
[ ] Qt Widgets app
[ ] fullscreen 1024×600
[ ] Touch
[ ] Camera worker
[ ] Preview
[ ] FPS
[ ] Snapshot
[ ] Settings
```

---

# 121. 第五周 Checklist

```text
[ ] socket
[ ] bind/listen/accept
[ ] connect
[ ] sendAll
[ ] recvExact
[ ] Header
[ ] Frame ID
[ ] raw frame → PC
```

---

# 122. 第六周 Checklist

```text
[ ] Orange Pi official image
[ ] 32GB SD
[ ] SSH
[ ] System info
[ ] RKNPU
[ ] RKNN Runtime
[ ] orangepi-build clone
[ ] Ubuntu 22.04 build environment
```

---

# 123. 第七周 Checklist

```text
[ ] RKNN Toolkit2
[ ] Model Zoo
[ ] YOLO11
[ ] ONNX
[ ] RKNN conversion
[ ] bus.jpg
[ ] C++ demo
[ ] RknnDetector
```

---

# 124. 第八/九周 Checklist

```text
[ ] TCP AI Server
[ ] Receive frame
[ ] RKNN
[ ] JSON
[ ] Result to i.MX
[ ] LCD bbox
[ ] RTT
[ ] AI on/off
[ ] Offline mode
```

---

# 125. 第十/十一周 Checklist

```text
[ ] RGA
[ ] Benchmark
[ ] BlockingQueue
[ ] multithread
[ ] drop oldest
[ ] ByteTrack
[ ] Heartbeat
[ ] Reconnect
[ ] Config
[ ] systemd
```

---

# 126. 第十二周 Checklist

```text
[ ] 1h stability
[ ] 4h stability optional
[ ] Web Dashboard
[ ] benchmark tables
[ ] architecture diagram
[ ] README
[ ] Demo Video
[ ] Resume bullets
```

---

# 127. 资料总表 — 必读

## 正点原子 i.MX6ULL 在线资料

https://wiki.alientek.com/docs/Boards/Linux/IMX6U/

---

## ALPHA 硬件资源

https://wiki.alientek.com/docs/Boards/Linux/IMX6U/I.MX6U%20%E7%A1%AC%E4%BB%B6%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C/resource_note/

---

## ALPHA RGB LCD

https://wiki.alientek.com/docs/Boards/Linux/IMX6U/I.MX6U%20%E7%A1%AC%E4%BB%B6%E5%8F%82%E8%80%83%E6%89%8B%E5%86%8C/baseboard/

---

## LCD 触摸测试

https://wiki.alientek.com/docs/Boards/Linux/IMX6U/I.MX6U%20%E5%BF%AB%E9%80%9F%E4%BD%93%E9%AA%8C%E6%89%8B%E5%86%8C/function%20test/lcd_test/

---

## OV5640 测试

https://wiki.alientek.com/docs/Boards/Linux/IMX6U/I.MX6U%20%E5%BF%AB%E9%80%9F%E4%BD%93%E9%AA%8C%E6%89%8B%E5%86%8C/function%20test/ov5640_test/

---

## 正点原子 i.MX6ULL 文档仓

https://github.com/alientek-openedv/imx6ull-document

---

## 正点原子 Qt Demo

https://github.com/alientek-openedv/imx6ull-qtdemo

---

## 正点原子 Embedded Qt Tutorial

https://github.com/alientek-openedv/Embedded-Qt-Tutorial

---

## Linux V4L2 官方文档

https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html

---

# 128. 资料总表 — Orange Pi / RK3588

## Orange Pi Build

https://github.com/orangepi-xunlong/orangepi-build

用途：

```text
Orange Pi 5
RK3588S
U-Boot
Kernel
Image
```

---

## RKNN Toolkit2

https://github.com/airockchip/rknn-toolkit2

用途：

```text
ONNX → RKNN
Board Runtime
C/C++
```

---

## RKNN Model Zoo

https://github.com/airockchip/rknn_model_zoo

---

## YOLO11 RKNN

https://github.com/airockchip/rknn_model_zoo/blob/main/examples/yolo11/README.md

---

## RGA

https://github.com/airockchip/librga

---

## MPP

https://github.com/rockchip-linux/mpp

---

## RKNN + MPP + RGA 完整参考

https://github.com/yingliuzhizhuzed/RK3588_RKNN_MPP_RGA_demo

注意：

```text
只在后期参考其 Pipeline。
不要直接作为自己的项目 Fork。
```

---

## ByteTrack C++

https://github.com/Vertical-Beach/ByteTrack-cpp

---

# 129. 资料总表 — 工程库

JSON：

https://github.com/nlohmann/json

Logging：

https://github.com/gabime/spdlog

Linux：

https://github.com/torvalds/linux

U-Boot：

https://github.com/u-boot/u-boot

---

# 130. GitHub 阅读顺序

## 现在

只看：

```text
imx6ull-document
V4L2 官方文档
```

---

## Camera 跑通以后

看：

```text
imx6ull-qtdemo
Embedded-Qt-Tutorial
```

---

## Orange Pi 开始以后

看：

```text
orangepi-build
rknn-toolkit2
rknn_model_zoo/examples/yolo11
```

---

## AI 跑通以后

看：

```text
librga
ByteTrack-cpp
```

---

## 最后

才看：

```text
mpp
RK3588_RKNN_MPP_RGA_demo
DMA-BUF
```

---

# 131. 最重要的反例

这个项目最容易失败的方式是：

```text
今天看 V4L2
明天看 YOLO
后天看 U-Boot
大后天看 Qt
然后看 Zero Copy
```

最后：

```text
每个都懂一点
但是系统没有跑起来
```

严格按 Milestone。

---

# 132. 最终 Milestone 图

```text
M0
Hardware Baseline
    │
    ▼
M1
OV5640 Official Demo
    │
    ▼
M2
Own V4L2 Capture
    │
    ▼
M3
Qt Touch Camera Terminal
    │
    ▼
M4
Camera → PC TCP
    │
    ▼
M5
RK3588 YOLO11 NPU
    │
    ▼
M6
PC → RK3588 AI Server
    │
    ▼
M7
i.MX → RK3588 → i.MX
    │
    ▼
M8
RGA + Multithread
    │
    ▼
M9
ByteTrack + Reconnect
    │
    ▼
M10
Benchmark + Stable Demo
```

---

# 133. 最终判断标准

项目做完之后，不以：

```text
有没有用了很多库
```

判断价值。

而以你能不能回答：

```text
Camera Frame 怎样进入用户态？
为什么 V4L2 使用 mmap？
为什么 Preview 和 AI Upload 要不同 FPS？
为什么 TCP 要设计 Header？
为什么 recv 不等于 Message？
为什么 Queue 要有容量？
为什么实时视觉允许丢旧帧？
为什么 RGA 能降低 CPU 负担？
RKNN 模型怎么从 ONNX 来？
FP16 与 INT8 的差别是什么？
怎么测端到端 RTT？
为什么 i.MX 脱离 Orange Pi 仍然能工作？
断网后软件状态怎样恢复？
```

如果全部能讲清楚：

> 这个项目已经达到了你当前阶段非常不错的嵌入式 Linux 软件校招作品水平。

---

# 134. 你现在真正应该执行的步骤

现在不要碰 Orange Pi AI。

先做：

```text
1.
给 i.MX6ULL 现有系统做环境记录

2.
断电连接 OV5640

3.
确认 /dev/videoX

4.
v4l2-ctl 看支持格式

5.
运行官方 OV5640 Demo

6.
确认 1024×600 LCD + Camera 工作正常

7.
创建 Git 仓库

8.
开始 C++ / CMake 最小训练

9.
自己写第一个 V4L2 Capture 程序
```

做到第 9 步：

> 再进入 Qt。

---

# 结语

这套项目不会把你的时间主要耗在：

```text
重新造 Linux BSP
重写 Camera Driver
重写 LCD Driver
```

而是把 i.MX6ULL 已经成熟的硬件支持作为底座，真正训练：

```text
Linux C++
V4L2
Qt
Touch UI
Socket
Protocol
Thread
Real-time Pipeline
RKNN
RGA
Edge AI
Software Architecture
Benchmark
```

i.MX6ULL 是一个：

```text
完整、可独立工作的 Camera/HMI Terminal
```

Orange Pi 5 是一个：

```text
可插拔、高性能 Edge AI Compute Node
```

两者组合后形成：

```text
异构嵌入式边缘智能视觉系统
```

这就是后续三个月的唯一主线。
