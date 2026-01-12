#!/bin/bash

# Unitree Go2 MuJoCo 仿真推理脚本
# 用于测试 robot_lab_pose_velocity 策略在 rough 环境中的表现

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}============================================${NC}"
echo -e "${BLUE}   Unitree Go2 MuJoCo 仿真推理${NC}"
echo -e "${BLUE}============================================${NC}"
echo ""

# 配置参数
ROBOT_NAME="go2"
SCENE_NAME="robot_lab"  # 场景名称，对应 policy/go2/robot_lab_pose_velocity/
EXECUTABLE="./cmake_build/bin/rl_sim_mujoco"

# 检查可执行文件
echo -e "${YELLOW}[1/3] 检查程序文件...${NC}"
if [ ! -f "$EXECUTABLE" ]; then
    echo -e "${RED}✗ 找不到: $EXECUTABLE${NC}"
    echo -e "${RED}  请先编译: ./build.sh -m${NC}"
    exit 1
fi
echo -e "${GREEN}✓ 程序文件存在${NC}"

# 检查策略文件
echo -e "${YELLOW}[2/3] 检查策略文件...${NC}"
POLICY_PATH="policy/${ROBOT_NAME}/${SCENE_NAME}_pose_velocity/policy.pt"
if [ ! -f "$POLICY_PATH" ]; then
    echo -e "${RED}✗ 找不到策略文件: $POLICY_PATH${NC}"
    echo -e "${RED}  请确保模型文件存在${NC}"
    exit 1
fi
echo -e "${GREEN}✓ 策略文件存在: $POLICY_PATH${NC}"

# 检查配置文件
CONFIG_PATH="policy/${ROBOT_NAME}/${SCENE_NAME}_pose_velocity/config.yaml"
if [ ! -f "$CONFIG_PATH" ]; then
    echo -e "${RED}✗ 找不到配置文件: $CONFIG_PATH${NC}"
    exit 1
fi
echo -e "${GREEN}✓ 配置文件存在: $CONFIG_PATH${NC}"

# 显示配置信息
echo ""
echo -e "${YELLOW}[3/3] 配置信息${NC}"
echo -e "${BLUE}--------------------------------------------${NC}"
echo "  机器人: $ROBOT_NAME"
echo "  场景:   $SCENE_NAME"
echo "  策略:   $POLICY_PATH"
echo "  配置:   $CONFIG_PATH"
echo -e "${BLUE}--------------------------------------------${NC}"

# 提取关键配置参数
NUM_OBS=$(grep "num_observations:" "$CONFIG_PATH" | awk '{print $2}')
ACTION_SCALE=$(grep -A 3 "action_scale:" "$CONFIG_PATH" | head -4)

echo ""
echo "关键参数:"
echo "  观测维度: $NUM_OBS"
echo "  动作缩放: (查看 config.yaml)"
echo ""

# 启动提示
echo -e "${YELLOW}============================================${NC}"
echo -e "${YELLOW}  即将启动 MuJoCo 仿真环境${NC}"
echo -e "${YELLOW}============================================${NC}"
echo ""
echo "控制说明:"
echo "  W/S/A/D    - 前进/后退/左转/右转"
echo "  空格       - 停止"
echo "  ESC/Q      - 退出"
echo "  R          - 重置"
echo ""
echo "MuJoCo 窗口将会弹出..."
echo ""
echo -e "${GREEN}按任意键开始，或 Ctrl+C 取消...${NC}"
read -n 1 -s

echo ""
echo -e "${GREEN}启动仿真...${NC}"
echo ""

# 运行仿真
$EXECUTABLE $ROBOT_NAME $SCENE_NAME

# 退出后的提示
echo ""
echo -e "${BLUE}============================================${NC}"
echo -e "${BLUE}  仿真已结束${NC}"
echo -e "${BLUE}============================================${NC}"
