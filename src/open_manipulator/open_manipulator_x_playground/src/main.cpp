#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <moveit_msgs/action/move_group.hpp>
#include <moveit_msgs/msg/constraints.hpp>
#include <moveit_msgs/msg/workspace_parameters.hpp>
#include <moveit_msgs/msg/joint_constraint.hpp>
#include <geometry_msgs/msg/pose.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <geometry_msgs/msg/transform_stamped.hpp>
#include <shape_msgs/msg/solid_primitive.hpp>
#include <visualization_msgs/msg/marker.hpp>

#include <tf2/LinearMath/Transform.h>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2/LinearMath/Vector3.h>
#include <tf2_ros/transform_broadcaster.h>

#include <iostream>
#include <string>
#include <sstream>
#include <thread>
#include <atomic>
#include <chrono>

using MoveGroup = moveit_msgs::action::MoveGroup;

// =================================================================
// TỌA ĐỘ ĐIỂM THẢ CHAI CỐ ĐỊNH 
// =================================================================
const double DROP_X = 0.33;
const double DROP_Y = 0.154; // Đặt sang bên trái robot
const double DROP_Z = 0.148;

// =================================================================
// 1. CLASS DIEU KHIEN CANH TAY VÀ KẸP
// =================================================================
class SimpleMover : public rclcpp::Node {
public:
    SimpleMover() : Node("test_xyz_manual") {
        action_client_ = rclcpp_action::create_client<MoveGroup>(this, "move_action");
        
        RCLCPP_INFO(this->get_logger(), "Dang ket noi voi Action Server robot...");
        action_client_->wait_for_action_server();
        RCLCPP_INFO(this->get_logger(), "-> DA KET NOI! San sang nhan lenh.");
    }

    // --- Hàm di chuyển tọa độ XYZ (Giữ nguyên của ông) ---
    bool move_to_xyz(double x, double y, double z) {
        auto goal_msg = MoveGroup::Goal();
        goal_msg.request.group_name = "arm";
        goal_msg.request.num_planning_attempts = 10;
        goal_msg.request.allowed_planning_time = 5.0;
        goal_msg.request.max_velocity_scaling_factor = 0.6; // Tăng nhẹ tốc độ để gắp nhanh hơn

        goal_msg.request.workspace_parameters.header.frame_id = "world";
        goal_msg.request.workspace_parameters.min_corner.x = -1.0;
        goal_msg.request.workspace_parameters.min_corner.y = -1.0;
        goal_msg.request.workspace_parameters.min_corner.z = -1.0; 
        goal_msg.request.workspace_parameters.max_corner.x = 1.0;
        goal_msg.request.workspace_parameters.max_corner.y = 1.0;
        goal_msg.request.workspace_parameters.max_corner.z = 1.0;

        moveit_msgs::msg::Constraints constraints;
        moveit_msgs::msg::PositionConstraint pcm;
        pcm.header.frame_id = "world";
        pcm.link_name = "end_effector_link"; 
        pcm.weight = 1.0;

        shape_msgs::msg::SolidPrimitive box;
        box.type = shape_msgs::msg::SolidPrimitive::BOX;
        box.dimensions = {0.005, 0.005, 0.005};

        geometry_msgs::msg::Pose target_pose;
        target_pose.position.x = x;
        target_pose.position.y = y;
        target_pose.position.z = z;
        target_pose.orientation.w = 1.0;
        
        pcm.constraint_region.primitives.push_back(box);
        pcm.constraint_region.primitive_poses.push_back(target_pose);

        constraints.position_constraints.push_back(pcm);
        goal_msg.request.goal_constraints.push_back(constraints);

        return send_action_goal(goal_msg, "Di chuyen XYZ");
    }

    // --- MỚI: Hàm đóng/mở kẹp ---
    // --- MỚI: Hàm đóng/mở kẹp ---
    bool control_gripper(const std::string& command) {
        auto goal_msg = MoveGroup::Goal();
        goal_msg.request.group_name = "gripper";
        goal_msg.request.num_planning_attempts = 5;
        goal_msg.request.allowed_planning_time = 2.0;

        // =========================================================
        // 🚀 BÍ QUYẾT TINH CHỈNH ĐỘ MỞ CỦA NGÀM KẸP
        // Hành trình kẹp: 0.019 (Mở toạc) ----> -0.009 (Đóng khít)
        // =========================================================
        double target;
        if (command == "mo") {
            target = 0.019;  // Mở hết cỡ để đón chai
        } else {
            // SỬA SỐ NÀY ĐỂ KẸP LỎNG HAY CHẶT
            // Ví dụ: 0.005 là đóng hờ, 0.0 là đóng vừa, -0.005 là siết chặt
            target = 0.002;  // <--- Thử số này trước nhé!
        }
        // =========================================================

        moveit_msgs::msg::Constraints constraints;
        // ... (phần code ở dưới giữ nguyên) ...
        
        moveit_msgs::msg::JointConstraint jc_left;
        jc_left.joint_name = "gripper_left_joint";
        jc_left.position = target;
        jc_left.weight = 1.0;

        moveit_msgs::msg::JointConstraint jc_right;
        jc_right.joint_name = "gripper_right_joint";
        jc_right.position = target;
        jc_right.weight = 1.0;

        constraints.joint_constraints.push_back(jc_left);
        constraints.joint_constraints.push_back(jc_right);
        
        goal_msg.request.goal_constraints.push_back(constraints);

        return send_action_goal(goal_msg, command == "mo" ? "Mo kep" : "Dong kep");
    }
    // --- MỚI: Hàm thu tay về tư thế chờ an toàn (Chạy bằng Góc Khớp) ---
    bool move_to_home() {
        auto goal_msg = MoveGroup::Goal();
        goal_msg.request.group_name = "arm";
        goal_msg.request.num_planning_attempts = 5;
        goal_msg.request.allowed_planning_time = 3.0;
        goal_msg.request.max_velocity_scaling_factor = 0.5;

        moveit_msgs::msg::Constraints constraints;
        
        // 4 góc chuẩn của OpenManipulator-X để ở tư thế "Ngóc đầu chờ lệnh"
        std::vector<std::string> joint_names = {"joint1", "joint2", "joint3", "joint4"};
        std::vector<double> home_angles = {0.0, 0.0, 0.0, 0.0}; 

        for (size_t i = 0; i < joint_names.size(); ++i) {
            moveit_msgs::msg::JointConstraint jc;
            jc.joint_name = joint_names[i];
            jc.position = home_angles[i];
            jc.weight = 1.0;
            constraints.joint_constraints.push_back(jc);
        }

        goal_msg.request.goal_constraints.push_back(constraints);
        return send_action_goal(goal_msg, "Thu tay ve Home");
    }

private:
    // Tách phần gửi Action thành hàm riêng cho gọn code
    bool send_action_goal(const MoveGroup::Goal& goal_msg, const std::string& action_name) {
        auto send_goal_options = rclcpp_action::Client<MoveGroup>::SendGoalOptions();
        auto send_goal_future = action_client_->async_send_goal(goal_msg, send_goal_options);

        if (rclcpp::spin_until_future_complete(this->get_node_base_interface(), send_goal_future) != rclcpp::FutureReturnCode::SUCCESS) {
            RCLCPP_ERROR(this->get_logger(), "[%s] Loi ket noi Action Server!", action_name.c_str());
            return false;
        }

        auto goal_handle = send_goal_future.get();
        if (!goal_handle) {
            RCLCPP_ERROR(this->get_logger(), "[%s] LOI: MoveIt tu choi!", action_name.c_str());
            return false;
        }

        auto get_result_future = action_client_->async_get_result(goal_handle);
        if (rclcpp::spin_until_future_complete(this->get_node_base_interface(), get_result_future) != rclcpp::FutureReturnCode::SUCCESS) {
            return false;
        }

        auto result = get_result_future.get().result;
        return (result->error_code.val == 1);
    }

    rclcpp_action::Client<MoveGroup>::SharedPtr action_client_;
};


// =================================================================
// 2. CLASS LẮNG NGHE CAMERA & TÍNH TOÁN TỌA ĐỘ
// =================================================================
class CameraListener : public rclcpp::Node {
public:
    CameraListener(std::shared_ptr<SimpleMover> mover) 
    : Node("camera_listener"), mover_(mover), is_picking_up_(false) {
        
        pose_sub_ = this->create_subscription<geometry_msgs::msg::PoseStamped>(
            "/target_pose", 10,
            std::bind(&CameraListener::pose_callback, this, std::placeholders::_1)
        );

        marker_pub_ = this->create_publisher<visualization_msgs::msg::Marker>("bottle_visual_marker", 10);
        tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);
    }

private:
    void draw_bottle_marker(double x, double y, double z) {
        visualization_msgs::msg::Marker marker;
        marker.header.frame_id = "world";
        marker.header.stamp = this->get_clock()->now();
        marker.ns = "virtual_bottles";
        marker.id = 0;
        marker.type = visualization_msgs::msg::Marker::CYLINDER;
        marker.action = visualization_msgs::msg::Marker::ADD;
        marker.pose.position.x = x; marker.pose.position.y = y; marker.pose.position.z = z;
        marker.pose.orientation.w = 1.0;
        marker.scale.x = 0.08; marker.scale.y = 0.08; marker.scale.z = 0.20;
        marker.color.r = 0.0f; marker.color.g = 0.5f; marker.color.b = 1.0f; marker.color.a = 0.5f;
        marker_pub_->publish(marker);
    }

    void pose_callback(const geometry_msgs::msg::PoseStamped::SharedPtr msg) {
        // Nếu đang bận gắp chai thì bỏ qua frame camera này
        if (is_picking_up_ || !rclcpp::ok()) return;

        tf2::Vector3 point_opt(msg->pose.position.x, msg->pose.position.y, msg->pose.position.z);

        tf2::Transform opt_to_cam; opt_to_cam.setOrigin(tf2::Vector3(0, 0, 0));
        tf2::Quaternion q_opt; q_opt.setRPY(-1.5708, 0, -1.5708);
        opt_to_cam.setRotation(q_opt);

        tf2::Transform cam_to_world; cam_to_world.setOrigin(tf2::Vector3(-0.05, 0.0, 0.8));
        tf2::Quaternion q_cam; q_cam.setRPY(0, 1.1869, 0);
        cam_to_world.setRotation(q_cam);

        tf2::Vector3 target_world = cam_to_world * opt_to_cam * point_opt;
        
        double tx = target_world.x(); double ty = target_world.y(); double tz = target_world.z();

        geometry_msgs::msg::TransformStamped t;
        t.header.stamp = this->get_clock()->now(); t.header.frame_id = "world"; t.child_frame_id = "bottle_target";
        t.transform.translation.x = tx; t.transform.translation.y = ty; t.transform.translation.z = tz;
        t.transform.rotation.w = 1.0;
        tf_broadcaster_->sendTransform(t);

        draw_bottle_marker(tx, ty, tz);

        RCLCPP_INFO(this->get_logger(), "=> Phat hien chai nuoc: (%.2f, %.2f, %.2f). BAT DAU GAP!", tx, ty, tz);

        // Khóa luồng để robot yên tâm đi làm nhiệm vụ
        is_picking_up_ = true;

        // 🚀 KỊCH BẢN PICK AND PLACE HOÀN CHỈNH
        std::thread([this, tx, ty, tz]() {
            RCLCPP_INFO(this->get_logger(), "1. Mo kep");
            mover_->control_gripper("mo");

            // ==============================
            // 🚀 THÊM ĐOẠN NÀY (QUAN TRỌNG)
            // ==============================
            double dx = tx;
            double dy = ty;
            double norm = sqrt(dx*dx + dy*dy);
            if (norm < 1e-3) norm = 1.0;

            double retreat_x = tx - 0.08 * (dx / norm);
            double retreat_y = ty - 0.08 * (dy / norm);

            RCLCPP_INFO(this->get_logger(), "2. Lui ve vi tri an toan");
            mover_->move_to_xyz(retreat_x, retreat_y, tz + 0.03);

            // ==============================
            // Sau đó mới tiến vào
            // ==============================
            RCLCPP_INFO(this->get_logger(), "3. Tien vao phia tren chai (Pre-grasp)");
            mover_->move_to_xyz(tx, ty, tz + 0.05);

            RCLCPP_INFO(this->get_logger(), "4. Ha xuong gap (Grasp)");
            mover_->move_to_xyz(tx, ty, tz);

            RCLCPP_INFO(this->get_logger(), "5. Dong kep");
            mover_->control_gripper("dong");
            std::this_thread::sleep_for(std::chrono::seconds(1));

            RCLCPP_INFO(this->get_logger(), "6. Nhac chai len");
            mover_->move_to_xyz(tx, ty, tz + 0.05);

            RCLCPP_INFO(this->get_logger(), "7. Di chuyen sang diem bo");
            mover_->move_to_xyz(DROP_X, DROP_Y, DROP_Z + 0.05);

            RCLCPP_INFO(this->get_logger(), "8. Ha chai xuong");
            mover_->move_to_xyz(DROP_X, DROP_Y, DROP_Z);

            RCLCPP_INFO(this->get_logger(), "9. Mo kep nha chai");
            mover_->control_gripper("mo");
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // retreat sau khi thả (cái này bạn làm đúng rồi 👍)
            RCLCPP_INFO(this->get_logger(), "10. Lui tay ra khoi chai");
            mover_->move_to_xyz(DROP_X - 0.07, DROP_Y - 0.05, DROP_Z);

            RCLCPP_INFO(this->get_logger(), "11. Thu tay ve tu the cho (Home)");
            mover_->move_to_home();

            RCLCPP_INFO(this->get_logger(), "=== CHU TRINH XONG! Dang cho camera tim chai moi... ===");

            std::this_thread::sleep_for(std::chrono::seconds(2));
            is_picking_up_ = false;
        }).detach();
    }

    std::shared_ptr<SimpleMover> mover_;
    std::atomic<bool> is_picking_up_;
    rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr pose_sub_;
    std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;
    rclcpp::Publisher<visualization_msgs::msg::Marker>::SharedPtr marker_pub_;
};

// =================================================================
// MAIN
// =================================================================
int main(int argc, char** argv) {
    rclcpp::init(argc, argv);

    auto mover = std::make_shared<SimpleMover>();
    auto camera_listener = std::make_shared<CameraListener>(mover);

    std::cout << "\n=================================================\n";
    std::cout << "  HE THONG PICK & PLACE TU DONG DA KICH HOAT\n";
    std::cout << "  - Gắp: Tọa độ Camera\n";
    std::cout << "  - Bỏ: X=" << DROP_X << ", Y=" << DROP_Y << ", Z=" << DROP_Z << "\n";
    std::cout << "=================================================\n\n";

    rclcpp::spin(camera_listener);

    rclcpp::shutdown();
    return 0;
}