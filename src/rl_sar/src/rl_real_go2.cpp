/*
 * Copyright (c) 2024-2025 Ziqi Fan
 * SPDX-License-Identifier: Apache-2.0
 */

#include "rl_real_go2.hpp"

RL_Real::RL_Real(int argc, char **argv)
{
    bool wheel_mode = (argc > 2 && std::string(argv[2]) == "wheel");

#if defined(USE_ROS1) && defined(USE_ROS)
    ros::NodeHandle nh;
    this->cmd_vel_subscriber = nh.subscribe<geometry_msgs::Twist>("/cmd_vel", 10, &RL_Real::CmdvelCallback, this);
#elif defined(USE_ROS2) && defined(USE_ROS)
    ros2_node = std::make_shared<rclcpp::Node>("rl_real_node");
    this->cmd_vel_subscriber = ros2_node->create_subscription<geometry_msgs::msg::Twist>(
        "/cmd_vel", rclcpp::SystemDefaultsQoS(),
        [this] (const geometry_msgs::msg::Twist::SharedPtr msg) {this->CmdvelCallback(msg);}
    );
#endif

    // read params from yaml
    this->ang_vel_axis = "body";
    this->robot_name = wheel_mode ? "go2w" : "go2";
    this->ReadYaml(this->robot_name, "base.yaml");

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

    // Setup Xbox joystick (alternative to unitree wireless controller)
    this->SetupSysJoystick("/dev/input/js0", 16); // 16 bits joystick

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
    // Get Xbox joystick input
    this->GetSysJoystick();
    
    // Process Xbox gamepad buttons (same as MuJoCo simulation)
    if (this->sys_js_button[0].on_press) this->control.SetGamepad(Input::Gamepad::A);
    if (this->sys_js_button[1].on_press) this->control.SetGamepad(Input::Gamepad::B);
    if (this->sys_js_button[2].on_press) this->control.SetGamepad(Input::Gamepad::X);
    if (this->sys_js_button[3].on_press) this->control.SetGamepad(Input::Gamepad::Y);
    if (this->sys_js_button[4].on_press) this->control.SetGamepad(Input::Gamepad::LB);
    if (this->sys_js_button[5].on_press) this->control.SetGamepad(Input::Gamepad::RB);
    if (this->sys_js_button[9].on_press) this->control.SetGamepad(Input::Gamepad::LStick);
    if (this->sys_js_button[10].on_press) this->control.SetGamepad(Input::Gamepad::RStick);
    if (this->sys_js_axis[7] < 0) this->control.SetGamepad(Input::Gamepad::DPadUp);
    if (this->sys_js_axis[7] > 0) this->control.SetGamepad(Input::Gamepad::DPadDown);
    if (this->sys_js_axis[6] > 0) this->control.SetGamepad(Input::Gamepad::DPadLeft);
    if (this->sys_js_axis[6] < 0) this->control.SetGamepad(Input::Gamepad::DPadRight);
    
    // Combination keys
    if (this->sys_js_button[4].pressed && this->sys_js_button[0].on_press) this->control.SetGamepad(Input::Gamepad::LB_A);
    if (this->sys_js_button[4].pressed && this->sys_js_button[1].on_press) this->control.SetGamepad(Input::Gamepad::LB_B);
    if (this->sys_js_button[4].pressed && this->sys_js_button[2].on_press) this->control.SetGamepad(Input::Gamepad::LB_X);
    if (this->sys_js_button[4].pressed && this->sys_js_button[3].on_press) this->control.SetGamepad(Input::Gamepad::LB_Y);
    if (this->sys_js_button[4].pressed && this->sys_js_button[9].on_press) this->control.SetGamepad(Input::Gamepad::LB_LStick);
    if (this->sys_js_button[4].pressed && this->sys_js_button[10].on_press) this->control.SetGamepad(Input::Gamepad::LB_RStick);
    if (this->sys_js_button[4].pressed && this->sys_js_axis[7] < 0) this->control.SetGamepad(Input::Gamepad::LB_DPadUp);
    if (this->sys_js_button[4].pressed && this->sys_js_axis[7] > 0) this->control.SetGamepad(Input::Gamepad::LB_DPadDown);
    if (this->sys_js_button[4].pressed && this->sys_js_axis[6] > 0) this->control.SetGamepad(Input::Gamepad::LB_DPadRight);
    if (this->sys_js_button[4].pressed && this->sys_js_axis[6] < 0) this->control.SetGamepad(Input::Gamepad::LB_DPadLeft);
    if (this->sys_js_button[5].pressed && this->sys_js_button[0].on_press) this->control.SetGamepad(Input::Gamepad::RB_A);
    if (this->sys_js_button[5].pressed && this->sys_js_button[1].on_press) this->control.SetGamepad(Input::Gamepad::RB_B);
    if (this->sys_js_button[5].pressed && this->sys_js_button[2].on_press) this->control.SetGamepad(Input::Gamepad::RB_X);
    if (this->sys_js_button[5].pressed && this->sys_js_button[3].on_press) this->control.SetGamepad(Input::Gamepad::RB_Y);
    if (this->sys_js_button[5].pressed && this->sys_js_button[9].on_press) this->control.SetGamepad(Input::Gamepad::RB_LStick);
    if (this->sys_js_button[5].pressed && this->sys_js_button[10].on_press) this->control.SetGamepad(Input::Gamepad::RB_RStick);
    if (this->sys_js_button[5].pressed && this->sys_js_axis[7] < 0) this->control.SetGamepad(Input::Gamepad::RB_DPadUp);
    if (this->sys_js_button[5].pressed && this->sys_js_axis[7] > 0) this->control.SetGamepad(Input::Gamepad::RB_DPadDown);
    if (this->sys_js_button[5].pressed && this->sys_js_axis[6] > 0) this->control.SetGamepad(Input::Gamepad::RB_DPadRight);
    if (this->sys_js_button[5].pressed && this->sys_js_axis[6] < 0) this->control.SetGamepad(Input::Gamepad::RB_DPadLeft);
    if (this->sys_js_button[4].pressed && this->sys_js_button[5].on_press) this->control.SetGamepad(Input::Gamepad::LB_RB);

    // Left stick: movement (x, y velocity)
    float ly = -float(this->sys_js_axis[1]) / float(this->sys_js_max_value);
    float lx = -float(this->sys_js_axis[0]) / float(this->sys_js_max_value);
    
    // Right stick: pose control
    float rs_x = float(this->sys_js_axis[3]) / float(this->sys_js_max_value);  // right stick horizontal -> roll
    float rs_y = -float(this->sys_js_axis[4]) / float(this->sys_js_max_value);  // right stick vertical -> pitch
    
    // Triggers: yaw angular velocity control (axis[2]=LT, axis[5]=RT)
    float lt_raw = -float(this->sys_js_axis[2]) / float(this->sys_js_max_value);
    float rt_raw = -float(this->sys_js_axis[5]) / float(this->sys_js_max_value);
    
    // Normalize triggers from [-1, 1] to [0, 1]
    float lt = (lt_raw < 0) ? 0.0f : lt_raw;
    float rt = (rt_raw < 0) ? 0.0f : rt_raw;
    
    // Apply deadzone to all axes
    if (std::abs(lx) < 0.1f) lx = 0.0f;
    if (std::abs(ly) < 0.1f) ly = 0.0f;
    if (std::abs(rs_x) < 0.1f) rs_x = 0.0f;
    if (std::abs(rs_y) < 0.1f) rs_y = 0.0f;
    if (std::abs(lt) < 0.05f) lt = 0.0f;
    if (std::abs(rt) < 0.05f) rt = 0.0f;
    
    float trigger_yaw = rt - lt;  // RT for positive yaw (right turn), LT for negative yaw (left turn)

    bool has_velocity_input = (std::abs(ly) > 0.01f || std::abs(lx) > 0.01f);
    bool has_yaw_input = (std::abs(trigger_yaw) > 0.01f);

    // Update velocity commands
    this->control.x = ly;
    this->control.y = lx;
    
    // Update yaw: only non-zero when triggers are pressed
    if (has_yaw_input)
    {
        this->control.yaw = trigger_yaw;
    }
    else
    {
        this->control.yaw = 0.0f;
    }
    
    // Track if gamepad is active for any velocity input
    this->sys_js_active = (has_velocity_input || has_yaw_input);
    
    // Pose control parameters (same as simulation)
    float height_increment = 0.005f; // 1cm per press (reduced from 2cm)
    float height_baseline = 0.33f;    // Baseline: 33cm (not used in runtime but for reference)
    float height_min = 0.18f;         // Absolute min: 18cm
    float height_max = 0.43f;         // Absolute max: 43cm
    
    float roll_scale = 0.785f;        // Map stick [-1, 1] to ±45°
    float roll_min = -0.785f;
    float roll_max = 0.785f;
    
    float pitch_scale = 0.436f;       // Map stick [-1, 1] to ±25°
    float pitch_min = -0.436f;
    float pitch_max = 0.436f;
    
    // Right stick controls roll and pitch directly (proportional control)
    // rs_x (horizontal left/right) -> roll
    // rs_y (vertical up/down) -> pitch
    // When stick is in deadzone, reset to neutral pose
    if (std::abs(rs_x) > 0.01f || std::abs(rs_y) > 0.01f)
    {
        // rs_x (horizontal left/right) -> roll
        this->control.roll = rs_x * roll_scale;
        if (this->control.roll < roll_min) this->control.roll = roll_min;
        if (this->control.roll > roll_max) this->control.roll = roll_max;
        
        // rs_y (vertical up/down) -> pitch
        this->control.pitch = rs_y * pitch_scale;
        if (this->control.pitch < pitch_min) this->control.pitch = pitch_min;
        if (this->control.pitch > pitch_max) this->control.pitch = pitch_max;
    }
    else
    {
        // Reset roll and pitch to neutral when stick is centered
        this->control.roll = 0.0f;
        this->control.pitch = 0.0f;
    }
    
    // Shoulder buttons for height control (only when not pressed with other buttons for combos)
    // RB alone (button[5]): increase height
    if (this->sys_js_button[5].on_press && 
        !this->sys_js_button[0].pressed && !this->sys_js_button[1].pressed && 
        !this->sys_js_button[2].pressed && !this->sys_js_button[3].pressed)
    {
        this->control.height += height_increment;
        if (this->control.height > height_max) this->control.height = height_max;
        // std::cout << LOGGER::INFO << "Height increased to: " << this->control.height << "m" << std::endl;
    }
    
    // LB alone (button[4]): decrease height
    if (this->sys_js_button[4].on_press && 
        !this->sys_js_button[0].pressed && !this->sys_js_button[1].pressed && 
        !this->sys_js_button[2].pressed && !this->sys_js_button[3].pressed &&
        !this->sys_js_button[5].pressed)
    {
        this->control.height -= height_increment;
        if (this->control.height < height_min) this->control.height = height_min;
        // std::cout << LOGGER::INFO << "Height decreased to: " << this->control.height << "m" << std::endl;
    }
    
    // X button (button[2]): reset to default pose (height=0.33m, roll=0, pitch=0)
    if (this->sys_js_button[2].on_press && 
        !this->sys_js_button[4].pressed && !this->sys_js_button[5].pressed)
    {
        this->control.height = 0.33f;
        this->control.roll = 0.0f;
        this->control.pitch = 0.0f;
        std::cout << LOGGER::INFO << "Pose reset to default: height=0.33m, roll=0°, pitch=0°" << std::endl;
    }

    // ==================== Unitree Wireless Controller Processing ====================
    // Process unitree wireless controller (same button mapping as Xbox)
    // If unitree controller is active, it will override Xbox inputs
    
    // Single button presses
    if (this->unitree_btn_A.on_press) this->control.SetGamepad(Input::Gamepad::A);
    if (this->unitree_btn_B.on_press) this->control.SetGamepad(Input::Gamepad::B);
    if (this->unitree_btn_X.on_press) this->control.SetGamepad(Input::Gamepad::X);
    if (this->unitree_btn_Y.on_press) this->control.SetGamepad(Input::Gamepad::Y);
    if (this->unitree_btn_L1.on_press) this->control.SetGamepad(Input::Gamepad::LB);
    if (this->unitree_btn_R1.on_press) this->control.SetGamepad(Input::Gamepad::RB);
    if (this->unitree_btn_F1.on_press) this->control.SetGamepad(Input::Gamepad::LStick);
    if (this->unitree_btn_F2.on_press) this->control.SetGamepad(Input::Gamepad::RStick);
    if (this->unitree_btn_up.on_press) this->control.SetGamepad(Input::Gamepad::DPadUp);
    if (this->unitree_btn_down.on_press) this->control.SetGamepad(Input::Gamepad::DPadDown);
    if (this->unitree_btn_left.on_press) this->control.SetGamepad(Input::Gamepad::DPadLeft);
    if (this->unitree_btn_right.on_press) this->control.SetGamepad(Input::Gamepad::DPadRight);
    
    // Combination keys (L1 + other button)
    if (this->unitree_btn_L1.pressed && this->unitree_btn_A.on_press) this->control.SetGamepad(Input::Gamepad::LB_A);
    if (this->unitree_btn_L1.pressed && this->unitree_btn_B.on_press) this->control.SetGamepad(Input::Gamepad::LB_B);
    if (this->unitree_btn_L1.pressed && this->unitree_btn_X.on_press) this->control.SetGamepad(Input::Gamepad::LB_X);
    if (this->unitree_btn_L1.pressed && this->unitree_btn_Y.on_press) this->control.SetGamepad(Input::Gamepad::LB_Y);
    if (this->unitree_btn_L1.pressed && this->unitree_btn_F1.on_press) this->control.SetGamepad(Input::Gamepad::LB_LStick);
    if (this->unitree_btn_L1.pressed && this->unitree_btn_F2.on_press) this->control.SetGamepad(Input::Gamepad::LB_RStick);
    if (this->unitree_btn_L1.pressed && this->unitree_btn_up.on_press) this->control.SetGamepad(Input::Gamepad::LB_DPadUp);
    if (this->unitree_btn_L1.pressed && this->unitree_btn_down.on_press) this->control.SetGamepad(Input::Gamepad::LB_DPadDown);
    if (this->unitree_btn_L1.pressed && this->unitree_btn_right.on_press) this->control.SetGamepad(Input::Gamepad::LB_DPadRight);
    if (this->unitree_btn_L1.pressed && this->unitree_btn_left.on_press) this->control.SetGamepad(Input::Gamepad::LB_DPadLeft);
    
    // Combination keys (R1 + other button)
    if (this->unitree_btn_R1.pressed && this->unitree_btn_A.on_press) this->control.SetGamepad(Input::Gamepad::RB_A);
    if (this->unitree_btn_R1.pressed && this->unitree_btn_B.on_press) this->control.SetGamepad(Input::Gamepad::RB_B);
    if (this->unitree_btn_R1.pressed && this->unitree_btn_X.on_press) this->control.SetGamepad(Input::Gamepad::RB_X);
    if (this->unitree_btn_R1.pressed && this->unitree_btn_Y.on_press) this->control.SetGamepad(Input::Gamepad::RB_Y);
    if (this->unitree_btn_R1.pressed && this->unitree_btn_F1.on_press) this->control.SetGamepad(Input::Gamepad::RB_LStick);
    if (this->unitree_btn_R1.pressed && this->unitree_btn_F2.on_press) this->control.SetGamepad(Input::Gamepad::RB_RStick);
    if (this->unitree_btn_R1.pressed && this->unitree_btn_up.on_press) this->control.SetGamepad(Input::Gamepad::RB_DPadUp);
    if (this->unitree_btn_R1.pressed && this->unitree_btn_down.on_press) this->control.SetGamepad(Input::Gamepad::RB_DPadDown);
    if (this->unitree_btn_R1.pressed && this->unitree_btn_right.on_press) this->control.SetGamepad(Input::Gamepad::RB_DPadRight);
    if (this->unitree_btn_R1.pressed && this->unitree_btn_left.on_press) this->control.SetGamepad(Input::Gamepad::RB_DPadLeft);
    if (this->unitree_btn_L1.pressed && this->unitree_btn_R1.on_press) this->control.SetGamepad(Input::Gamepad::LB_RB);

    // Get stick values from Unitree wireless controller
    // 修正方向：ly正值=前进，lx正值=右移，yaw正值=右转
    float unitree_ly = this->joystick.ly();   // Forward/backward (正值=前进)
    float unitree_lx = -this->joystick.lx();  // Left/right (正值=右移，需要取反)
    float unitree_rs_x = this->joystick.rx(); // Right stick horizontal -> roll
    float unitree_rs_y = -this->joystick.ry(); // Right stick vertical -> pitch (invert)
    
    // Apply deadzone to Unitree sticks
    if (std::abs(unitree_lx) < 0.1f) unitree_lx = 0.0f;
    if (std::abs(unitree_ly) < 0.1f) unitree_ly = 0.0f;
    if (std::abs(unitree_rs_x) < 0.1f) unitree_rs_x = 0.0f;
    if (std::abs(unitree_rs_y) < 0.1f) unitree_rs_y = 0.0f;
    
    bool unitree_has_velocity_input = (std::abs(unitree_ly) > 0.01f || std::abs(unitree_lx) > 0.01f);
    bool unitree_has_pose_input = (std::abs(unitree_rs_x) > 0.01f || std::abs(unitree_rs_y) > 0.01f);
    
    // L2/R2 button-based yaw control (按住持续施加yaw角速度，松开则yaw=0)
    // 宝树遥控器的L2/R2只是按键，不是模拟量
    float unitree_yaw_command = 0.0f;
    float yaw_speed = 1.0f;  // yaw angular velocity when button is pressed (rad/s)
    
    if (this->unitree_btn_R2.pressed)
    {
        unitree_yaw_command = -yaw_speed;  // R2: 右转（负yaw）
    }
    else if (this->unitree_btn_L2.pressed)
    {
        unitree_yaw_command = yaw_speed;   // L2: 左转（正yaw）
    }
    
    bool unitree_has_yaw_input = (std::abs(unitree_yaw_command) > 0.01f);
    
    // Check for height control buttons
    bool unitree_has_height_input = (this->unitree_btn_R1.pressed || this->unitree_btn_L1.pressed);
    
    // Unitree controller is active if ANY control is being used
    this->unitree_joy_active = (unitree_has_velocity_input || unitree_has_yaw_input || 
                                unitree_has_pose_input || unitree_has_height_input);
    
    // Process Unitree controller inputs (runs even without movement, for pose/height control)
    // Update velocity commands from Unitree controller
    this->control.x = unitree_ly;
    this->control.y = unitree_lx;
    this->control.yaw = unitree_yaw_command;
    
    // Right stick controls roll and pitch (always processed when using Unitree controller)
    // Reuse roll/pitch scale and limits defined earlier (lines 205-211)
    if (unitree_has_pose_input)
    {
        this->control.roll = unitree_rs_x * roll_scale;
        if (this->control.roll < roll_min) this->control.roll = roll_min;
        if (this->control.roll > roll_max) this->control.roll = roll_max;
        
        this->control.pitch = -unitree_rs_y * pitch_scale;  // Inverted for correct direction
        if (this->control.pitch < pitch_min) this->control.pitch = pitch_min;
        if (this->control.pitch > pitch_max) this->control.pitch = pitch_max;
    }
    else if (!this->sys_js_active)  // Only reset if Xbox is also inactive
    {
        this->control.roll = 0.0f;
        this->control.pitch = 0.0f;
    }
    
    // Shoulder buttons for height control (always processed)
    // Reuse height variables defined earlier (lines 200-203)
    
    // R1 alone: increase height
    if (this->unitree_btn_R1.on_press && 
        !this->unitree_btn_A.pressed && !this->unitree_btn_B.pressed && 
        !this->unitree_btn_X.pressed && !this->unitree_btn_Y.pressed)
    {
        this->control.height += height_increment;
        if (this->control.height > height_max) this->control.height = height_max;
        std::cout << LOGGER::INFO << "[Unitree] Height: " << this->control.height << "m" << std::endl;
    }
    
    // L1 alone: decrease height
    if (this->unitree_btn_L1.on_press && 
        !this->unitree_btn_A.pressed && !this->unitree_btn_B.pressed && 
        !this->unitree_btn_X.pressed && !this->unitree_btn_Y.pressed &&
        !this->unitree_btn_R1.pressed)
    {
        this->control.height -= height_increment;
        if (this->control.height < height_min) this->control.height = height_min;
        std::cout << LOGGER::INFO << "[Unitree] Height: " << this->control.height << "m" << std::endl;
    }
    
    // X button: reset to default pose
    if (this->unitree_btn_X.on_press && 
        !this->unitree_btn_L1.pressed && !this->unitree_btn_R1.pressed)
    {
        this->control.height = 0.33f;
        this->control.roll = 0.0f;
        this->control.pitch = 0.0f;
        std::cout << LOGGER::INFO << "[Unitree] Pose reset to default: height=0.33m, roll=0°, pitch=0°" << std::endl;
    }

    state->imu.quaternion[0] = this->unitree_low_state.imu_state().quaternion()[0]; // w
    state->imu.quaternion[1] = this->unitree_low_state.imu_state().quaternion()[1]; // x
    state->imu.quaternion[2] = this->unitree_low_state.imu_state().quaternion()[2]; // y
    state->imu.quaternion[3] = this->unitree_low_state.imu_state().quaternion()[3]; // z

    for (int i = 0; i < 3; ++i)
    {
        state->imu.gyroscope[i] = this->unitree_low_state.imu_state().gyroscope()[i];
    }
    for (int i = 0; i < this->params.Get<int>("num_of_dofs"); ++i)
    {
        state->motor_state.q[i] = this->unitree_low_state.motor_state()[this->params.Get<std::vector<int>>("joint_mapping")[i]].q();
        state->motor_state.dq[i] = this->unitree_low_state.motor_state()[this->params.Get<std::vector<int>>("joint_mapping")[i]].dq();
        state->motor_state.tau_est[i] = this->unitree_low_state.motor_state()[this->params.Get<std::vector<int>>("joint_mapping")[i]].tau_est();
    }
}

void RL_Real::SetCommand(const RobotCommand<float> *command)
{
    for (int i = 0; i < this->params.Get<int>("num_of_dofs"); ++i)
    {
        this->unitree_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].mode() = 0x01;
        this->unitree_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].q() = command->motor_command.q[i];
        this->unitree_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].dq() = command->motor_command.dq[i];
        this->unitree_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].kp() = command->motor_command.kp[i];
        this->unitree_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].kd() = command->motor_command.kd[i];
        this->unitree_low_command.motor_cmd()[this->params.Get<std::vector<int>>("joint_mapping")[i]].tau() = command->motor_command.tau[i];
    }

    this->unitree_low_command.crc() = Crc32Core((uint32_t *)&unitree_low_command, (sizeof(unitree_go::msg::dds_::LowCmd_) >> 2) - 1);
    lowcmd_publisher->Write(unitree_low_command);
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
        this->episode_length_buf += 1;
        this->obs.ang_vel = this->robot_state.imu.gyroscope;
        
        // Support both 3D velocity commands and 7D velocity_pose commands based on num_commands config
        int num_commands = this->params.Get<int>("num_commands", 3);
        if (num_commands == 7)
        {
            // 7D velocity_pose commands: [vx, vy, vyaw, height, roll, pitch, yaw]
            // Last yaw is always 0 for dimension consistency with Isaac Lab training
            this->obs.commands = {this->control.x, this->control.y, this->control.yaw, 
                                  this->control.height, this->control.roll, this->control.pitch, 0.0f};
        }
        else
        {
            // 3D velocity commands: [vx, vy, vyaw]
            this->obs.commands = {this->control.x, this->control.y, this->control.yaw};
        }
        
#if !defined(USE_CMAKE) && defined(USE_ROS)
        if (this->control.navigation_mode)
        {
            if (num_commands == 7)
            {
                // In navigation mode with velocity_pose, use ROS velocity commands but keep pose at neutral
                this->obs.commands = {(float)this->cmd_vel.linear.x, (float)this->cmd_vel.linear.y, (float)this->cmd_vel.angular.z,
                                      this->control.height, this->control.roll, this->control.pitch, 0.0f};
            }
            else
            {
                this->obs.commands = {(float)this->cmd_vel.linear.x, (float)this->cmd_vel.linear.y, (float)this->cmd_vel.angular.z};
            }
        }
#endif
        this->obs.base_quat = this->robot_state.imu.quaternion;
        this->obs.dof_pos = this->robot_state.motor_state.q;
        this->obs.dof_vel = this->robot_state.motor_state.dq;

        this->obs.actions = this->Forward();
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

void RL_Real::SetupSysJoystick(const std::string& device, int bits)
{
    this->sys_js = std::make_unique<Joystick>(device);
    if (!this->sys_js->isFound())
    {
        std::cout << LOGGER::WARNING << "Xbox joystick [" << device << "] not found. Will use keyboard control only." << std::endl;
    }
    else
    {
        std::cout << LOGGER::INFO << "Xbox joystick [" << device << "] initialized successfully." << std::endl;
    }

    this->sys_js_max_value = (1 << (bits - 1));
}

void RL_Real::GetSysJoystick()
{
    // Clear all button event states
    for (int i = 0; i < 20; ++i)
    {
        this->sys_js_button[i].on_press = false;
        this->sys_js_button[i].on_release = false;
    }

    // Check if joystick is valid before using
    if (!this->sys_js || !this->sys_js->isFound())
    {
        return;
    }

    while (this->sys_js->sample(&this->sys_js_event))
    {
        if (this->sys_js_event.isButton())
        {
            this->sys_js_button[this->sys_js_event.number].update(this->sys_js_event.value);
        }
        else if (this->sys_js_event.isAxis())
        {
            double normalized = double(this->sys_js_event.value) / this->sys_js_max_value;
            if (std::abs(normalized) < this->axis_deadzone)
            {
                this->sys_js_axis[this->sys_js_event.number] = 0;
            }
            else
            {
                this->sys_js_axis[this->sys_js_event.number] = this->sys_js_event.value;
            }
        }
    }
}

void RL_Real::JoystickHandler(const void *message)
{
    joystick = *(unitree_go::msg::dds_::WirelessController_ *)message;
    this->unitree_joy.value = joystick.keys();
    
    // Update button states for Unitree wireless controller
    this->unitree_btn_R1.update(this->unitree_joy.components.R1);
    this->unitree_btn_L1.update(this->unitree_joy.components.L1);
    this->unitree_btn_start.update(this->unitree_joy.components.start);
    this->unitree_btn_select.update(this->unitree_joy.components.select);
    this->unitree_btn_R2.update(this->unitree_joy.components.R2);
    this->unitree_btn_L2.update(this->unitree_joy.components.L2);
    this->unitree_btn_F1.update(this->unitree_joy.components.F1);
    this->unitree_btn_F2.update(this->unitree_joy.components.F2);
    this->unitree_btn_A.update(this->unitree_joy.components.A);
    this->unitree_btn_B.update(this->unitree_joy.components.B);
    this->unitree_btn_X.update(this->unitree_joy.components.X);
    this->unitree_btn_Y.update(this->unitree_joy.components.Y);
    this->unitree_btn_up.update(this->unitree_joy.components.up);
    this->unitree_btn_down.update(this->unitree_joy.components.down);
    this->unitree_btn_left.update(this->unitree_joy.components.left);
    this->unitree_btn_right.update(this->unitree_joy.components.right);
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
#endif

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
        std::cout << LOGGER::ERROR << "Usage: " << argv[0] << " networkInterface [wheel]" << std::endl;
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
