
#include "CCO_VOXEL/utils.h"

bool trajectoryUpdated = false;

nav_msgs::Path trajectory; // this receives the trajectory every time subscriber is called

int i = 0; // index in the trajectory

int count = 0; // count to see if the controller starts just now or not.

Eigen::Vector3d goal, currPose;

geometry_msgs::PoseStamped previous_pose;

float delay; // time between publishing two setpoints

/** trajectory callback **/
void path_cb(nav_msgs::Path traj)
{
    trajectory = traj;
    trajectoryUpdated = true;
}

/** drone pose callback **/
void local_pose_cb(const geometry_msgs::PoseStamped pose)
{
    currPose(0) = pose.pose.position.x;
    currPose(1) = pose.pose.position.y;
    currPose(2) = pose.pose.position.z;
}

/** MAVROS OFFBOARD Control **/
void control(ros::Publisher pub, ros::Rate rate)
{

    if (trajectoryUpdated)
    {
        trajectoryUpdated = false;
        std::cout << "Size of trajectory is: " << trajectory.poses.size() << std::endl;
        nav_msgs::Path traj_local = trajectory;

        while (i < traj_local.poses.size())
        {
            if (!ros::ok())
            {
                break;
            }
            Eigen::Vector3d wp_eigen;

            wp_eigen(0) = traj_local.poses.at(i).pose.position.x;
            wp_eigen(1) = traj_local.poses.at(i).pose.position.y;
            wp_eigen(2) = traj_local.poses.at(i).pose.position.z;

            previous_pose.pose.position.x = wp_eigen(0);
            previous_pose.pose.position.y = wp_eigen(1);
            previous_pose.pose.position.z = wp_eigen(2);

            std::cout << "Published point " << i << std::endl;
            pub.publish(traj_local.poses.at(i));
            ros::spinOnce();
            ros::Duration(delay).sleep();
            i++;
        }
    }

    else
    {
        std::cout << "No trajectory available" << std::endl;

        pub.publish(previous_pose);
    }
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "drone_control");
    ros::NodeHandle n;

    ros::Subscriber path = n.subscribe<nav_msgs::Path>("/fastPlanner_path", 1, path_cb);
    // ros::Subscriber path = n.subscribe<nav_msgs::Path>("/MMD_trajectory",1, path_cb);
    ros::Subscriber loc = n.subscribe<geometry_msgs::PoseStamped>("/mavros/local_position/pose", 10, local_pose_cb);

    ros::Publisher wp_pub = n.advertise<geometry_msgs::PoseStamped>("/mavros/setpoint_position/local", 10);

    /* Services */
    ros::ServiceClient arming_client = n.serviceClient<mavros_msgs::CommandBool>("mavros/cmd/arming");
    ros::ServiceClient landing_client = n.serviceClient<mavros_msgs::CommandTOL>("mavros/cmd/landing");
    ros::ServiceClient set_mode_client = n.serviceClient<mavros_msgs::SetMode>("mavros/set_mode");

    ros::Rate rate(20);

    /** get delay from user **/
    // std::cout<<"Enter delay between waypoints (in seconds) ";
    // std::cin>>delay;

    delay = 0.02;

    while (!trajectoryUpdated || !ros::ok())
    {
        std::cout << "Waiting for trajectory" << std::endl;
        ros::spinOnce();
        rate.sleep();

        if (!ros::ok())
        {
            break;
        }
    }

    if (trajectoryUpdated)
    {
        geometry_msgs::PoseStamped initPose;

        if (count == 0)
        {
            std::cout << "Controller started ... " << std::endl;

            initPose.pose.position.x = 0;
            initPose.pose.position.y = 0;
            initPose.pose.position.z = 0;

            for (int i = 0; i < 10 && ros::ok(); i++)
            {
                wp_pub.publish(initPose);
                ros::spinOnce();
                rate.sleep();
            }

            mavros_msgs::SetMode offb_set_mode;
            offb_set_mode.request.custom_mode = "OFFBOARD";

            mavros_msgs::CommandBool arm_cmd;
            arm_cmd.request.value = true;

            /** set mode to offboard **/
            if (set_mode_client.call(offb_set_mode) &&
                offb_set_mode.response.mode_sent)
            {
                ROS_INFO("Offboard enabled");
            }
        }

        count++;

        while (ros::ok())
        {
            control(wp_pub, rate);

            rate.sleep();
            ros::spinOnce();

            if (!ros::ok())
                break;
        }

        count++;
    }

    ros::spinOnce();
    return 0;
}