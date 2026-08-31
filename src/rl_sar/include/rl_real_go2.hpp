/*
 * Copyright (c) 2024-2025 Ziqi Fan
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RL_REAL_GO2_HPP
#define RL_REAL_GO2_HPP

// #define PLOT
// #define CSV_LOGGER
// #define USE_ROS

#include "rl_sdk.hpp"
#include "observation_buffer.hpp"
#include "inference_runtime.hpp"
#include "loop.hpp"
#include "fsm_go2.hpp"
#include "fsm_go2_x5.hpp"
#include "fsm_go2w.hpp"

#include <unitree/robot/channel/channel_publisher.hpp>
#include <unitree/robot/channel/channel_subscriber.hpp>
#include <unitree/idl/go2/LowState_.hpp>
#include <unitree/idl/go2/LowCmd_.hpp>
#include <unitree/idl/go2/WirelessController_.hpp>
#include <unitree/common/time/time_tool.hpp>
#include <unitree/common/thread/thread.hpp>
#include <unitree/robot/b2/motion_switcher/motion_switcher_client.hpp>
#include <csignal>
#include <chrono>

#if defined(USE_ROS1) && defined(USE_ROS)
#include <ros/ros.h>
#include <geometry_msgs/Twist.h>
#include <std_msgs/Float32.h>
#include <std_msgs/Float32MultiArray.h>
#elif defined(USE_ROS2) && defined(USE_ROS)
#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/twist.hpp>
#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>
#endif

#include "matplotlibcpp.h"
namespace plt = matplotlibcpp;

using namespace unitree::common;
using namespace unitree::robot;
using namespace unitree::robot::b2;
#define TOPIC_LOWCMD "rt/lowcmd"
#define TOPIC_LOWSTATE "rt/lowstate"
#define TOPIC_JOYSTICK "rt/wirelesscontroller"
constexpr double PosStopF = (2.146E+9f);
constexpr double VelStopF = (16000.0f);

// union for joystick keys
typedef union
{
    struct
    {
        uint8_t R1 : 1;
        uint8_t L1 : 1;
        uint8_t start : 1;
        uint8_t select : 1;
        uint8_t R2 : 1;
        uint8_t L2 : 1;
        uint8_t F1 : 1;
        uint8_t F2 : 1;
        uint8_t A : 1;
        uint8_t B : 1;
        uint8_t X : 1;
        uint8_t Y : 1;
        uint8_t up : 1;
        uint8_t right : 1;
        uint8_t down : 1;
        uint8_t left : 1;
    } components;
    uint16_t value;
} xKeySwitchUnion;

class RL_Real : public RL
{
public:
    RL_Real(int argc, char **argv);
    ~RL_Real();

#if defined(USE_ROS2) && defined(USE_ROS)
    std::shared_ptr<rclcpp::Node> ros2_node;
#endif

private:
    using SteadyTime = std::chrono::steady_clock::time_point;

    struct ExternalObservationState
    {
        std::vector<float> lin_vel = std::vector<float>(3, 0.0f);
        float base_height = 0.0f;
        std::vector<float> body_pose = std::vector<float>(2, 0.0f); // pitch, roll
        std::vector<float> arm_dof_pos;
        std::vector<float> arm_dof_vel;
        bool have_lin_vel = false;
        bool have_base_height = false;
        bool have_body_pose = false;
        bool have_arm_dof_pos = false;
        bool have_arm_dof_vel = false;
        SteadyTime lin_vel_stamp;
        SteadyTime base_height_stamp;
        SteadyTime body_pose_stamp;
        SteadyTime arm_dof_pos_stamp;
        SteadyTime arm_dof_vel_stamp;
    };

    // rl functions
    std::vector<float> Forward() override;
    void GetState(RobotState<float> *state) override;
    void SetCommand(const RobotCommand<float> *command) override;
    void RunModel();
    void RobotControl();

    // loop
    std::shared_ptr<LoopFunc> loop_keyboard;
    std::shared_ptr<LoopFunc> loop_control;
    std::shared_ptr<LoopFunc> loop_rl;
    std::shared_ptr<LoopFunc> loop_plot;

    // plot
    const int plot_size = 100;
    std::vector<int> plot_t;
    std::vector<std::vector<float>> plot_real_joint_pos, plot_target_joint_pos;
    void Plot();

    // unitree interface
    void InitLowCmd();
    int QueryMotionStatus();
    std::string QueryServiceName(std::string form, std::string name);
    uint32_t Crc32Core(uint32_t *ptr, uint32_t len);
    void LowStateMessageHandler(const void *messages);
    void JoystickHandler(const void *message);
    MotionSwitcherClient msc;
    unitree_go::msg::dds_::LowCmd_ unitree_low_command{};
    unitree_go::msg::dds_::LowState_ unitree_low_state{};
    unitree_go::msg::dds_::WirelessController_ joystick{};
    ChannelPublisherPtr<unitree_go::msg::dds_::LowCmd_> lowcmd_publisher;
    ChannelSubscriberPtr<unitree_go::msg::dds_::LowState_> lowstate_subscriber;
    ChannelSubscriberPtr<unitree_go::msg::dds_::WirelessController_> joystick_subscriber;
    xKeySwitchUnion unitree_joy;

    // others
    std::vector<float> mapped_joint_positions;
    std::vector<float> mapped_joint_velocities;
    bool x5_mode_ = false;
    mutable std::mutex external_obs_mutex_;
    ExternalObservationState external_obs_;
    SteadyTime last_external_obs_warning_{};
    bool CopyExternalObservations(RobotState<float> *state = nullptr);
    void WarnExternalObservations(const std::string &reason);

#if defined(USE_ROS1) && defined(USE_ROS)
    geometry_msgs::Twist cmd_vel;
    ros::Subscriber cmd_vel_subscriber;
    ros::Subscriber lin_vel_subscriber;
    ros::Subscriber base_height_subscriber;
    ros::Subscriber body_pose_subscriber;
    ros::Subscriber arm_dof_pos_subscriber;
    ros::Subscriber arm_dof_vel_subscriber;
    void CmdvelCallback(const geometry_msgs::Twist::ConstPtr &msg);
    void LinVelCallback(const std_msgs::Float32MultiArray::ConstPtr &msg);
    void BaseHeightCallback(const std_msgs::Float32::ConstPtr &msg);
    void BodyPoseCallback(const std_msgs::Float32MultiArray::ConstPtr &msg);
    void ArmDofPosCallback(const std_msgs::Float32MultiArray::ConstPtr &msg);
    void ArmDofVelCallback(const std_msgs::Float32MultiArray::ConstPtr &msg);
#elif defined(USE_ROS2) && defined(USE_ROS)
    geometry_msgs::msg::Twist cmd_vel;
    rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_subscriber;
    rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr lin_vel_subscriber;
    rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr base_height_subscriber;
    rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr body_pose_subscriber;
    rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr arm_dof_pos_subscriber;
    rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr arm_dof_vel_subscriber;
    void CmdvelCallback(const geometry_msgs::msg::Twist::SharedPtr msg);
    void LinVelCallback(const std_msgs::msg::Float32MultiArray::SharedPtr msg);
    void BaseHeightCallback(const std_msgs::msg::Float32::SharedPtr msg);
    void BodyPoseCallback(const std_msgs::msg::Float32MultiArray::SharedPtr msg);
    void ArmDofPosCallback(const std_msgs::msg::Float32MultiArray::SharedPtr msg);
    void ArmDofVelCallback(const std_msgs::msg::Float32MultiArray::SharedPtr msg);
#endif
};

#endif // RL_REAL_GO2_HPP
