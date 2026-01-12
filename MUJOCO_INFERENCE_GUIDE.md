# MuJoCo 仿真推理使用指南

## 快速开始

### 方法1: 使用启动脚本（推荐）

```bash
cd /home/yzy/MyProject/rl_sar

# 运行 robot_lab_pose_velocity 策略
./run_mujoco_robot_lab.sh
```

### 方法2: 直接运行

```bash
cd /home/yzy/MyProject/rl_sar

# 格式: ./cmake_build/bin/rl_sim_mujoco <robot_name> <scene_name>
# 使用环境变量 RL_CONFIG 指定配置名称
RL_CONFIG=robot_lab_pose_velocity ./cmake_build/bin/rl_sim_mujoco go2 robot_lab
```

**重要说明：**
- `scene_name` 参数（robot_lab）用于加载 MuJoCo XML 场景文件
- `RL_CONFIG` 环境变量指定策略配置目录名（robot_lab_pose_velocity）
- 如果不设置 `RL_CONFIG`，默认使用 `himloco` 配置

## 重要说明

### 1. 场景名称规则

程序使用两个参数来定位策略：

```
命令格式:
RL_CONFIG=<config_name> ./cmake_build/bin/rl_sim_mujoco <robot_name> <scene_name>

示例:
RL_CONFIG=robot_lab_pose_velocity ./cmake_build/bin/rl_sim_mujoco go2 robot_lab

参数说明:
- robot_name: go2 (机器人型号)
- scene_name: robot_lab (MuJoCo XML 场景文件名)
- RL_CONFIG: robot_lab_pose_velocity (策略配置目录名)

会加载:
- MuJoCo 场景: rl_sar_zoo/go2_description/mjcf/robot_lab.xml
- 策略配置: policy/go2/robot_lab_pose_velocity/config.yaml
- 策略模型: policy/go2/robot_lab_pose_velocity/policy.pt
```

### 2. 目录结构

```
policy/go2/
├── robot_lab_pose_velocity/   ← 你的策略
│   ├── config.yaml            ← 配置文件
│   └── policy.pt              ← TorchScript 模型
├── himloco/                   ← 其他策略示例
└── base.yaml                  ← 基础配置
```

### 3. 配置文件关键参数

`policy/go2/robot_lab_pose_velocity/config.yaml`:

```yaml
go2/robot_lab_pose_velocity:  # 注意：这里的 key 包含完整路径
  robot_name: go2
  scene_name: robot_lab_pose_velocity
  
  model_name: "policy.pt"
  num_observations: 48  # 重要：必须与训练时一致！
  
  # 观测空间 (无 base_lin_vel，无 height_scan)
  # ang_vel (3) + gravity_vec (3) + commands (6) + dof_pos (12) + dof_vel (12) + actions (12) = 48
  observations: ["ang_vel", "gravity_vec", "commands", "dof_pos", "dof_vel", "actions"]
  
  # PD 控制增益
  rl_kp: [20.0, 20.0, ...]  # 12个值
  rl_kd: [0.5, 0.5, ...]    # 12个值
  
  # 动作缩放 (hip关节更小，其他关节正常)
  action_scale: [0.125, 0.25, 0.25,  # FR: hip, thigh, calf
                 0.125, 0.25, 0.25,  # FL
                 0.125, 0.25, 0.25,  # RR
                 0.125, 0.25, 0.25]  # RL
  
  # 观测缩放
  lin_vel_scale: 2.0
  ang_vel_scale: 0.25
  dof_pos_scale: 1.0
  dof_vel_scale: 0.05
  commands_scale: [1.0, 1.0, 1.0, 1.0, 1.0, 1.0]  # 6维命令
```

**关键说明：**
- `commands` 包含 **6 维**: [lin_vel_x, lin_vel_y, ang_vel_z, height, roll, pitch]
- 这是 velocity_pose 任务特有的，比普通 velocity 任务多了 3 维（height, roll, pitch）
- 训练时 `base_lin_vel` 和 `height_scan` 被设置为 None，所以不在观测空间中

## 控制说明

### 键盘控制

```
移动控制:
  W - 前进
  S - 后退
  A - 左移/左转
  D - 右移/右转
  Q - 原地左转
  E - 原地右转

功能键:
  空格    - 停止移动
  R       - 重置机器人
  ESC/Q   - 退出仿真
  
视角控制:
  鼠标左键+拖动  - 旋转视角
  鼠标右键+拖动  - 平移视角
  滚轮          - 缩放
```

### 游戏手柄控制（如果连接）

```
左摇杆  - 前进/后退 + 左右移动
右摇杆  - 转向
按钮    - 特殊功能（根据代码配置）
```

## 测试不同环境

根据你的训练环境，可以测试不同的地形配置：

### Rough 环境（默认）

```bash
# MuJoCo 会加载对应的 rough 地形
./cmake_build/bin/rl_sim_mujoco go2 robot_lab
```

如果你想要平坦地面或其他环境，需要修改 MuJoCo XML 场景文件。

## 性能监控

运行时终端会输出：

```
[INFO] Frequency: 50.2 Hz        ← 控制频率
[INFO] Command: vx=0.5 vy=0.0    ← 速度命令
[INFO] Actions: [0.12, -0.05...] ← 策略输出
```

## 常见问题

### 1. 找不到策略文件

```bash
# 错误: 找不到 policy/go2/robot_lab_pose_velocity/policy.pt

# 解决: 复制模型文件到正确位置
cp robot_lab/logs/rsl_rl/.../exported/policy.pt \
   policy/go2/robot_lab_pose_velocity/policy.pt
```

### 2. 观测维度不匹配

```
错误: Expected observation size 45, but got 48

解决: 检查 config.yaml 中的 num_observations 和 observations 列表
确保与训练时完全一致
```

### 3. 机器人倒地/不稳定

```yaml
# 调整参数 (config.yaml)

# 方案1: 降低动作缩放
action_scale: [0.1, 0.2, 0.2, ...]  # 从 0.125/0.25 降低

# 方案2: 增加阻尼
rl_kd: [1.0, 1.0, 1.0, ...]  # 从 0.5 增加到 1.0

# 方案3: 调整 PD 增益
rl_kp: [15.0, 15.0, ...]  # 从 20.0 降低到 15.0
```

### 4. 控制频率低

```
现象: Frequency < 40 Hz

原因: 
- 模型推理太慢
- CPU 负载高

解决:
- 关闭其他程序
- 使用性能模式
- 考虑使用 ONNX 后端
```

## 调试技巧

### 1. 详细日志

```bash
# 设置日志级别
export RL_SAR_LOG_LEVEL=DEBUG
./cmake_build/bin/rl_sim_mujoco go2 robot_lab
```

### 2. 慢动作模式

在 MuJoCo 窗口中按 `Ctrl+减号` 可以降低仿真速度，便于观察细节。

### 3. 录制视频

MuJoCo 支持内置录屏：
- 按 `F12` 开始/停止录制
- 视频保存在当前目录

## 性能对比测试

如果你有多个策略，可以对比测试：

```bash
# 测试策略 A
./cmake_build/bin/rl_sim_mujoco go2 robot_lab
# 观察表现，记录数据

# 测试策略 B（如果有）
./cmake_build/bin/rl_sim_mujoco go2 himloco
# 对比差异
```

## 下一步

1. **仿真测试通过** → 继续实机部署（参考 DEPLOYMENT_GUIDE.md）
2. **仿真表现不佳** → 返回 robot_lab 重新训练，调整参数
3. **Sim-to-Real** → 分析差异，进行 domain randomization

## 参考文档

- **观测空间说明**: [OBSERVATION_SPACE.md](OBSERVATION_SPACE.md)
- **模型推理指南**: [ISAACLAB_MODEL_INFERENCE_GUIDE.md](ISAACLAB_MODEL_INFERENCE_GUIDE.md)
- **实机部署指南**: [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md)

---

**祝测试顺利！** 🚀
