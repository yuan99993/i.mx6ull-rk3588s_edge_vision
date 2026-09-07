# RKNN 环境版本记录

> 按实施方案第 48 节要求维护。RKNN 问题最常见来源是**版本不匹配**，任何升级/重装后必须更新此表。

## Golden Baseline（2026-09-07）

| 项目 | 值 | 备注 |
|---|---|---|
| 系统镜像 | Orangepi5_1.2.4_ubuntu_jammy_server_linux6.1.99 | 官方百度网盘渠道 |
| 镜像 SHA256 | `d47109d059f47d8a3d33a7639715a82527dadbb46dd78b7d1e06f2e624a42426` | 与官方 .sha 一致 |
| 内核 | 6.1.99-rockchip-rk3588 #1.2.4 | vendor 内核 |
| 系统 | Ubuntu 22.04.5 LTS (jammy) aarch64 | |
| RKNPU 驱动 | 内核内置，`fdab0000.npu` 已绑定，iommu group 0 | dmesg 已确认 |
| RKNN Runtime | librknnrt **2.3.0**（vendor 镜像自带，/usr/lib/librknnrt.so） | PC 端 Toolkit2 需配 2.3.x |
| RGA | rga3 × 2，hw_version 3.0.76831 | |
| GPU | Mali（mali0，/dev/dri card0/card1） | |
| 网络 | eth0 192.168.137.128/24（Windows ICS DHCP） | |

## 待填（Week 7 起）

| 项目 | 值 | 备注 |
|---|---|---|
| RKNN Toolkit2 版本 | TBD | 在 Ubuntu 22.04 VM 安装 |
| rknn_model_zoo commit | TBD | 记录 commit hash |
| 模型 | TBD | 计划 YOLO11n |
| 精度 | TBD | 先 INT8 默认 |

## 硬件基线详情

见 [docs/hardware/orangepi5_baseline.txt](hardware/orangepi5_baseline.txt)
