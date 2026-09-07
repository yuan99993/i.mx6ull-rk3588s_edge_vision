# 基于 i.MX6ULL + RK3588S 的异构嵌入式边缘智能视觉终端

> **项目定位**：嵌入式 Linux 应用软件 / C++ 系统开发 / 多媒体 / 网络通信 / Edge AI

## 系统架构

```
              OV5640
                 │
                 ▼
        ┌─────────────────┐
        │    i.MX6ULL     │
        │  V4L2 Camera    │
        │  Qt Touch UI    │
        │  Frame Sender   │
        │  Result Receiver│
        └────────┬────────┘
                 │ Ethernet
                 ▼
        ┌─────────────────┐
        │  Orange Pi 5    │
        │   RK3588S       │
        │  Frame Receiver │
        │  RGA            │
        │  RKNN (YOLO11)  │
        │  ByteTrack      │
        └────────┬────────┘
                 │ Detection Result
                 ▼
        ┌─────────────────┐
        │    i.MX6ULL     │
        │  Bounding Boxes │
        │  Track ID       │
        │  Touch Control  │
        └─────────────────┘
```

## 仓库结构

```
├── docs/                # 设计文档、环境记录、Benchmark
├── common/              # 双板共享：协议 / Frame / Detection 定义
├── imx6ull-terminal/    # edge_terminal：V4L2 采集 + Qt 触摸 UI + 网络客户端
└── rk3588-ai-server/    # edge_ai_server：TCP Server + RGA + RKNN + ByteTrack
```

## 状态

- [x] Phase 0：项目初始化
- [ ] M0：硬件基线（i.MX6ULL 环境记录 + OV5640 官方 Demo）
- [ ] M1：自写 V4L2 Capture
- [ ] M2：Qt 触摸相机终端
- [ ] M3：Camera → PC TCP 传输
- [ ] M4：RK3588 YOLO11 NPU 部署
- [ ] M5：PC → RK3588 AI Server
- [ ] M6：双板 AI 闭环
- [ ] M7：RGA + 多线程 Pipeline
- [ ] M8：ByteTrack + 断线重连
- [ ] M9：Benchmark + 稳定演示

详细方案见 [docs/iMX6ULL_OrangePi5_异构边缘视觉系统_三个月最终实施方案.md](docs/iMX6ULL_OrangePi5_异构边缘视觉系统_三个月最终实施方案.md)
