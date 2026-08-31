/*
 * Copyright (c) 2024-2025 Ziqi Fan
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rl_real_go2.hpp"

#include <cmath>

namespace
{
template <typename Container>
bool AllFinite(const Container &values)
{
    return std::all_of(values.begin(), values.end(),
                       [](float value) { return std::isfinite(value); });
}
}

RL_Real::RL_Real(int argc, char **argv)
{
    bool wheel_mode = (argc > 2 && std::string(argv[2]) == "wheel");
    this->x5_mode_ = (argc > 2 && std::string(argv[2]) == "x5");

    // Read the robot config before constructing observation subscribers: the
    // Go2-X5 topic names are configurable in policy/go2_x5/base.yaml.
    this->ang_vel_axis = "body";
    this->robot_name = this->x5_mode_ ? "go2_x5" : (wheel_mode ? "go2w" : "go2");
    this->ReadYaml(this->robot_name, "base.yaml");

#if defined(USE_ROS1) && defined(USE_ROS)
    ros::NodeHandle nh;
    this->cmd_vel_subscriber = nh.subscribe<geometry_msgs::Twist>("/cmd_vel", 10, &RL_Real::CmdvelCallback, this);
    if (this->x5_mode_)
    {
        this->lin_vel_subscriber = nh.subscribe<std_msgs::Float32MultiArray>(
            this->params.Get<std::string>("lin_vel_topic"), 1, &RL_Real::LinVelCallback, this);
        this->base_height_subscriber = nh.subscribe<std_msgs::Float32>(
            this->params.Get<std::string>("base_height_topic"), 1, &RL_Real::BaseHeightCallback, this);
        this->body_pose_subscriber = nh.subscribe<std_msgs::Float32MultiArray>(
            this->params.Get<std::string>("body_pose_actual_topic"), 1, &RL_Real::BodyPoseCallback, this);
        this->arm_dof_pos_subscriber = nh.subscribe<std_msgs::Float32MultiArray>(
            this->params.Get<std::string>("arm_dof_pos_topic"), 1, &RL_Real::ArmDofPosCallback, this);
        this->arm_dof_vel_subscriber = nh.subscribe<std_msgs::Float32MultiArray>(
            this->params.Get<std::string>("arm_dof_vel_topic"), 1, &RL_Real::ArmDofVelCallback, this);
    }
#elif defined(USE_ROS2) && defined(USE_ROS)
    ros2_node = std::make_shared<rclcpp::Node>("rl_real_node");
    this->cmd_vel_subscriber = ros2_node->create_subscription<geometry_msgs::msg::Twist>(
        "/cmd_vel", rclcpp::SystemDefaultsQoS(),
        [this] (const geometry_msgs::msg::Twist::SharedPtr msg) {this->CmdvelCallback(msg);}
    );
    if (this->x5_mode_)
    {
        const auto qos = rclcpp::SensorDataQoS();
        this->lin_vel_subscriber = ros2_node->create_subscription<std_msgs::msg::Float32MultiArray>(
            this->params.Get<std::string>("lin_vel_topic"), qos,
            [this](const std_msgs::msg::Float32MultiArray::SharedPtr msg) { this->LinVelCallback(msg); });
        this->base_height_subscriber = ros2_node->create_subscription<std_msgs::msg::Float32>(
            this->params.Get<std::string>("base_height_topic"), qos,
            [this](const std_msgs::msg::Float32::SharedPtr msg) { this->BaseHeightCallback(msg); });
        this->body_pose_subscriber = ros2_node->create_subscription<std_msgs::msg::Float32MultiArray>(
            this->params.Get<std::string>("body_pose_actual_topic"), qos,
            [this](const std_msgs::msg::Float32MultiArray::SharedPtr msg) { this->BodyPoseCallback(msg); });
        this->arm_dof_pos_subscriber = ros2_node->create_subscription<std_msgs::msg::Float32MultiArray>(
            this->params.Get<std::string>("arm_dof_pos_topic"), qos,
            [this](const std_msgs::msg::Float32MultiArray::SharedPtr msg) { this->ArmDofPosCallback(msg); });
        this->arm_dof_vel_subscriber = ros2_node->create_subscription<std_msgs::msg::Float32MultiArray>(
            this->params.Get<std::string>("arm_dof_vel_topic"), qos,
            [this](const std_msgs::msg::Float32MultiArray::SharedPtr msg) { this->ArmDofVelCallback(msg); });
    }
#endif

    // auto load FSM by robot_name
    if (FSMManager::GetInstance().IsTypeSupported(this->robot_name))
    {
        auto fsm_ptr = FSMManager::GetInstance().CreateFSM(this->robot_name, this);
        if (fsm_ptr)
        {
            this->fsm = *fsm_ptr;
        }
    }
    else
    {
        std::cout << LOGGER::ERROR << "[FSM] No FSM registered for robot: " << this->robot_name << std::endl;
    }

    // init robot
    this->InitLowCmd();
    this->InitJointNum(this->params.Get<int>("num_of_dofs"));
    this->InitOutputs();
    this->InitControl();
    // create lowcmd publisher
    this->lowcmd_publisher.reset(new ChannelPublisher<unitree_go::msg::dds_::LowCmd_>(TOPIC_LOWCMD));
    this->lowcmd_publisher->InitChannel();
    // create lowstate subscriber
    this->lowstate_subscriber.reset(new ChannelSubscriber<unitree_go::msg::dds_::LowState_>(TOPIC_LOWSTATE));
    this->lowstate_subscriber->InitChannel(std::bind(&RL_Real::LowStateMessageHandler, this, std::placeholders::_1), 1);
    // create joystick subscriber
    this->joystick_subscriber.reset(new ChannelSubscriber<unitree_go::msg::dds_::WirelessController_>(TOPIC_JOYSTICK));
    this->joystick_subscriber->InitChannel(std::bind(&RL_Real::JoystickHandler, this, std::placeholders::_1), 1);
    // init MotionSwitcherClient
    this->msc.SetTimeout(10.0f);
    this->msc.Init();
    // Shut down motion control-related service
    while(this->QueryMotionStatus())
    {
        std::cout << "Try to deactivate the motion control-related service." << std::endl;
        int32_t ret = this->msc.ReleaseMode();
        if (ret == 0)
        {
            std::cout << "ReleaseMode succeeded." << std::endl;
        }
        else
        {
            std::cout << "ReleaseMode failed. Error code: " << ret << std::endl;
        }
        sleep(1);
    }

    // loop
    this->loop_keyboard = std::make_shared<LoopFunc>("loop_keyboard", 0.05, std::bind(&RL_Real::KeyboardInterface, this));
    this->loop_control = std::make_shared<LoopFunc>("loop_control", this->params.Get<float>("dt"), std::bind(&RL_Real::RobotControl, this));
    this->loop_rl = std::make_shared<LoopFunc>("loop_rl", this->params.Get<float>("dt") * this->params.Get<int>("decimation"), std::bind(&RL_Real::RunModel, this));
    this->loop_keyboard->start();
    this->loop_control->start();
    this->loop_rl->start();

#ifdef PLOT
    this->plot_t = std::vector<int>(this->plot_size, 0);
    this->plot_real_joint_pos.resize(this->params.Get<int>("num_of_dofs"));
    this->plot_target_joint_pos.resize(this->params.Get<int>("num_of_dofs"));
    for (auto &vector : this->plot_real_joint_pos) { vector = std::vector<float>(this->plot_size, 0); }
    for (auto &vector : this->plot_target_joint_pos) { vector = std::vector<float>(this->plot_size, 0); }
    this->loop_plot = std::make_shared<LoopFunc>("loop_plot", 0.002, std::bind(&RL_Real::Plot, this));
    this->loop_plot->start();
#endif
#ifdef CSV_LOGGER
    this->CSVInit(this->robot_name);
#endif
}

RL_Real::~RL_Real()
{
    this->loop_keyboard->shutdown();
    this->loop_control->shutdown();
    this->loop_rl->shutdown();
#ifdef PLOT
    this->loop_plot->shutdown();
#endif
    std::cout << LOGGER::INFO << "RL_Real exit" << std::endl;
}

void RL_Real::GetState(RobotState<float> *state)
{
    if (this->unitree_joy.components.A) this->control.SetGamepad(Input::Gamepad::A);
    if (this->unitree_joy.components.B) this->control.SetGamepad(Input::Gamepad::B);
    if (this->unitree_joy.components.X) this->control.SetGamepad(Input::Gamepad::X);
    if (this->unitree_joy.components.Y) this->control.SetGamepad(Input::Gamepad::Y);
    if (this->unitree_joy.components.L1) this->control.SetGamepad(Input::Gamepad::LB);
    if (this->unitree_joy.components.R1) this->control.SetGamepad(Input::Gamepad::RB);
    if (this->unitree_joy.components.F1) this->control.SetGamepad(Input::Gamepad::LStick);
    if (this->unitree_joy.components.F2) this->control.SetGamepad(Input::Gamepad::RStick);
    if (this->unitree_joy.components.up) this->control.SetGamepad(Input::Gamepad::DPadUp);
    if (this->unitree_joy.components.down) this->control.SetGamepad(Input::Gamepad::DPadDown);
    if (this->unitree_joy.components.left) this->control.SetGamepad(Input::Gamepad::DPadLeft);
    if (this->unitree_joy.components.right) this->control.SetGamepad(Input::Gamepad::DPadRight);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.A) this->control.SetGamepad(Input::Gamepad::LB_A);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.B) this->control.SetGamepad(Input::Gamepad::LB_B);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.X) this->control.SetGamepad(Input::Gamepad::LB_X);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.Y) this->control.SetGamepad(Input::Gamepad::LB_Y);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.F1) this->control.SetGamepad(Input::Gamepad::LB_LStick);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.F2) this->control.SetGamepad(Input::Gamepad::LB_RStick);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.up) this->control.SetGamepad(Input::Gamepad::LB_DPadUp);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.down) this->control.SetGamepad(Input::Gamepad::LB_DPadDown);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.left) this->control.SetGamepad(Input::Gamepad::LB_DPadLeft);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.right) this->control.SetGamepad(Input::Gamepad::LB_DPadRight);
    if (this->unitree_joy.components.R1 && this->unitree_joy.components.A) this->control.SetGamepad(Input::Gamepad::RB_A);
    if (this->unitree_joy.components.R1 && this->unitree_joy.components.B) this->control.SetGamepad(Input::Gamepad::RB_B);
    if (this->unitree_joy.components.R1 && this->unitree_joy.components.X) this->control.SetGamepad(Input::Gamepad::RB_X);
    if (this->unitree_joy.components.R1 && this->unitree_joy.components.Y) this->control.SetGamepad(Input::Gamepad::RB_Y);
    if (this->unitree_joy.components.R1 && this->unitree_joy.components.F1) this->control.SetGamepad(Input::Gamepad::RB_LStick);
    if (this->unitree_joy.components.R1 && this->unitree_joy.components.F2) this->control.SetGamepad(Input::Gamepad::RB_RStick);
    if (this->unitree_joy.components.R1 && this->unitree_joy.components.up) this->control.SetGamepad(Input::Gamepad::RB_DPadUp);
    if (this->unitree_joy.components.R1 && this->unitree_joy.components.down) this->control.SetGamepad(Input::Gamepad::RB_DPadDown);
    if (this->unitree_joy.components.R1 && this->unitree_joy.components.left) this->control.SetGamepad(Input::Gamepad::RB_DPadLeft);
    if (this->unitree_joy.components.R1 && this->unitree_joy.components.right) this->control.SetGamepad(Input::Gamepad::RB_DPadRight);
    if (this->unitree_joy.components.L1 && this->unitree_joy.components.R1) this->control.SetGamepad(Input::Gamepad::LB_RB);

    this->control.x = this->joystick.ly();
    this->control.y = -this->joystick.lx();
    this->control.yaw = -this->joystick.rx();

    state->imu.quaternion[0] = this->unitree_low_state.imu_state().quaternion()[0]; // w
    state->imu.quaternion[1] = this->unitree_low_state.imu_state().quaternion()[1]; // x
    state->imu.quaternion[2] = this->unitree_low_state.imu_state().quaternion()[2]; // y
    state->imu.quaternion[3] = this->unitree_low_state.imu_state().quaternion()[3]; // z

    for (int i = 0; i < 3; ++i)
    {
        state->imu.gyroscope[i] = this->unitree_low_state.imu_state().gyroscope()[i];
    }
    const int unitree_dofs = this->x5_mode_ ? this->params.Get<int>("num_leg_dofs")
                                           : this->params.Get<int>("num_of_dofs");
    for (int i = 0; i < unitree_dofs; ++i)
    {
        state->motor_state.q[i] = this->unitree_low_state.motor_state()[this->params.Get<std::vector<int>>("joint_mapping")[i]].q();
        state->motor_state.dq[i] = this->unitree_low_state.motor_state()[this->params.Get<std::vector<int>>("joint_mapping")[i]].dq();
        state->motor_state.tau_est[i] = this->unitree_low_state.motor_state()[this->params.Get<std::vector<int>>("joint_mapping")[i]].tau_est();
    }
    if (this->x5_mode_)
    {
        // The arm encoders are not part of Go2 LowState. Overlay the most
        // recent ROS sample into the policy-order motor state.
        this->CopyExternalObservations(state);
    }
}

void RL_Real::SetCommand(const RobotCommand<float> *command)
{
    unitree_go::msg::dds_::LowCmd_ dds_low_command;
    dds_low_command.head()[0] = 0xFE;
    dds_low_command.head()[1] = 0xEF;
    dds_low_command.level_flag() = 0xFF;
    dds_low_command.gpio() = 0;

    for (int i = 0; i < 20; ++i)
    {
        dds_low_command.motor_cmd()[i].mode() = 0x01;
        dds_low_command.motor_cmd()[i].q() = PosStopF;
        dds_low_command.motor_cmd()[i].kp() = 0;
        dds_low_command.motor_cmd()[i].dq() = VelStopF;
        dds_low_command.motor_cmd()[i].kd() = 0;
        dds_low_command.motor_cmd()[i].tau() = 0;
    }

    for (int i = 0; i < this->params.Get<int>("num_of_dofs"); ++i)
    {
        dds_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].mode() = 0x01;
        dds_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].q() = command->motor_command.q[i];
        dds_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].dq() = command->motor_command.dq[i];
        dds_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].kp() = command->motor_command.kp[i];
        dds_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].kd() = command->motor_command.kd[i];
        dds_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].tau() = command->motor_command.tau[i];
    }

    dds_low_command.crc() = Crc32Core((uint32_t *)&dds_low_command, (sizeof(unitree_go::msg::dds_::LowCmd_) >> 2) - 1);
    lowcmd_publisher->Write(dds_low_command);

#ifdef PLOT
    this->unitree_low_command = dds_low_command;
#endif
}

void RL_Real::RobotControl()
{
    this->GetState(&this->robot_state);

    this->StateController(&this->robot_state, &this->robot_command);

    this->control.ClearInput();

    this->SetCommand(&this->robot_command);
}

void RL_Real::RunModel()
{
    if (this->rl_init_done)
    {
        if (this->x5_mode_ && !this->CopyExternalObservations())
        {
            return;
        }
        this->episode_length_buf += 1;
        this->obs.ang_vel = this->robot_state.imu.gyroscope;
        this->obs.commands = {this->control.x, this->control.y, this->control.yaw};
#if !defined(USE_CMAKE) && defined(USE_ROS)
        if (this->control.navigation_mode)
        {
            this->obs.commands = {(float)this->cmd_vel.linear.x, (float)this->cmd_vel.linear.y, (float)this->cmd_vel.angular.z};

        }
#endif
        this->obs.base_quat = this->robot_state.imu.quaternion;
        this->obs.dof_pos = this->robot_state.motor_state.q;
        this->obs.dof_vel = this->robot_state.motor_state.dq;

        this->obs.actions = this->Forward();
        if (this->x5_mode_)
        {
            const int num_leg_dofs = this->params.Get<int>("num_leg_dofs");
            const int num_of_dofs = this->params.Get<int>("num_of_dofs");
            if (static_cast<int>(this->obs.actions.size()) != num_leg_dofs)
            {
                std::cout << std::endl << LOGGER::ERROR
                          << "[Go2-X5] policy returned " << this->obs.actions.size()
                          << " actions, expected " << num_leg_dofs << std::endl;
                return;
            }
            // RoboDuet stage 1 controls the 12 legs only. The generic output
            // path spans all 18 joints, so preserve the policy actions at the
            // front and explicitly zero-pad the six arm action slots.
            this->obs.actions.resize(num_of_dofs, 0.0f);
        }
        this->ComputeOutput(this->obs.actions, this->output_dof_pos, this->output_dof_vel, this->output_dof_tau);

        if (!this->output_dof_pos.empty())
        {
            output_dof_pos_queue.push(this->output_dof_pos);
        }
        if (!this->output_dof_vel.empty())
        {
            output_dof_vel_queue.push(this->output_dof_vel);
        }
        if (!this->output_dof_tau.empty())
        {
            output_dof_tau_queue.push(this->output_dof_tau);
        }

        // this->TorqueProtect(this->output_dof_tau);
        // this->AttitudeProtect(this->robot_state.imu.quaternion, 75.0f, 75.0f);

#ifdef CSV_LOGGER
        std::vector<float> tau_est = this->robot_state.motor_state.tau_est;
        this->CSVLogger(this->output_dof_tau, tau_est, this->obs.dof_pos, this->output_dof_pos, this->obs.dof_vel);
#endif
    }
}

std::vector<float> RL_Real::Forward()
{
    std::unique_lock<std::mutex> lock(this->model_mutex, std::try_to_lock);

    // If model is being reinitialized, return previous actions to avoid blocking
    if (!lock.owns_lock())
    {
        std::cout << LOGGER::WARNING << "Model is being reinitialized, using previous actions" << std::endl;
        return this->obs.actions;
    }

    std::vector<float> clamped_obs = this->ComputeObservation();

    std::vector<float> actions;
    if (!this->params.Get<std::vector<int>>("observations_history").empty())
    {
        this->history_obs_buf.insert(clamped_obs);
        this->history_obs = this->history_obs_buf.get_obs_vec(this->params.Get<std::vector<int>>("observations_history"));
        actions = this->model->forward({this->history_obs});
    }
    else
    {
        actions = this->model->forward({clamped_obs});
    }

    if (!this->params.Get<std::vector<float>>("clip_actions_upper").empty() && !this->params.Get<std::vector<float>>("clip_actions_lower").empty())
    {
        return clamp(actions, this->params.Get<std::vector<float>>("clip_actions_lower"), this->params.Get<std::vector<float>>("clip_actions_upper"));
    }
    else
    {
        return actions;
    }
}

void RL_Real::Plot()
{
    this->plot_t.erase(this->plot_t.begin());
    this->plot_t.push_back(this->motiontime);
    plt::cla();
    plt::clf();
    for (int i = 0; i < this->params.Get<int>("num_of_dofs"); ++i)
    {
        this->plot_real_joint_pos[i].erase(this->plot_real_joint_pos[i].begin());
        this->plot_target_joint_pos[i].erase(this->plot_target_joint_pos[i].begin());
        this->plot_real_joint_pos[i].push_back(this->unitree_low_state.motor_state()[i].q());
        this->plot_target_joint_pos[i].push_back(this->unitree_low_command.motor_cmd()[i].q());
        plt::subplot(this->params.Get<int>("num_of_dofs"), 1, i + 1);
        plt::named_plot("_real_joint_pos", this->plot_t, this->plot_real_joint_pos[i], "r");
        plt::named_plot("_target_joint_pos", this->plot_t, this->plot_target_joint_pos[i], "b");
        plt::xlim(this->plot_t.front(), this->plot_t.back());
    }
    // plt::legend();
    plt::pause(0.0001);
}

uint32_t RL_Real::Crc32Core(uint32_t *ptr, uint32_t len)
{
    unsigned int xbit = 0;
    unsigned int data = 0;
    unsigned int CRC32 = 0xFFFFFFFF;
    const unsigned int dwPolynomial = 0x04c11db7;

    for (unsigned int i = 0; i < len; ++i)
    {
        xbit = 1 << 31;
        data = ptr[i];
        for (unsigned int bits = 0; bits < 32; bits++)
        {
            if (CRC32 & 0x80000000)
            {
                CRC32 <<= 1;
                CRC32 ^= dwPolynomial;
            }
            else
            {
                CRC32 <<= 1;
            }

            if (data & xbit)
            {
                CRC32 ^= dwPolynomial;
            }
            xbit >>= 1;
        }
    }

    return CRC32;
}

void RL_Real::InitLowCmd()
{
    this->unitree_low_command.head()[0] = 0xFE;
    this->unitree_low_command.head()[1] = 0xEF;
    this->unitree_low_command.level_flag() = 0xFF;
    this->unitree_low_command.gpio() = 0;

    for (int i = 0; i < 20; ++i)
    {
        this->unitree_low_command.motor_cmd()[i].mode() = (0x01); // motor switch to servo (PMSM) mode
        this->unitree_low_command.motor_cmd()[i].q() = (PosStopF);
        this->unitree_low_command.motor_cmd()[i].kp() = (0);
        this->unitree_low_command.motor_cmd()[i].dq() = (VelStopF);
        this->unitree_low_command.motor_cmd()[i].kd() = (0);
        this->unitree_low_command.motor_cmd()[i].tau() = (0);
    }
}

int RL_Real::QueryMotionStatus()
{
    std::string robotForm, motionName;
    int motionStatus;
    int32_t ret = this->msc.CheckMode(robotForm, motionName);
    if (ret == 0)
    {
        std::cout << "CheckMode succeeded." << std::endl;
    }
    else
    {
        std::cout << "CheckMode failed. Error code: " << ret << std::endl;
    }
    if (motionName.empty())
    {
        std::cout << "The motion control-related service is deactivated." << std::endl;
        motionStatus = 0;
    }
    else
    {
        std::string serviceName = QueryServiceName(robotForm, motionName);
        std::cout << "Service: " << serviceName << " is activate" << std::endl;
        motionStatus = 1;
    }
    return motionStatus;
}

std::string RL_Real::QueryServiceName(std::string form, std::string name)
{
    if (form == "0")
    {
        if (name == "normal" )   return "sport_mode";
        if (name == "ai" )       return "ai_sport";
        if (name == "advanced" ) return "advanced_sport";
    }
    else
    {
        if (name == "ai-w" )     return "wheeled_sport(go2W)";
        if (name == "normal-w" ) return "wheeled_sport(b2W)";
    }
    return "";
}

void RL_Real::LowStateMessageHandler(const void *message)
{
    this->unitree_low_state = *(unitree_go::msg::dds_::LowState_ *)message;
}

void RL_Real::JoystickHandler(const void *message)
{
    joystick = *(unitree_go::msg::dds_::WirelessController_ *)message;
    this->unitree_joy.value = joystick.keys();
}

#if !defined(USE_CMAKE) && defined(USE_ROS)
void RL_Real::CmdvelCallback(
#if defined(USE_ROS1) && defined(USE_ROS)
    const geometry_msgs::Twist::ConstPtr &msg
#elif defined(USE_ROS2) && defined(USE_ROS)
    const geometry_msgs::msg::Twist::SharedPtr msg
#endif
)
{
    this->cmd_vel = *msg;
}

void RL_Real::LinVelCallback(
#if defined(USE_ROS1) && defined(USE_ROS)
    const std_msgs::Float32MultiArray::ConstPtr &msg
#elif defined(USE_ROS2) && defined(USE_ROS)
    const std_msgs::msg::Float32MultiArray::SharedPtr msg
#endif
)
{
    if (msg->data.size() != 3 || !AllFinite(msg->data))
    {
        this->WarnExternalObservations("lin_vel must contain exactly 3 finite floats");
        return;
    }
    std::lock_guard<std::mutex> lock(this->external_obs_mutex_);
    this->external_obs_.lin_vel.assign(msg->data.begin(), msg->data.end());
    this->external_obs_.have_lin_vel = true;
    this->external_obs_.lin_vel_stamp = std::chrono::steady_clock::now();
}

void RL_Real::BaseHeightCallback(
#if defined(USE_ROS1) && defined(USE_ROS)
    const std_msgs::Float32::ConstPtr &msg
#elif defined(USE_ROS2) && defined(USE_ROS)
    const std_msgs::msg::Float32::SharedPtr msg
#endif
)
{
    if (!std::isfinite(msg->data))
    {
        this->WarnExternalObservations("base_height must be finite");
        return;
    }
    std::lock_guard<std::mutex> lock(this->external_obs_mutex_);
    this->external_obs_.base_height = msg->data;
    this->external_obs_.have_base_height = true;
    this->external_obs_.base_height_stamp = std::chrono::steady_clock::now();
}

void RL_Real::BodyPoseCallback(
#if defined(USE_ROS1) && defined(USE_ROS)
    const std_msgs::Float32MultiArray::ConstPtr &msg
#elif defined(USE_ROS2) && defined(USE_ROS)
    const std_msgs::msg::Float32MultiArray::SharedPtr msg
#endif
)
{
    if ((msg->data.size() != 2 && msg->data.size() != 3) || !AllFinite(msg->data))
    {
        this->WarnExternalObservations("body_pose_actual must contain finite [pitch, roll] or [height, pitch, roll]");
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(this->external_obs_mutex_);
    const size_t pose_offset = msg->data.size() == 3 ? 1 : 0;
    if (msg->data.size() == 3)
    {
        this->external_obs_.base_height = msg->data[0];
        this->external_obs_.have_base_height = true;
        this->external_obs_.base_height_stamp = now;
    }
    this->external_obs_.body_pose = {msg->data[pose_offset], msg->data[pose_offset + 1]};
    this->external_obs_.have_body_pose = true;
    this->external_obs_.body_pose_stamp = now;
}

void RL_Real::ArmDofPosCallback(
#if defined(USE_ROS1) && defined(USE_ROS)
    const std_msgs::Float32MultiArray::ConstPtr &msg
#elif defined(USE_ROS2) && defined(USE_ROS)
    const std_msgs::msg::Float32MultiArray::SharedPtr msg
#endif
)
{
    const int expected = this->params.Get<int>("num_arm_dofs");
    if (static_cast<int>(msg->data.size()) != expected || !AllFinite(msg->data))
    {
        this->WarnExternalObservations("arm_dof_pos must contain num_arm_dofs finite values");
        return;
    }
    std::lock_guard<std::mutex> lock(this->external_obs_mutex_);
    this->external_obs_.arm_dof_pos.assign(msg->data.begin(), msg->data.end());
    this->external_obs_.have_arm_dof_pos = true;
    this->external_obs_.arm_dof_pos_stamp = std::chrono::steady_clock::now();
}

void RL_Real::ArmDofVelCallback(
#if defined(USE_ROS1) && defined(USE_ROS)
    const std_msgs::Float32MultiArray::ConstPtr &msg
#elif defined(USE_ROS2) && defined(USE_ROS)
    const std_msgs::msg::Float32MultiArray::SharedPtr msg
#endif
)
{
    const int expected = this->params.Get<int>("num_arm_dofs");
    if (static_cast<int>(msg->data.size()) != expected || !AllFinite(msg->data))
    {
        this->WarnExternalObservations("arm_dof_vel must contain num_arm_dofs finite values");
        return;
    }
    std::lock_guard<std::mutex> lock(this->external_obs_mutex_);
    this->external_obs_.arm_dof_vel.assign(msg->data.begin(), msg->data.end());
    this->external_obs_.have_arm_dof_vel = true;
    this->external_obs_.arm_dof_vel_stamp = std::chrono::steady_clock::now();
}
#endif

bool RL_Real::CopyExternalObservations(RobotState<float> *state)
{
    const auto now = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(this->external_obs_mutex_);

    // GetState only merges the separately received arm state. Readiness is
    // enforced immediately before inference by the state == nullptr path.
    if (state != nullptr)
    {
        const int arm_begin = this->params.Get<int>("num_leg_dofs");
        if (this->external_obs_.have_arm_dof_pos)
        {
            std::copy(this->external_obs_.arm_dof_pos.begin(), this->external_obs_.arm_dof_pos.end(),
                      state->motor_state.q.begin() + arm_begin);
        }
        if (this->external_obs_.have_arm_dof_vel)
        {
            std::copy(this->external_obs_.arm_dof_vel.begin(), this->external_obs_.arm_dof_vel.end(),
                      state->motor_state.dq.begin() + arm_begin);
        }
        return true;
    }

    std::string reason;
    if (!this->external_obs_.have_lin_vel) reason += " lin_vel";
    if (!this->external_obs_.have_base_height) reason += " base_height";
    if (!this->external_obs_.have_body_pose) reason += " body_pose_actual";
    if (!this->external_obs_.have_arm_dof_pos) reason += " arm_dof_pos";
    if (!this->external_obs_.have_arm_dof_vel) reason += " arm_dof_vel";
    if (!reason.empty())
    {
        lock.unlock();
        this->WarnExternalObservations("waiting for:" + reason);
        return false;
    }

    const float timeout = this->params.Get<float>("external_obs_timeout", 0.2f);
    auto stale = [&](const SteadyTime &stamp)
    {
        return std::chrono::duration<float>(now - stamp).count() > timeout;
    };
    if (stale(this->external_obs_.lin_vel_stamp) ||
        stale(this->external_obs_.base_height_stamp) ||
        stale(this->external_obs_.body_pose_stamp) ||
        stale(this->external_obs_.arm_dof_pos_stamp) ||
        stale(this->external_obs_.arm_dof_vel_stamp))
    {
        lock.unlock();
        this->WarnExternalObservations("one or more topics exceeded external_obs_timeout");
        return false;
    }

    this->obs.lin_vel = this->external_obs_.lin_vel;
    this->obs.base_height = {this->external_obs_.base_height};
    this->obs.body_pose_actual = {
        this->external_obs_.base_height,
        this->external_obs_.body_pose[0],
        this->external_obs_.body_pose[1]};
    return true;
}

void RL_Real::WarnExternalObservations(const std::string &reason)
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(this->external_obs_mutex_);
    if (this->last_external_obs_warning_.time_since_epoch().count() == 0 ||
        std::chrono::duration<float>(now - this->last_external_obs_warning_).count() >= 1.0f)
    {
        std::cout << std::endl << LOGGER::WARNING
                  << "[Go2-X5 Obs] " << reason << "; policy inference paused" << std::endl;
        this->last_external_obs_warning_ = now;
    }
}

#if defined(USE_ROS1) && defined(USE_ROS)
void signalHandler(int signum)
{
    ros::shutdown();
    exit(0);
}
#elif defined(USE_CMAKE) || !defined(USE_ROS)
// Signal handler for CMAKE mode
volatile sig_atomic_t g_shutdown_requested = 0;
void signalHandler(int signum)
{
    std::cout << LOGGER::INFO << "Received signal " << signum << ", shutting down..." << std::endl;
    g_shutdown_requested = 1;
}
#endif

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cout << LOGGER::ERROR << "Usage: " << argv[0] << " networkInterface [wheel|x5]" << std::endl;
        throw std::runtime_error("Invalid arguments");
    }
    ChannelFactory::Instance()->Init(0, argv[1]);

#if defined(USE_ROS1) && defined(USE_ROS)
    signal(SIGINT, signalHandler);
    ros::init(argc, argv, "rl_sar");
    RL_Real rl_sar(argc, argv);
    ros::spin();
#elif defined(USE_ROS2) && defined(USE_ROS)
    rclcpp::init(argc, argv);
    auto rl_sar = std::make_shared<RL_Real>(argc, argv);
    rclcpp::spin(rl_sar->ros2_node);
    rclcpp::shutdown();
#elif defined(USE_CMAKE) || !defined(USE_ROS)
    signal(SIGINT, signalHandler);
    RL_Real rl_sar(argc, argv);
    while (!g_shutdown_requested) { sleep(1); }
    std::cout << LOGGER::INFO << "Exiting..." << std::endl;
#endif

    return 0;
}
