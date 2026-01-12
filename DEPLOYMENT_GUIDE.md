# Unitree Go2 实机部署操作指南

> 📌 本指南假设你已完成软件环境准备（ROS、SDK、依赖库等）
> 
> 从物理连接开始，详细说明每一步操作流程

---

## 📋 目录

- [阶段1: 硬件连接与网络配置](#阶段1-硬件连接与网络配置)
- [阶段2: 验证机器人下位机](#阶段2-验证机器人下位机)
- [阶段3: 模型文件准备与配置](#阶段3-模型文件准备与配置)
- [阶段4: 编译部署程序](#阶段4-编译部署程序)
- [阶段5: 安全测试流程](#阶段5-安全测试流程)
- [阶段6: 实际运行与调优](#阶段6-实际运行与调优)
- [故障排除](#故障排除)

---

## ⚠️ 开始前的安全检查

**请确保以下条件全部满足：**

```
硬件准备：
□ Go2 电池电量 > 50%（建议 > 70%）
□ 遥控器电量充足，功能正常
□ 千兆网线（Cat5e 或 Cat6）
□ 笔记本电脑有千兆网口（或 USB 转接器）

场地准备：
□ 平坦、防滑地面（瓷砖/水泥地）
□ 至少 3m x 3m 空旷空间
□ 清除所有障碍物
□ 有观察员协助

软件准备：
□ Ubuntu 20.04/22.04 系统
□ rl_sar 项目已克隆
□ 依赖库已安装
□ 模型文件已导出（exported/policy.pt）

安全意识：
□ 熟悉遥控器 STOP 按钮位置
□ 了解紧急停止流程
□ 有专人看护机器人
□ 远离机器人运动范围
```

---

## 阶段1: 硬件连接与网络配置

### 1.1 启动 Go2 机器人

**步骤详解：**

1. **找到电源按钮**
   - 位置：机身侧面或背部（参考你的型号说明书）
   - 通常是一个圆形或矩形按钮

2. **长按电源按钮**
   - 按住 3-5 秒
   - 感觉到按钮按下的"咔哒"反馈

3. **观察启动过程**
   ```
   0-10秒：LED 指示灯开始闪烁（通常为红色）
   10-30秒：听到内部风扇启动的声音
   30-60秒：LED 变为绿色或蓝色常亮
   60秒后：系统完全启动，机器人保持初始姿态
   ```

4. **确认启动成功**
   - LED 指示灯稳定显示（绿色/蓝色）
   - 风扇运转正常（持续的低频噪音）
   - 机身温度正常（不烫手）
   - 关节处于自然放松状态

**异常情况处理：**

```
问题1: LED 持续闪烁不稳定
  → 等待更长时间（最多 2 分钟）
  → 如果仍未启动，重新按电源按钮重启
  
问题2: 无任何反应
  → 检查电池是否有电（LED 完全不亮 = 没电）
  → 给电池充电后再试
  
问题3: 发出异常声音
  → 立即关机，检查是否有物理损坏
  → 联系售后支持
```

### 1.2 物理网线连接

**步骤详解：**

1. **找到 Go2 的以太网口**
   ```
   常见位置：
   - 机身右侧面板（大多数型号）
   - 机身尾部（部分型号）
   - 通常有橡胶防尘盖保护
   ```

2. **打开防尘盖**
   - 轻轻向外拉防尘盖
   - 不要用力过猛，避免撕裂
   - 看到标准 RJ45 以太网口

3. **连接网线到 Go2**
   - 将网线水晶头对准网口
   - 确保方向正确（卡扣朝下）
   - 用力按压直到听到"咔哒"声
   - **关键：必须听到卡扣锁定的声音！**

4. **检查 Go2 端连接**
   - 轻轻拉扯网线，确保不会脱落
   - 观察网口指示灯：
     - ✅ 绿灯常亮 = 连接建立
     - ✅ 橙灯闪烁 = 数据传输
     - ❌ 无灯 = 连接失败，重新插拔

5. **连接网线到电脑**
   - 找到笔记本的以太网口
     - 如果没有，使用 **USB 3.0 转千兆网口转接器**
     - ⚠️ 注意：必须是千兆转接器！百兆会导致延迟过高
   
   - 将网线另一端插入电脑网口
   - 同样确保听到"咔哒"锁定声
   - 检查电脑网口指示灯

6. **验证物理连接**
   ```bash
   # 打开终端，查看网络接口
   ifconfig
   # 或
   ip link show
   
   # 寻找新增的网络接口，例如：
   # enp0s31f6  (有线网卡)
   # eth0       (有线网卡)
   # enxf8e43b  (USB 转接器)
   
   # 确认接口状态为 UP 和 RUNNING：
   # <UP,BROADCAST,RUNNING,MULTICAST>
   ```

**网线质量检查：**

```bash
# 检查网络连接速度（连接后执行）
sudo ethtool enp0s31f6  # 替换为你的网口名称

# 期望输出：
Speed: 1000Mb/s          ← 必须是 1000 (千兆)
Duplex: Full             ← 全双工
Link detected: yes       ← 已检测到连接

# 如果 Speed 显示 100Mb/s 或更低：
# - 更换质量更好的网线（Cat5e/Cat6）
# - 检查网口是否支持千兆
# - 清洁水晶头触点
```

### 1.3 配置电脑网络

**重要前提：确定你的 Go2 的 IP 地址**

Go2 不同版本的默认 IP 可能不同，常见配置：

| Go2 型号 | 默认 IP | 子网 |
|---------|---------|------|
| Go2 标准版 | 192.168.123.161 | 192.168.123.0/24 |
| Go2 Pro | 192.168.123.18 | 192.168.123.0/24 |
| Go2 Edu | 192.168.123.13 | 192.168.123.0/24 |

> 💡 如不确定，查看产品说明书或咨询 Unitree 官方支持

**方法1: 图形界面配置（推荐）**

**Ubuntu 20.04/22.04：**

1. **打开网络设置**
   ```
   点击屏幕右上角网络图标 🌐
   → 选择 "Settings" 或 "网络设置"
   → 进入 "Network" 面板
   ```

2. **选择有线连接**
   ```
   在左侧列表找到 "Wired" 或 "有线"
   应该显示为 "Connected" 或 "已连接"
   ```

3. **点击设置按钮**
   ```
   点击连接名称旁边的 ⚙️ 齿轮图标
   进入连接详细设置
   ```

4. **配置 IPv4 地址**
   ```
   点击 "IPv4" 标签页
   
   IPv4 Method: 选择 "Manual" (手动)
   
   在 Addresses 部分点击 ➕ 添加：
   
   Address:  192.168.123.100    ← 你的电脑 IP
   Netmask:  255.255.255.0       ← 子网掩码
   Gateway:  (留空)              ← 不填写
   
   DNS: (留空或关闭自动 DNS)
   
   Routes: 可以留空
   ```

5. **应用设置**
   ```
   点击 "Apply" 按钮
   关闭设置窗口
   ```

6. **重新连接**
   ```
   可能需要：
   - 关闭/开启有线连接开关
   - 或重新插拔网线
   ```

**方法2: 命令行配置（快速）**

```bash
# 1. 确定网络接口名称
ip link show
# 假设是 enp0s31f6

# 2. 配置 IP 地址
sudo ifconfig enp0s31f6 192.168.123.100 netmask 255.255.255.0 up

# 或使用 ip 命令
sudo ip addr flush dev enp0s31f6
sudo ip addr add 192.168.123.100/24 dev enp0s31f6
sudo ip link set enp0s31f6 up

# 3. 验证配置
ifconfig enp0s31f6
# 应该看到：
# inet 192.168.123.100  netmask 255.255.255.0
```

**方法3: 使用 netplan 配置（持久化，Ubuntu 18.04+）**

```bash
# 1. 创建配置文件
sudo nano /etc/netplan/01-go2-network.yaml

# 2. 输入以下内容：
network:
  version: 2
  renderer: NetworkManager
  ethernets:
    enp0s31f6:  # 替换为你的网口名
      dhcp4: no
      addresses:
        - 192.168.123.100/24

# 3. 保存文件 (Ctrl+O, Enter, Ctrl+X)

# 4. 应用配置
sudo netplan apply

# 5. 验证
ip addr show enp0s31f6
```

### 1.4 测试网络连通性

**步骤1: 基础 Ping 测试**

```bash
# Ping Go2 (假设 IP 是 192.168.123.161)
ping 192.168.123.161

# 成功的输出示例：
64 bytes from 192.168.123.161: icmp_seq=1 ttl=64 time=0.521 ms
64 bytes from 192.168.123.161: icmp_seq=2 ttl=64 time=0.432 ms
64 bytes from 192.168.123.161: icmp_seq=3 ttl=64 time=0.498 ms
64 bytes from 192.168.123.161: icmp_seq=4 ttl=64 time=0.445 ms

# 按 Ctrl+C 停止

--- 192.168.123.161 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 3067ms
rtt min/avg/max/mdev = 0.432/0.474/0.521/0.035 ms
```

**关键指标判断：**

```
✅ 优秀: 0% packet loss, avg < 0.5 ms
✅ 良好: 0% packet loss, avg < 1.0 ms
⚠️ 可用: 0% packet loss, avg < 2.0 ms
❌ 问题: 有丢包或 avg > 2.0 ms
```

**步骤2: 持续 Ping 测试（稳定性）**

```bash
# 连续 ping 100 次，测试稳定性
ping -c 100 192.168.123.161

# 查看统计信息
--- 192.168.123.161 ping statistics ---
100 packets transmitted, 100 received, 0% packet loss
rtt min/avg/max/mdev = 0.398/0.512/1.234/0.089 ms

# 分析：
# min: 最小延迟 (应该 < 0.5 ms)
# avg: 平均延迟 (应该 < 1.0 ms)
# max: 最大延迟 (应该 < 2.0 ms)
# mdev: 标准差 (应该 < 0.2 ms，越小越稳定)
```

**步骤3: 高频 Ping 测试（控制环境）**

```bash
# 模拟 50Hz 控制频率 (每 20ms 一次)
ping -i 0.02 -c 500 192.168.123.161

# 如果提示权限不足：
sudo ping -i 0.02 -c 500 192.168.123.161

# 或使用最小间隔测试
sudo ping -f -c 10000 192.168.123.161

# 期望结果：
# 0% packet loss
# avg time < 1 ms
```

**网络诊断与优化：**

```bash
# 如果延迟高或有丢包，进行以下检查：

# 1. 检查网络接口统计
netstat -i
# 查看 RX-ERR 和 TX-ERR 列，应该都是 0

# 2. 检查网络错误
sudo ethtool -S enp0s31f6 | grep -i error
# 所有错误计数应该是 0

# 3. 禁用节能模式
sudo ethtool -s enp0s31f6 speed 1000 duplex full autoneg off

# 4. 增大网络缓冲区
sudo sysctl -w net.core.rmem_max=26214400
sudo sysctl -w net.core.wmem_max=26214400
sudo sysctl -w net.core.rmem_default=26214400
sudo sysctl -w net.core.wmem_default=26214400

# 5. 禁用防火墙（仅测试用）
sudo ufw disable
# 测试完后记得重新启用: sudo ufw enable
```

**步骤4: 验证网络配置**

```bash
# 查看路由表
ip route show

# 应该看到类似：
192.168.123.0/24 dev enp0s31f6 proto kernel scope link src 192.168.123.100

# 查看 ARP 表（确认 Go2 MAC 地址）
arp -n
# 应该看到：
# 192.168.123.161  ether  XX:XX:XX:XX:XX:XX  C  enp0s31f6

# 如果 ARP 表中没有 Go2：
sudo arping -I enp0s31f6 192.168.123.161
```

**网络连接检查清单：**

```
在继续下一步之前，确认以下全部通过：

□ 物理连接正常（LED 指示灯亮）
□ 网络接口状态为 UP
□ 电脑 IP 配置为 192.168.123.100
□ 可以 ping 通 Go2 (192.168.123.161)
□ Ping 延迟 < 1 ms
□ 无丢包 (0% packet loss)
□ 连续 ping 测试稳定

如果以上任一项不通过，返回检查对应步骤
如果全部通过，进入下一阶段 ✅
```

---

## 阶段2: 验证机器人下位机

现在网络已连通，需要验证 Go2 的下位机系统是否正常工作。

### 2.1 SSH 连接到 Go2

**步骤1: 首次 SSH 登录**

```bash
# 打开终端，SSH 连接到 Go2
ssh unitree@192.168.123.161

# 首次连接会看到安全提示：
The authenticity of host '192.168.123.161 (192.168.123.161)' can't be established.
ECDSA key fingerprint is SHA256:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.
Are you sure you want to continue connecting (yes/no/[fingerprint])?

# 输入 yes 并按 Enter
yes

# 提示输入密码
unitree@192.168.123.161's password:
```

**Go2 常见默认密码：**

根据你的 Go2 版本，默认密码可能是：
- `123`
- `unitree`
- `00000000`

> 💡 如果都不对，查看产品说明书或联系 Unitree 支持

**步骤2: 确认登录成功**

```bash
# 成功后会看到 Go2 的命令行提示符：
unitree@go2:~$

# 或类似：
unitree@unitree-go2:~$

# 此时你已经在 Go2 机器人的 Linux 系统中
```

**步骤3: 基础系统信息检查**

```bash
# 1. 查看系统版本
uname -a
# 期望输出包含 aarch64 (ARM 64位架构)

# 2. 查看操作系统信息
cat /etc/os-release
# 通常是 Ubuntu 或定制的 Linux 发行版

# 3. 查看磁盘空间
df -h
# 确保根分区 (/) 还有至少 1GB 空间
# 示例输出：
# Filesystem      Size  Used Avail Use% Mounted on
# /dev/mmcblk0p1   29G   12G   16G  43% /

# 4. 查看内存使用
free -h
# 确保有足够的可用内存
# 示例输出：
#               total        used        free      shared  buff/cache   available
# Mem:          3.8Gi       1.2Gi       1.5Gi       45Mi       1.1Gi       2.4Gi

# 5. 查看 CPU 信息
lscpu | grep "Model name"
# 应该显示 ARM 架构处理器

# 6. 查看系统运行时间
uptime
# 示例：up 2:15  (运行了2小时15分钟)
```

**步骤4: 退出 SSH**

```bash
# 退出 Go2 系统，返回你的电脑
exit
# 或按 Ctrl+D

# 应该回到你电脑的命令行提示符
```

### 2.2 检查下位机服务状态

**方法1: 远程执行命令（推荐）**

不需要登录进 Go2，直接在电脑上执行：

```bash
# 查看所有 unitree 相关服务
ssh unitree@192.168.123.161 'systemctl status unitree* --no-pager'

# 期望看到类似输出：
● unitree_motor.service - Unitree Motor Control Service
   Loaded: loaded (/lib/systemd/system/unitree_motor.service; enabled)
   Active: active (running) since Fri 2026-01-10 10:30:15 CST; 2h 15min ago
   Main PID: 1234 (unitree_motor)
   ...

● unitree_comm.service - Unitree Communication Service
   Loaded: loaded (/lib/systemd/system/unitree_comm.service; enabled)
   Active: active (running) since Fri 2026-01-10 10:30:20 CST; 2h 15min ago
   Main PID: 1256 (unitree_comm)
   ...
```

**关键信息解读：**

```
✅ Active: active (running)     服务正常运行
✅ Loaded: loaded ... enabled   服务已加载并设为开机启动
✅ Main PID: xxxx               进程 ID 存在

❌ Active: inactive (dead)      服务未运行
❌ Active: failed               服务启动失败
⚠️ Active: activating           服务正在启动中（等待片刻再检查）
```

**方法2: 登录后检查**

```bash
# SSH 登录
ssh unitree@192.168.123.161

# 查看服务状态
systemctl status unitree_motor
systemctl status unitree_comm
# 或查看所有
systemctl status unitree*

# 查看服务是否开机自启
systemctl is-enabled unitree_motor
# 期望输出：enabled

# 退出
exit
```

**如果服务未运行：**

```bash
# 尝试启动服务
ssh unitree@192.168.123.161 'sudo systemctl start unitree_motor'
ssh unitree@192.168.123.161 'sudo systemctl start unitree_comm'

# 再次检查状态
ssh unitree@192.168.123.161 'systemctl status unitree_motor'
```

### 2.3 检查运行进程

```bash
# 查看所有 unitree 相关进程
ssh unitree@192.168.123.161 'ps aux | grep unitree | grep -v grep'

# 期望看到类似输出：
unitree   1234  0.5  1.2  123456  45678 ?  Sl  10:30  0:15 /usr/local/bin/unitree_motor
unitree   1256  0.3  0.8   98765  32100 ?  Sl  10:30  0:10 /usr/local/bin/unitree_comm
unitree   1278  0.2  0.5   65432  21098 ?  Sl  10:30  0:08 /usr/local/bin/unitree_sdk2

# 关键列说明：
# USER  PID   %CPU  %MEM  VSZ    RSS   TTY  STAT START  TIME  COMMAND
```

**检查关键进程：**

```bash
# 1. 电机控制进程
ssh unitree@192.168.123.161 'pgrep -f unitree_motor'
# 应该返回一个进程 ID 数字

# 2. 通信进程
ssh unitree@192.168.123.161 'pgrep -f unitree_comm'
# 应该返回一个进程 ID 数字

# 3. 如果没有返回任何数字，说明进程未运行
```

### 2.4 查看系统日志

```bash
# 查看最近的系统日志（最近 50 行）
ssh unitree@192.168.123.161 'journalctl -n 50 --no-pager'

# 查看 unitree 服务的日志
ssh unitree@192.168.123.161 'journalctl -u unitree_motor -n 50 --no-pager'
ssh unitree@192.168.123.161 'journalctl -u unitree_comm -n 50 --no-pager'

# 查看是否有错误信息
ssh unitree@192.168.123.161 'journalctl -p err -n 50 --no-pager'

# 实时监控日志（按 Ctrl+C 停止）
ssh unitree@192.168.123.161 'journalctl -f'
```

**日志分析要点：**

```
正常日志示例：
Jan 10 10:30:15 go2 systemd[1]: Started Unitree Motor Control Service.
Jan 10 10:30:15 go2 unitree_motor[1234]: [INFO] Motor controller initialized
Jan 10 10:30:15 go2 unitree_motor[1234]: [INFO] All joints ready

异常日志示例：
Jan 10 10:30:15 go2 unitree_motor[1234]: [ERROR] Failed to connect to motor
Jan 10 10:30:15 go2 unitree_motor[1234]: [WARN] Joint 3 temperature high: 65°C
```

### 2.5 验证关节状态（可选，高级）

如果 Go2 提供了状态查询工具：

```bash
# 查看关节状态（根据实际工具名称调整）
ssh unitree@192.168.123.161 'unitree_tool joint_status'

# 或查看传感器数据
ssh unitree@192.168.123.161 'unitree_tool imu_data'

# 可能的输出示例：
Joint 0 (FR_hip):   pos=0.05 rad, vel=0.00 rad/s, temp=42°C
Joint 1 (FR_thigh): pos=0.90 rad, vel=0.00 rad/s, temp=45°C
...
```

### 2.6 检查 SDK 版本

```bash
# 查看 unitree SDK 库文件
ssh unitree@192.168.123.161 'ls -l /usr/local/lib/libunitree*'

# 期望输出：
-rw-r--r-- 1 root root 1234567 Dec 20 2025 /usr/local/lib/libunitree_sdk2.so
-rw-r--r-- 1 root root  234567 Dec 20 2025 /usr/local/lib/libunitree_dds.so

# 查看 SDK 版本（如果有版本工具）
ssh unitree@192.168.123.161 'unitree_sdk2 --version'
# 或
ssh unitree@192.168.123.161 'cat /usr/local/include/unitree/version.h'
```

### 2.7 测试通信接口（可选）

**使用 Unitree 测试工具：**

```bash
# 登录到 Go2
ssh unitree@192.168.123.161

# 运行 DDS 通信测试（如果提供）
unitree_tool test_dds

# 应该看到数据流输出：
[DDS Test] Publishing to topic: rt/lowstate
[DDS Test] Data: timestamp=123456789, joints=[...]
[DDS Test] Publishing to topic: rt/lowstate
[DDS Test] Data: timestamp=123456790, joints=[...]

# 按 Ctrl+C 停止测试

# 退出
exit
```

**检查网络端口：**

```bash
# 查看 Go2 打开的网络端口
ssh unitree@192.168.123.161 'netstat -tuln | grep LISTEN'

# 期望看到某些端口在监听（具体端口根据 SDK 版本）
# 例如：
tcp  0  0  0.0.0.0:8080   0.0.0.0:*  LISTEN
udp  0  0  0.0.0.0:7400   0.0.0.0:*
```

### 2.8 下位机验证检查清单

```
在继续之前，确认以下全部通过：

□ 可以通过 SSH 登录到 Go2
□ unitree_motor 服务正在运行
□ unitree_comm 服务正在运行
□ 相关进程存在且运行正常
□ 系统日志无严重错误信息
□ 磁盘空间充足 (> 1GB)
□ 内存可用 (> 500MB)
□ SDK 库文件存在

如果以上全部通过，进入下一阶段 ✅
如果有问题，参考故障排除部分
```

---

## 阶段3: 模型文件准备与配置

### 3.1 确认模型文件位置

**关键提醒：必须使用 exported/policy.pt！**

```bash
# 在你的电脑上，定位到 rl_sar 项目
cd /home/yzy/MyProject/rl_sar

# 检查训练日志目录结构
ls -la robot_lab/logs/rsl_rl/unitree_go2_rough/

# 找到你的训练运行目录（根据日期时间）
# 例如：2025-12-24_16-11-25

# 检查是否有 exported 文件夹
ls robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/

# 应该看到：
exported/        ← 导出的模型目录
model_3600.pt
model_3700.pt
...
model_23599.pt   ← 训练检查点（不能直接用！）
```

**验证 exported/policy.pt 存在：**

```bash
# 检查导出的模型文件
ls -lh robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/exported/

# 期望输出：
policy.pt      ← 这个文件必须存在！（TorchScript 模型）
policy.onnx    ← ONNX 格式（可选）

# 查看文件大小
du -h robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/exported/policy.pt
# 通常几 MB 到几十 MB
```

**如果 exported 文件夹不存在：**

```bash
# 你需要先导出模型！
python3 export_isaac_lab_model.py \
    robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/model_23599.pt \
    -o robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/exported/policy.pt

# 等待导出完成
# 应该看到：
# ✓ Model successfully exported and verified!
# Output file: .../exported/policy.pt
```

### 3.2 验证模型可加载

```bash
# 使用 Python 测试加载模型
python3 << 'EOF'
import torch
import os

# 模型路径（根据你的实际路径调整）
model_path = "robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/exported/policy.pt"

# 检查文件存在
if not os.path.exists(model_path):
    print(f"❌ 错误：模型文件不存在: {model_path}")
    exit(1)

print(f"✓ 模型文件存在: {model_path}")
print(f"  文件大小: {os.path.getsize(model_path) / 1024 / 1024:.2f} MB")

# 尝试加载模型
try:
    policy = torch.jit.load(model_path, map_location='cpu')
    print("✓ 模型加载成功")
    print(f"  类型: {type(policy)}")
    
    # 检查是否有 forward 方法
    if hasattr(policy, 'forward'):
        print("✓ 模型包含 forward 函数")
    else:
        print("❌ 警告：模型不包含 forward 函数")
    
    # 测试推理
    import numpy as np
    dummy_obs = torch.randn(1, 45)  # 假设观察空间是 45 维
    with torch.no_grad():
        actions = policy(dummy_obs)
    print(f"✓ 测试推理成功")
    print(f"  输入形状: {dummy_obs.shape}")
    print(f"  输出形状: {actions.shape}")
    print(f"  输出示例: {actions[0, :3].tolist()}")
    
except Exception as e:
    print(f"❌ 错误：{e}")
    exit(1)

print("\n✅ 模型验证通过！")
EOF
```

### 3.3 复制模型到部署位置

**rl_sar 项目的模型存放规则：**

```
src/rl_sar/policy/
└── go2/                    ← 机器人型号
    ├── base.yaml           ← 基础配置（关节名称、默认姿态等）
    ├── himloco/            ← 配置名称1
    │   ├── config.yaml
    │   └── policy.pt
    ├── robot_lab/          ← 配置名称2
    │   ├── config.yaml
    │   └── policy.pt
    └── your_config/        ← 你的自定义配置
        ├── config.yaml
        └── policy.pt
```

**步骤1: 选择或创建配置目录**

```bash
# 进入 policy 目录
cd /home/yzy/MyProject/rl_sar/src/rl_sar/policy/go2

# 查看现有配置
ls -la

# 方案A: 使用现有配置（例如 robot_lab）
# 方案B: 创建新配置
mkdir -p my_deployment
```

**步骤2: 复制模型文件**

```bash
# 复制 policy.pt 到目标位置
# （根据你选择的配置目录调整）

cp /home/yzy/MyProject/rl_sar/robot_lab/logs/rsl_rl/unitree_go2_rough/2025-12-24_16-11-25/exported/policy.pt \
   /home/yzy/MyProject/rl_sar/src/rl_sar/policy/go2/robot_lab/policy.pt

# 验证复制成功
ls -lh /home/yzy/MyProject/rl_sar/src/rl_sar/policy/go2/robot_lab/policy.pt

# 应该显示文件大小和日期
```

### 3.4 配置 config.yaml

**步骤1: 编辑配置文件**

```bash
# 使用文本编辑器打开配置文件
cd /home/yzy/MyProject/rl_sar
nano src/rl_sar/policy/go2/robot_lab/config.yaml

# 或使用图形编辑器
gedit src/rl_sar/policy/go2/robot_lab/config.yaml
```

**步骤2: 检查关键参数**

以下是需要检查和设置的关键参数（根据实际训练配置调整）：

```yaml
# ============ 观察和动作空间 ============
num_observations: 45        # 必须与训练时一致！
num_actions: 12             # Go2 有 12 个关节

# ============ 推理后端 ============
runner: "libtorch"          # 使用 TorchScript (policy.pt)
# runner: "onnx"            # 或使用 ONNX (policy.onnx)

# ============ 控制参数 ============
control:
  decimation: 10            # 降采样倍数 (仿真频率 500Hz → 控制频率 50Hz)
  action_scale: 0.25        # 动作缩放系数
                            # 初始建议 0.1-0.15（保守）
                            # 测试稳定后可增加到 0.25-0.3
  
# ============ PD 控制增益 ============
kp: 20.0                    # 位置增益（刚度）
kd: 0.5                     # 速度阻尼
                            # 如果抖动：降低 kp，增加 kd
                            # 如果响应慢：增加 kp

# ============ 安全限制 ============
safety:
  max_joint_velocity: 10.0  # 最大关节速度 (rad/s)
  max_joint_torque: 25.0    # 最大关节扭矩 (Nm)
```

**步骤3: 检查 base.yaml**

```bash
# 查看基础配置
cat src/rl_sar/policy/go2/base.yaml
```

确认包含正确的关节配置：

```yaml
# 关节名称（顺序很重要！）
joint_names:
  - FR_hip       # 0
  - FR_thigh     # 1
  - FR_calf      # 2
  - FL_hip       # 3
  - FL_thigh     # 4
  - FL_calf      # 5
  - RR_hip       # 6
  - RR_thigh     # 7
  - RR_calf      # 8
  - RL_hip       # 9
  - RL_thigh     # 10
  - RL_calf      # 11

# 默认站立姿态（弧度）
default_dof_pos:
  - 0.0    # FR_hip
  - 0.9    # FR_thigh
  - -1.8   # FR_calf
  - 0.0    # FL_hip
  - 0.9    # FL_thigh
  - -1.8   # FL_calf
  - 0.0    # RR_hip
  - 0.9    # RR_thigh
  - -1.8   # RR_calf
  - 0.0    # RL_hip
  - 0.9    # RL_thigh
  - -1.8   # RL_calf
```

### 3.5 模型配置检查清单

```
确认以下全部正确：

□ policy.pt 文件已复制到正确位置
□ policy.pt 可以成功加载（Python 测试通过）
□ config.yaml 中 num_observations 与训练一致
□ config.yaml 中 num_actions = 12
□ runner 设置为 "libtorch"
□ action_scale 设置为保守值（0.1-0.15）
□ kp、kd 参数已设置
□ base.yaml 中关节顺序正确
□ default_dof_pos 为合理的站立姿态

如果以上全部确认，进入下一阶段 ✅
```

---

## 阶段4: 编译部署程序

### 4.1 检查编译环境

```bash
# 确保在项目根目录
cd /home/yzy/MyProject/rl_sar

# 检查必要工具
which cmake
which g++
which make

# 检查编译脚本
ls -la build.sh
# 应该看到可执行权限 (-rwxr-xr-x)

# 如果没有执行权限
chmod +x build.sh
```

### 4.2 编译项目（CMake 方式）

**使用 build.sh 脚本：**

```bash
# 方式1: CMake 编译（独立于 ROS）
./build.sh -m

# 编译过程输出：
[build.sh] Building with CMake...
-- The CXX compiler identification is GNU 9.4.0
-- Check for working CXX compiler: /usr/bin/c++
-- Configuring done
-- Generating done
-- Build files have been written to: /home/yzy/MyProject/rl_sar/cmake_build
[ 10%] Building CXX object CMakeFiles/rl_sar.dir/src/...
[ 20%] Building CXX object CMakeFiles/rl_sar.dir/src/...
...
[100%] Built target rl_real_go2

# 首次编译约需 5-15 分钟，取决于电脑性能
```

**如果需要并行编译（加速）：**

```bash
# 使用 4 个线程并行编译
./build.sh -m -j4

# 或使用 CPU 核心数
./build.sh -m -j$(nproc)
```

**如果编译过程中出现错误：**

```bash
# 清理后重新编译
rm -rf cmake_build
./build.sh -m

# 或使用编译脚本的清理选项
./build.sh -m --clean
```

### 4.3 验证编译结果

```bash
# 检查生成的可执行文件
ls -lh cmake_build/bin/

# 期望输出：
rl_real_go2         ← Go2 实机控制程序（这是我们需要的！）
rl_real_a1          ← A1 控制程序（如果编译了）
rl_sim_mujoco       ← MuJoCo 仿真（如果编译了）

# 查看文件大小（通常几 MB）
du -h cmake_build/bin/rl_real_go2

# 检查文件类型
file cmake_build/bin/rl_real_go2
# 应该显示：ELF 64-bit LSB executable, x86-64
```

**测试程序启动：**

```bash
# 尝试运行帮助命令（不会控制机器人）
./cmake_build/bin/rl_real_go2 --help

# 期望输出使用说明：
Usage: rl_real_go2 <network_interface> [options]

Arguments:
  network_interface    Network interface name (e.g., enp0s31f6)

Options:
  --robot <type>       Robot type (default: go2)
  --policy <name>      Policy configuration name (default: robot_lab)
  --help               Show this help message

Examples:
  ./rl_real_go2 enp0s31f6
  ./rl_real_go2 enp0s31f6 --policy himloco
```

### 4.4 配置运行参数

**方法1: 命令行参数**

```bash
# 基本用法
./cmake_build/bin/rl_real_go2 <网络接口> [--policy <配置名>]

# 示例：
./cmake_build/bin/rl_real_go2 enp0s31f6 --policy robot_lab

# 参数说明：
# enp0s31f6      - 你电脑连接 Go2 的网络接口名
# --policy       - 使用哪个策略配置（对应 policy/go2/ 下的目录名）
```

**方法2: 环境变量（可选）**

```bash
# 设置默认策略
export RL_SAR_POLICY=robot_lab

# 设置日志级别
export RL_SAR_LOG_LEVEL=INFO  # DEBUG, INFO, WARN, ERROR

# 运行
./cmake_build/bin/rl_real_go2 enp0s31f6
```

### 4.5 创建启动脚本（推荐）

```bash
# 创建方便的启动脚本
cat > run_go2.sh << 'EOF'
#!/bin/bash

# Unitree Go2 启动脚本

# 配置参数
NETWORK_INTERFACE="enp0s31f6"  # 替换为你的网口
POLICY_NAME="robot_lab"         # 策略配置名
ROBOT_IP="192.168.123.161"      # Go2 IP

# 颜色输出
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}============================================${NC}"
echo -e "${GREEN}    Unitree Go2 启动脚本${NC}"
echo -e "${GREEN}============================================${NC}"
echo ""

# 检查网络连接
echo -e "${YELLOW}[1/4] 检查网络连接...${NC}"
if ping -c 1 -W 1 $ROBOT_IP > /dev/null 2>&1; then
    echo -e "${GREEN}✓ 网络连接正常${NC}"
else
    echo -e "${RED}✗ 无法连接到 Go2 ($ROBOT_IP)${NC}"
    echo -e "${RED}  请检查网线和网络配置${NC}"
    exit 1
fi

# 检查可执行文件
echo -e "${YELLOW}[2/4] 检查程序文件...${NC}"
if [ -f "cmake_build/bin/rl_real_go2" ]; then
    echo -e "${GREEN}✓ 程序文件存在${NC}"
else
    echo -e "${RED}✗ 找不到 cmake_build/bin/rl_real_go2${NC}"
    echo -e "${RED}  请先编译：./build.sh -m${NC}"
    exit 1
fi

# 检查策略文件
echo -e "${YELLOW}[3/4] 检查策略文件...${NC}"
POLICY_PATH="src/rl_sar/policy/go2/$POLICY_NAME/policy.pt"
if [ -f "$POLICY_PATH" ]; then
    echo -e "${GREEN}✓ 策略文件存在: $POLICY_PATH${NC}"
else
    echo -e "${RED}✗ 找不到策略文件: $POLICY_PATH${NC}"
    exit 1
fi

# 安全确认
echo ""
echo -e "${YELLOW}[4/4] 安全检查${NC}"
echo -e "${YELLOW}============================================${NC}"
echo "请确认："
echo "  □ Go2 已放在支架上或安全区域"
echo "  □ 遥控器在手中，STOP 按钮可随时按下"
echo "  □ 周围无人员和障碍物"
echo "  □ 观察员已就位"
echo -e "${YELLOW}============================================${NC}"
echo ""
read -p "确认以上安全措施 (输入 yes 继续): " confirm

if [ "$confirm" != "yes" ]; then
    echo -e "${RED}已取消${NC}"
    exit 0
fi

echo ""
echo -e "${GREEN}启动控制程序...${NC}"
echo -e "${YELLOW}按 Ctrl+C 安全退出${NC}"
echo ""

# 运行程序
./cmake_build/bin/rl_real_go2 $NETWORK_INTERFACE --policy $POLICY_NAME

EOF

# 设置执行权限
chmod +x run_go2.sh

# 测试脚本
echo "✓ 启动脚本已创建: run_go2.sh"
```

**使用启动脚本：**

```bash
# 直接运行
./run_go2.sh

# 或修改网络接口后运行
nano run_go2.sh  # 编辑 NETWORK_INTERFACE 变量
./run_go2.sh
```

### 4.6 编译检查清单

```
确认以下全部完成：

□ 编译成功，无错误
□ rl_real_go2 可执行文件存在
□ 程序可以显示帮助信息
□ 策略文件路径正确
□ 网络接口名称正确
□ 创建了启动脚本（可选）

如果以上全部完成，进入测试阶段 ✅
```

---

## 阶段5: 安全测试流程

⚠️ **这是最关键的阶段！严格按照步骤执行！**

### 5.1 测试前准备

**环境准备：**

```
测试环境要求：
□ 机器人支架或安全台面（首次测试必须悬空）
□ 平坦地面（后续落地测试用）
□ 清空周围 3 米范围内所有物品
□ 光线充足，便于观察
□ 有至少一名观察员协助

人员准备：
□ 操作员：控制电脑和终端
□ 观察员：手持遥控器，观察机器人状态
□ 双方确认紧急停止流程

设备准备：
□ Go2 电量 > 70%
□ 遥控器电量充足
□ 网线连接稳定
□ 电脑电量充足或接电源
```

**预启动检查：**

```bash
# 1. 最后一次网络测试
ping -c 10 192.168.123.161
# 确保 0% 丢包，延迟 < 1ms

# 2. 检查 SSH 连接
ssh unitree@192.168.123.161 'uptime'
# 确保能正常连接

# 3. 检查关节初始状态
ssh unitree@192.168.123.161 'systemctl status unitree_motor'
# 确保服务运行正常
```

### 5.2 支架悬空测试（第一步，必须！）

**⚠️ 极其重要：首次运行必须让机器人悬空！**

**步骤1: 放置机器人**

```
理想设置：
- 使用专用机器人测试支架
- 机器人四条腿悬空，不接触地面
- 机身稳固支撑，不会晃动
- 腿部有足够摆动空间（前后左右至少 30cm）

替代方案（如无专用支架）：
- 使用两个高度适当的桌子
- 将 Go2 放在桌子之间，腿部悬空
- 用软垫保护机身，防止滑动
- 确保桌子稳固，不会倾倒

⚠️ 注意：
- 悬空时腿部会摆动，确保周围无障碍物
- 有人随时准备按遥控器 STOP
- 第一次测试时间控制在 10-30 秒
```

**步骤2: 启动程序**

```bash
# 方式1: 使用启动脚本
./run_go2.sh

# 方式2: 直接运行
./cmake_build/bin/rl_real_go2 enp0s31f6 --policy robot_lab

# 程序启动输出：
[INFO] ============================================
[INFO] Unitree Go2 Control System
[INFO] ============================================
[INFO] Network interface: enp0s31f6
[INFO] Policy: robot_lab
[INFO] Loading configuration...
[INFO] Loading policy from: src/rl_sar/policy/go2/robot_lab/policy.pt
[INFO] Policy loaded successfully
[INFO] Connecting to robot at 192.168.123.161...
[INFO] Connection established
[INFO] Entering damping mode...
[INFO] 
[INFO] Robot ready!
[INFO] Press gamepad A (or keyboard Num0) to stand up
[INFO] Press STOP button for emergency stop
[INFO] 
```

**步骤3: 观察阻尼模式**

```
此时机器人处于"阻尼模式"：
- 关节电机通电但处于低阻尼状态
- 可以用手轻松推动关节
- 感觉像是有轻微阻力的"软"状态
- 这是正常的初始状态

用手测试：
1. 轻轻抬起一条腿，应该感觉很轻
2. 松手后腿会缓慢下垂
3. 推动髋关节，应该能自由转动
4. 这说明通信和电机控制正常
```

**步骤4: 激活站立**

```
⚠️ 重要：确保观察员手持遥控器，拇指在 STOP 按钮上！

操作步骤：
1. 操作员：按下遥控器 A 按钮（或键盘 Num0）
2. 观察员：密切观察机器人动作

期望行为：
- 四条腿缓慢、同步地伸展
- 过程平滑，持续约 1-2 秒
- 最终达到站立姿态
- 无剧烈抖动或异常响声

正常站立姿态：
- 髋关节（hip）接近 0°
- 大腿关节（thigh）约 50-60°
- 小腿关节（calf）约 -100° to -110°
- 身体保持水平
```

**步骤5: 检查站立状态**

```
站立后观察 30 秒：

终端输出检查：
[INFO] Standing mode activated
[INFO] Control frequency: 50.2 Hz      ← 应该稳定在 48-52 Hz
[INFO] Joint positions: [0.05, 0.92, -1.79, ...]
[INFO] Joint torques: [2.3, 5.1, -3.2, ...]  ← 应该 < 15 Nm

视觉检查：
✓ 四条腿保持站立姿态
✓ 身体基本水平（pitch/roll < 5°）
✓ 无明显抖动
✓ 关节无异常响声
✓ 电机温度正常（触摸不烫手）

如果一切正常，继续下一步
如果有任何异常，立即按 STOP
```

**步骤6: 激活步态**

```
操作：
按 RB + DPad Up（或键盘 Num1）

期望行为：
- 程序进入"步态模式"
- 四条腿开始做出行走动作
- 虽然悬空，但应该有协调的步态

终端输出：
[INFO] Gait activated
[INFO] Command: vx=0.00 vy=0.00 vyaw=0.00  ← 初始无速度命令

观察重点：
✓ 四条腿交替摆动（对角线步态）
✓ 摆动幅度合理（不是极端角度）
✓ 动作流畅，无卡顿
✓ 频率稳定（约 1-2 Hz 的步频）
✓ 无异常振动或碰撞声

测试时间：
- 首次观察 10-20 秒
- 如果正常，可延长到 1-2 分钟
```

**步骤7: 测试速度命令**

```
给予小幅度速度命令：

操作：左摇杆轻轻前推（或按住键盘 W）

期望行为：
- 腿部摆动频率加快
- 摆动幅度可能增大
- 动作仍然协调

观察：
✓ 步频随命令增加
✓ 动作不会过于剧烈
✓ 控制频率保持稳定
✓ 无抖动或失控迹象

测试不同命令：
- 前进（W 键或左摇杆前推）
- 后退（S 键或左摇杆后拉）
- 左转（Q 键或右摇杆左推）
- 右转（E 键或右摇杆右推）

每个命令测试 5-10 秒
```

**步骤8: 停止测试**

```
正常停止流程：

1. 松开摇杆/按键（速度命令归零）
2. 按空格键停止步态
3. 按 B 键（或 Num2）躺下
4. 终端按 Ctrl+C 退出程序

机器人行为：
- 停止步态后腿部回到站立姿态
- 躺下时缓慢收腿
- 退出程序后进入阻尼模式

或紧急停止（测试紧急按钮）：
1. 按遥控器 STOP 按钮
2. 机器人立即停止所有动作
3. 进入阻尼模式
4. 终端可能显示通信中断
5. Ctrl+C 退出程序
```

**步骤9: 悬空测试检查清单**

```
完成以下全部测试项：

□ 阻尼模式正常（可手动推动关节）
□ 站立过程平滑（1-2秒）
□ 站立姿态合理（无极端角度）
□ 站立状态稳定（30秒无抖动）
□ 控制频率稳定（48-52 Hz）
□ 步态激活成功
□ 原地步态协调（悬空走路动作）
□ 速度命令有响应（前进/后退/转向）
□ 关节温度正常（< 50°C）
□ 紧急停止功能有效
□ 正常停止流程顺利
□ 终端无严重错误信息

如果以上全部通过，可以进行落地测试 ✅
如果有任何问题，参考故障排除或调整参数
```

### 5.3 落地站立测试（第二步）

⚠️ **在悬空测试完全通过后才能进行！**

**步骤1: 场地最后检查**

```
地面要求：
□ 平坦、水平（用水平仪检查）
□ 防滑（瓷砖、水泥地、橡胶地垫）
□ 清洁、干燥（无水渍、油渍）
□ 硬质地面（避免地毯、草地）

空间要求：
□ 至少 3m x 3m 空旷区域
□ 无障碍物（桌腿、椅子、线缆等）
□ 有足够撤退空间

人员位置：
□ 观察员站在侧面 2米处（不要站前方或后方）
□ 操作员坐在电脑前，视线能看到机器人
□ 预设撤退路线
```

**步骤2: 放置机器人**

```
初始姿态：
- 将 Go2 轻放在地面上
- 腹部着地，四腿自然收起
- 确保放置平稳，不倾斜
- 头部朝向空旷方向

调整：
- 轻轻推动，确保四个脚掌都接触地面
- 检查无异物垫在脚下
- 机身摆正，不歪斜
```

**步骤3: 启动程序**

```bash
# 启动控制程序
./run_go2.sh

# 或
./cmake_build/bin/rl_real_go2 enp0s31f6 --policy robot_lab

# 等待进入阻尼模式
[INFO] Robot ready!
[INFO] Press gamepad A (or keyboard Num0) to stand up
```

**步骤4: 观察员确认**

```
操作员：准备好了吗？
观察员：准备好，遥控器在手
操作员：我要按 A 键站立了
观察员：可以，我在看着

（沟通很重要！）
```

**步骤5: 首次落地站立**

```
操作：按 A 键

观察重点（观察员）：
1. 站立过程（2-3秒）
   ✓ 四条腿同步发力
   ✓ 身体平稳抬起
   ✓ 无打滑现象
   ✓ 脚掌完全着地

2. 最终姿态
   ✓ 身体水平（pitch/roll < 5°）
   ✓ 高度合理（地面到髋关节约 20-25cm）
   ✓ 四脚着地，受力均匀
   ✓ 无颤抖或摇晃

3. 异常情况立即 STOP：
   ✗ 身体倾斜 > 15°
   ✗ 脚掌打滑
   ✗ 腿部剧烈抖动
   ✗ 无法站起（腿一直弯曲）
   ✗ 发出异常响声

终端监控（操作员）：
[INFO] Standing...
[INFO] Frequency: 50.1 Hz
[INFO] IMU: roll=0.5° pitch=1.2°
[INFO] Torques: [3.2, 6.1, -4.3, ...] ← 应该 < 15 Nm
[INFO] Standing complete
```

**步骤6: 原地站立稳定性测试**

```
站立后保持静止 1 分钟：

观察内容：
✓ 机身保持稳定，无明显摇晃
✓ 四条腿受力均匀（无单腿承重过大）
✓ 身体高度恒定（不下沉）
✓ 关节位置稳定（无缓慢漂移）

终端监控每秒输出：
[INFO] Standing stable | Freq: 50.2Hz | Torque max: 8.5Nm

如果 1分钟内稳定，测试通过 ✅
如果出现以下情况，停止并分析：
- 身体缓慢倾斜
- 某条腿无力（弯曲）
- 关节温度快速上升
- 控制频率下降
```

**步骤7: 轻微外力测试（可选）**

```
⚠️ 轻轻推动测试（需谨慎）：

观察员轻轻推动机身侧面：
- 用手掌推，力度约 5-10N
- 推动后观察是否能恢复平衡
- 不要用力过猛

期望反应：
✓ 机器人能保持站立
✓ 腿部自动调整以维持平衡
✓ 推力移除后恢复原位

如果一推就倒，说明：
- action_scale 可能过小
- kp 增益可能不足
- 策略可能需要重新训练
```

**步骤8: 正常停止**

```
测试完成后：
1. 按 B 键（或 Num2）躺下
2. 观察躺下过程（应该平滑）
3. Ctrl+C 退出程序

不要直接关电源或拔网线！
```

**落地站立检查清单：**

```
□ 站立过程平稳（无打滑）
□ 最终姿态正确（水平、高度合理）
□ 原地站立稳定（1分钟无问题）
□ 四脚受力均匀
□ 关节温度正常
□ 控制频率稳定
□ 能正常躺下

如果全部通过，进入移动测试 ✅
```

### 5.4 原地踏步测试（第三步）

**步骤1: 站立并激活步态**

```bash
# 启动程序，站立
./run_go2.sh
# 按 A 键站立
# 按 RB+DPad Up (或 Num1) 激活步态

# 此时不要给速度命令（摇杆保持居中）
```

**步骤2: 观察原地步态**

```
期望行为：
- 机器人原地做出走路动作
- 身体基本保持在原位（允许轻微位移 < 10cm）
- 步态协调、流畅
- 抬腿高度合理（足端离地约 5-10cm）

观察时间：30-60 秒

检查项：
✓ 对角线步态（RF-LH、LF-RH 交替）
✓ 步频稳定（约 1-2 Hz）
✓ 身体姿态稳定（无大幅摇晃）
✓ 无脚掌拖地现象
✓ 着地轻柔（无重击地面）

异常情况：
✗ 步频不稳定（忽快忽慢）
✗ 单腿不动或动作异常
✗ 身体大幅前后/左右摆动
✗ 突然失去平衡

如果稳定，继续下一步
```

**步骤3: 停止步态**

```
按空格键停止步态

观察：
- 机器人平滑过渡回站立姿态
- 四条腿稳定着地
- 身体保持水平

测试 2-3 次循环：
站立 → 步态 → 停止 → 站立
确保转换过程稳定
```

### 5.5 慢速移动测试（第四步）

⚠️ **开始实际移动！需要空旷场地！**

**步骤1: 场地准备**

```
确保：
□ 前方至少 3 米无障碍物
□ 地面平坦、防滑
□ 观察员位于侧方，不在前进路线上
□ 有足够空间让机器人移动
```

**步骤2: 小幅度前进**

```
操作：
1. 激活步态（Num1）
2. 左摇杆轻轻前推约 20-30%（或按住 W 键）

期望速度：约 0.1-0.2 m/s（很慢！）

观察重点：
✓ 缓慢向前移动
✓ 保持直线（允许轻微偏离 < 15°）
✓ 步态稳定
✓ 身体姿态良好
✓ 无跌倒迹象

首次移动距离：
- 第1次：0.5 米后停止（松开摇杆 + 空格）
- 第2次：1 米后停止
- 第3次：2 米后停止

每次移动后检查：
- 关节温度（触摸）
- 控制频率（终端）
- 电池电量
```

**步骤3: 速度递增测试**

```
在小幅度测试成功后，逐步增加速度：

摇杆位置  /  期望速度  /  测试距离
20%       →  0.1 m/s  →  0.5米 ✓
30%       →  0.15 m/s →  1米
40%       →  0.2 m/s  →  2米
50%       →  0.3 m/s  →  3米

⚠️ 注意：
- 每个速度测试 2-3 次
- 确认稳定后再增加
- 不要超过 50% 摇杆（中等速度）
- 如果出现不稳定，降回上一级

终端监控：
[INFO] Command: vx=0.20 vy=0.00 vyaw=0.00
[INFO] Frequency: 50.1 Hz
[INFO] Actual vel: [0.18, 0.01, 0.02] m/s  ← 实际速度接近命令
```

**步骤4: 后退测试**

```
操作：左摇杆后拉（或按住 S 键）

注意：
- 后退通常比前进更不稳定
- 速度要更慢（摇杆 10-20%）
- 距离更短（0.5-1 米）
- 观察员密切关注

测试：
- 缓慢后退 0.5 米
- 停止
- 再前进回原位
```

### 5.6 转向测试（第五步）

**步骤1: 原地转向**

```
操作：
1. 站立，激活步态
2. 不给前进命令
3. 右摇杆左/右摆动（或 Q/E 键）

期望行为：
- 机器人原地旋转
- 身体保持稳定
- 转向流畅

测试：
- 向左转 90°，停止
- 向右转 90°，停止
- 向左转 180°，停止

观察：
✓ 转向角度准确
✓ 无打滑现象
✓ 转向速度合理
```

**步骤2: 前进中转向**

```
操作：
1. 给予小幅度前进命令（W 键）
2. 同时给予转向命令（Q 或 E 键）

期望行为：
- 机器人沿弧线移动
- 转弯半径合理（> 0.5米）
- 保持平衡

测试路线：
- 画一个大圈（直径 3-4 米）
- 走 "8" 字形
- Z 字形绕行

如果能完成，说明控制效果良好 ✅
```

### 5.7 综合测试（第六步）

**步骤1: 连续移动测试**

```
任务：让机器人在场地内自由移动 2-3 分钟

操作：
- 随机给予前进、后退、转向命令
- 模拟真实使用场景
- 尝试不同速度组合

观察：
✓ 长时间运行稳定性
✓ 电池续航情况
✓ 关节温度变化
✓ 控制响应速度
✓ 整体表现流畅度
```

**步骤2: 极限测试（可选，谨慎！）**

```
⚠️ 仅在前面测试全部稳定后尝试！

测试项：
1. 最大安全速度（逐步增加到 70-80% 摇杆）
2. 快速转向（大幅度摇杆变化）
3. 紧急停止（高速中突然松开摇杆）

目的：
- 确定安全运行边界
- 测试极端情况表现
- 验证安全机制有效性

如果出现不稳定：
- 记录出现问题的条件
- 降低参数返回安全区
- 作为调优参考
```

### 5.8 安全测试总结

**完整测试流程回顾：**

```
阶段          |  时间  |  状态
-------------|--------|--------
悬空测试      |  30分钟 | ✅ 完成
落地站立      |  15分钟 | ✅ 完成
原地踏步      |  15分钟 | ✅ 完成
慢速移动      |  30分钟 | ✅ 完成
转向测试      |  20分钟 | ✅ 完成
综合测试      |  30分钟 | ✅ 完成
总计          |  2.5小时 |
```

**如果全部测试通过，机器人已可投入实际使用！🎉**

---

## 阶段6: 实际运行与调优

### 6.1 日常使用流程

**标准启动流程：**

```bash
# 1. 开机前检查（2分钟）
□ 电池电量 > 30%
□ 场地安全
□ 网线连接
□ Ping 测试通过

# 2. 启动（3分钟）
./run_go2.sh
# 或
./cmake_build/bin/rl_real_go2 enp0s31f6 --policy robot_lab

# 3. 站立
按 A 键（或 Num0）

# 4. 激活步态
按 RB+DPad Up（或 Num1）

# 5. 开始控制
使用摇杆或键盘控制移动

# 6. 正常关闭
松开控制 → 空格停止步态 → B键躺下 → Ctrl+C 退出
```

**遥控器/键盘控制说明：**

```
基础控制：
A / Num0      - 站立
B / Num2      - 躺下
Num1          - 激活步态
Space         - 停止步态
Ctrl+C        - 退出程序

移动控制（步态激活后）：
W / 左摇杆↑  - 前进
S / 左摇杆↓  - 后退
A / 左摇杆←  - 左侧移（如果支持）
D / 左摇杆→  - 右侧移（如果支持）
Q / 右摇杆←  - 左转
E / 右摇杆→  - 右转

紧急：
遥控器 STOP  - 紧急停止
```

### 6.2 参数调优指南

**问题1: 机器人走路不稳定、频繁跌倒**

```yaml
原因：动作幅度过大

解决：编辑 config.yaml
control:
  action_scale: 0.15  # 从 0.25 降低到 0.15

重新运行测试
如果改善，逐步增加到找到最佳值
```

**问题2: 响应速度慢、动作迟缓**

```yaml
原因：动作幅度过小或增益不足

解决1：增加动作缩放
control:
  action_scale: 0.3  # 从 0.25 增加到 0.3

解决2：增加 PD 增益
kp: 25.0  # 从 20.0 增加到 25.0
kd: 0.5   # 保持不变

测试效果
```

**问题3: 关节抖动、高频振动**

```yaml
原因：kp 过高或 kd 过低

解决：
kp: 15.0  # 从 20.0 降低到 15.0
kd: 1.0   # 从 0.5 增加到 1.0

说明：
- 降低 kp 减少刚性
- 增加 kd 增加阻尼
- 两者配合消除振动
```

**问题4: 控制频率低、延迟高**

```
可能原因：

1. 网络延迟
   检查：ping -c 100 192.168.123.161
   解决：更换网线，检查网口速度

2. CPU 负载高
   检查：top / htop
   解决：关闭不必要的程序

3. 推理速度慢
   考虑使用 ONNX 后端（需要转换模型）

4. Decimation 设置
   config.yaml:
   control:
     decimation: 8  # 从 10 降低到 8（提高频率）
```

**问题5: 实机与仿真差异大（Sim-to-Real Gap）**

```
现象：
- 仿真中表现好，实机上不稳定
- 动作幅度与预期不符
- 某些动作无法完成

短期解决：
1. 调整 action_scale（通常需要降低）
2. 调整 PD 增益
3. 检查观察空间数据是否正确

长期解决：
1. 收集实机数据
2. 使用实机数据进行 domain randomization
3. 在仿真中添加实际物理参数
4. 进行实机 fine-tuning（强化学习继续训练）
```

### 6.3 性能监控

**实时监控脚本：**

```bash
# 创建监控脚本
cat > monitor_go2.sh << 'EOF'
#!/bin/bash

echo "Go2 实时监控"
echo "按 Ctrl+C 停止"
echo ""

while true; do
    clear
    echo "=========================================="
    echo "          Go2 系统监控"
    echo "=========================================="
    echo ""
    
    # 网络延迟
    echo -n "网络延迟: "
    ping -c 1 -W 1 192.168.123.161 | grep 'time=' | awk -F'time=' '{print $2}'
    
    # Go2 CPU 温度
    echo -n "CPU 温度: "
    ssh -q unitree@192.168.123.161 'cat /sys/class/thermal/thermal_zone0/temp' 2>/dev/null | awk '{printf "%.1f°C\n", $1/1000}'
    
    # Go2 内存使用
    echo -n "内存使用: "
    ssh -q unitree@192.168.123.161 'free -h | grep Mem' 2>/dev/null | awk '{print $3 "/" $2}'
    
    # 控制进程
    echo -n "控制进程: "
    if pgrep -f rl_real_go2 > /dev/null; then
        echo "运行中 ✓"
    else
        echo "未运行 ✗"
    fi
    
    echo ""
    echo "=========================================="
    
    sleep 2
done
EOF

chmod +x monitor_go2.sh

# 在另一个终端运行
./monitor_go2.sh
```

### 6.4 数据记录与分析

**启用日志记录：**

控制程序通常会自动记录到 `logs/` 目录：

```bash
# 查看日志文件
ls -lh logs/

# 示例输出：
go2_20260110_143025.csv  ← 控制数据（CSV 格式）
go2_20260110_143025.log  ← 程序日志

# 查看 CSV 数据
head -20 logs/go2_20260110_143025.csv

# 列内容通常包括：
timestamp,
joint_pos_0, joint_pos_1, ..., joint_pos_11,
joint_vel_0, joint_vel_1, ..., joint_vel_11,
joint_tau_0, joint_tau_1, ..., joint_tau_11,
imu_quat_w, imu_quat_x, imu_quat_y, imu_quat_z,
imu_gyro_x, imu_gyro_y, imu_gyro_z,
cmd_vx, cmd_vy, cmd_vyaw,
action_0, action_1, ..., action_11,
control_freq
```

**数据分析示例：**

```python
#!/usr/bin/env python3
import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# 加载数据
data = pd.read_csv('logs/go2_20260110_143025.csv')

# 1. 绘制控制频率
plt.figure(figsize=(12, 4))
plt.plot(data['timestamp'], data['control_freq'])
plt.axhline(y=50, color='r', linestyle='--', label='Target 50Hz')
plt.xlabel('Time (s)')
plt.ylabel('Frequency (Hz)')
plt.title('Control Frequency Over Time')
plt.legend()
plt.grid(True)
plt.savefig('frequency_plot.png')

# 2. 绘制关节扭矩
plt.figure(figsize=(12, 8))
for i in range(12):
    plt.plot(data['timestamp'], data[f'joint_tau_{i}'], label=f'Joint {i}')
plt.axhline(y=25, color='r', linestyle='--', label='Max limit')
plt.axhline(y=-25, color='r', linestyle='--')
plt.xlabel('Time (s)')
plt.ylabel('Torque (Nm)')
plt.title('Joint Torques Over Time')
plt.legend(bbox_to_anchor=(1.05, 1), loc='upper left')
plt.grid(True)
plt.tight_layout()
plt.savefig('torque_plot.png')

# 3. 统计分析
print("\n========== 统计分析 ==========")
print(f"运行时长: {data['timestamp'].max() - data['timestamp'].min():.1f} 秒")
print(f"\n控制频率:")
print(f"  平均: {data['control_freq'].mean():.2f} Hz")
print(f"  最小: {data['control_freq'].min():.2f} Hz")
print(f"  最大: {data['control_freq'].max():.2f} Hz")
print(f"  标准差: {data['control_freq'].std():.2f} Hz")

print(f"\n关节扭矩:")
tau_cols = [f'joint_tau_{i}' for i in range(12)]
tau_max = data[tau_cols].abs().max().max()
print(f"  最大绝对值: {tau_max:.2f} Nm")
if tau_max > 25:
    print(f"  ⚠️ 警告：超过安全限制 25 Nm!")

print(f"\n图表已保存:")
print(f"  - frequency_plot.png")
print(f"  - torque_plot.png")
```

运行分析：

```bash
python3 analyze_log.py
```

---

## 故障排除

### 问题1: 无法 Ping 通 Go2

**症状：**
```bash
ping 192.168.123.161
# PING 192.168.123.161: 56 data bytes
# Request timeout for icmp_seq 0
```

**排查步骤：**

```bash
# 1. 检查物理连接
sudo ethtool enp0s31f6  # 替换为你的网口
# Link detected: yes  ← 必须是 yes
# Speed: 1000Mb/s     ← 必须是 1000

# 2. 检查 IP 配置
ifconfig enp0s31f6
# 确认有 192.168.123.100 地址

# 3. 重新配置网络
sudo ifconfig enp0s31f6 192.168.123.100 netmask 255.255.255.0 up

# 4. 尝试 ping 自己
ping 192.168.123.100  # 应该能通

# 5. 检查防火墙
sudo ufw status
# 如果 active，临时禁用测试：
sudo ufw disable

# 6. 重启网络服务
sudo systemctl restart NetworkManager

# 7. 重启 Go2
# 长按电源键 5 秒关机，等待 10 秒，再开机
```

### 问题2: SSH 连接超时

**症状：**
```bash
ssh unitree@192.168.123.161
# ssh: connect to host 192.168.123.161 port 22: Connection timed out
```

**解决：**

```bash
# 1. 确认 ping 通
ping 192.168.123.161

# 2. 检查 SSH 端口是否开放
nmap 192.168.123.161 -p 22
# 应该显示: 22/tcp open

# 3. 尝试详细模式
ssh -vvv unitree@192.168.123.161
# 查看详细错误信息

# 4. 检查 Go2 的 SSH 服务
# 如果能通过其他方式登录（串口/显示器）：
systemctl status sshd
```

### 问题3: 程序启动后无响应

**症状：**
```bash
./cmake_build/bin/rl_real_go2 enp0s31f6
# 卡住，无任何输出
```

**排查：**

```bash
# 1. 检查是否有其他实例在运行
ps aux | grep rl_real_go2
# 如果有，杀掉：
killall rl_real_go2

# 2. 检查网络接口名是否正确
ip link show
# 使用正确的接口名

# 3. 检查策略文件是否存在
ls -la src/rl_sar/policy/go2/robot_lab/policy.pt

# 4. 检查库文件
ldd cmake_build/bin/rl_real_go2
# 不应该有 "not found"

# 5. 尝试调试模式
gdb cmake_build/bin/rl_real_go2
# (gdb) run enp0s31f6
# (gdb) bt  # 如果崩溃，查看堆栈
```

### 问题4: 控制频率过低

**症状：**
终端显示 `Frequency: 35 Hz`（应该是 50 Hz）

**解决：**

```bash
# 1. 检查网络延迟
ping -c 100 192.168.123.161
# 平均延迟应该 < 1 ms

# 2. 检查 CPU 负载
top
# rl_real_go2 进程 CPU 使用率应该 < 80%

# 3. 关闭不必要的程序
# 关闭浏览器、IDE 等占用 CPU 的程序

# 4. 使用性能模式
sudo cpupower frequency-set -g performance

# 5. 减少 decimation
# 编辑 config.yaml:
# decimation: 8  # 从 10 降到 8

# 6. 考虑使用更快的机器或ONNX后端
```

### 问题5: 机器人站立后立即倒下

**原因分析：**

```
可能原因：
1. action_scale 过大 → 动作幅度超出合理范围
2. 模型与实际不匹配 → sim-to-real gap
3. 地面太滑 → 脚掌打滑
4. 观察数据异常 → IMU 或关节传感器问题
```

**解决步骤：**

```yaml
# 1. 大幅降低 action_scale
编辑 config.yaml:
control:
  action_scale: 0.1  # 从 0.25 降到 0.1

# 2. 增加阻尼
kd: 1.0  # 从 0.5 增加到 1.0

# 3. 更换地面
使用更防滑的地面（橡胶地垫）

# 4. 检查传感器数据
# 在站立时查看终端输出的 IMU 数据
# 如果异常（如 NaN、极大值），可能是传感器故障
```

### 问题6: 关节过热

**症状：**
触摸关节电机感觉很烫（> 60°C）

**解决：**

```
立即措施：
1. 停止运行，让电机冷却 10-15 分钟
2. 检查是否有异物卡住关节
3. 检查关节是否有机械损伤

长期解决：
1. 降低 kp 增益（减少对抗力）
2. 减少连续运行时间（每 5-10 分钟休息）
3. 改善散热（风扇、空调房）
4. 检查是否需要润滑（联系售后）
```

### 问题7: 遥控器无响应

**症状：**
按遥控器按键，机器人无反应

**排查：**

```bash
# 1. 检查遥控器电量
# LED 指示灯是否正常

# 2. 检查遥控器与接收器配对
# 参考遥控器说明书重新配对

# 3. 检查程序是否接收输入
# 终端应该显示按键事件
# 如果没有，可能是 SDK 通信问题

# 4. 尝试键盘控制
# 按 Num0 测试是否响应

# 5. 重启程序
Ctrl+C 退出，重新运行
```

---

## 🎯 总结与最佳实践

### 完整流程回顾

```
✅ 阶段1: 硬件连接（15分钟）
   - 启动 Go2
   - 连接网线
   - 配置网络
   - 测试连通性

✅ 阶段2: 验证下位机（20分钟）
   - SSH 连接
   - 检查服务
   - 查看日志

✅ 阶段3: 模型准备（15分钟）
   - 确认 policy.pt 位置
   - 复制到部署目录
   - 配置参数

✅ 阶段4: 编译程序（20分钟）
   - 编译 rl_sar
   - 验证可执行文件
   - 创建启动脚本

✅ 阶段5: 安全测试（2小时）
   - 悬空测试
   - 落地站立
   - 移动测试
   - 综合验证

✅ 阶段6: 实际运行（持续）
   - 日常使用
   - 参数调优
   - 数据分析
```

### 关键要点

```
🔑 模型文件：必须使用 exported/policy.pt
🔑 安全第一：遥控器就手，从悬空开始
🔑 循序渐进：悬空 → 站立 → 慢速 → 正常
🔑 实时监控：频率、扭矩、温度、姿态
🔑 参数保守：初始 action_scale = 0.1-0.15
🔑 记录数据：便于分析和优化
🔑 网络稳定：延迟 < 1ms，零丢包
🔑 持续改进：收集数据，迭代优化
```

### 性能指标参考

```
优秀性能：
- 控制频率: 48-52 Hz（稳定）
- 网络延迟: < 0.5 ms
- 站立稳定: > 5 分钟无问题
- 移动速度: > 0.5 m/s
- 关节温度: < 50°C
- 电池续航: > 30 分钟

可接受性能：
- 控制频率: 45-55 Hz
- 网络延迟: < 1 ms
- 站立稳定: > 2 分钟
- 移动速度: > 0.3 m/s
- 关节温度: < 60°C
- 电池续航: > 20 分钟

需要改进：
- 控制频率: < 45 Hz
- 网络延迟: > 2 ms
- 站立不稳定
- 移动速度: < 0.2 m/s
- 关节温度: > 65°C
```

### 下一步方向

```
1. 功能扩展
   □ 添加视觉感知
   □ 实现自主导航
   □ 多机器人协同
   □ 复杂地形适应

2. 性能优化
   □ Sim-to-real fine-tuning
   □ 使用 ONNX 加速推理
   □ 优化控制算法
   □ 降低功耗

3. 用户体验
   □ 开发图形界面
   □ 手机 App 控制
   □ 语音交互
   □ 云端监控

4. 安全增强
   □ 碰撞检测
   □ 自动跌倒恢复
   □ 电量预警
   □ 故障诊断
```

---

## 📚 参考资源

- **rl_sar 项目**: https://github.com/fan-ziqi/rl_sar
- **robot_lab 项目**: https://github.com/fan-ziqi/robot_lab
- **Isaac Lab 文档**: https://isaac-sim.github.io/IsaacLab/
- **Unitree 官方**: https://www.unitree.com/
- **Unitree SDK2**: https://github.com/unitreerobotics/unitree_sdk2
- **模型推理指南**: [ISAACLAB_MODEL_INFERENCE_GUIDE.md](ISAACLAB_MODEL_INFERENCE_GUIDE.md)

---

## 📞 获取帮助

遇到问题？

1. **查看故障排除章节**（本文档）
2. **查看项目 Issues**: https://github.com/fan-ziqi/rl_sar/issues
3. **加入讨论**: https://github.com/fan-ziqi/rl_sar/discussions
4. **联系作者**: 查看项目 README

---

**🎉 祝你部署成功！享受与 Go2 互动的乐趣！**

*最后更新: 2026-01-10*


