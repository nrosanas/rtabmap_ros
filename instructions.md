# Instructions to run rtabmap_ros with apriltag detection with D435i and T265
The tags can be found in the folder /printable_tags

Under launch/test/tags.yaml the real size of the tags can be specified in meters.


ros packages needed
- realsense2_camera
- rtabmap_ros
- apriltag_ros


Terminal 1:

roslaunch realsense2_camera rs_d400_and_t265.launch enable_accel:=true

Terminal 2:

roslaunch rtabmap_ros test_apriltag_ros.launch rgb_topic:=/d400/color/image_raw camera_info_topic:=/d400/color/camera_info camera_frame_id:=d400_color_optical_frame

Terminal 3:

roslaunch rtabmap_ros rtabmap.launch    args:="-d --Mem/UseOdomGravity true --Optimizer/GravitySigma 0.3"    odom_topic:=/t265/odom/sample    frame_id:=t265_link    rgbd_sync:=true    depth_topic:=/d400/aligned_depth_to_color/image_raw    rgb_topic:=/d400/color/image_raw    camera_info_topic:=/d400/color/camera_info    approx_rgbd_sync:=false    visual_odometry:=false rviz:=true rtabmapviz:=true
