from launch import LaunchDescription
from launch_ros.actions import Node
from launch.actions import IncludeLaunchDescription
from launch.launch_description_sources import PythonLaunchDescriptionSource
from ament_index_python.packages import get_package_share_directory
import os

def generate_launch_description():

    moveit_launch = IncludeLaunchDescription(
        PythonLaunchDescriptionSource(
            os.path.join(
                get_package_share_directory('open_manipulator_x_moveit_config'),
                'launch',
                'move_group.launch.py'
            )
        )
    )

    main_node = Node(
        package='open_manipulator_x_playground',
        executable='main',
        output='screen',
    )

    return LaunchDescription([
        moveit_launch,
        main_node
    ])