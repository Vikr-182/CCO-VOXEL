#ifndef _KINODYNAMIC_ASTAR_H
#define _KINODYNAMIC_ASTAR_H
#include "CCO_VOXEL/utils.h"

#include <Eigen/Eigen>
#include <iostream>
#include <map>
#include <ros/console.h>
#include <ros/ros.h>
#include <string>
#include <unordered_map>
#include <boost/functional/hash.hpp>
#include <queue>
#include <memory>
#include <vector>
/* Add Octomap EDT Library */
#include <dynamicEDT3D/dynamicEDTOctomap.h>

namespace fast_planner
{
// #define REACH_HORIZON 1
// #define REACH_END 2
// #define NO_PATH 3
#define IN_CLOSE_SET 'a'
#define IN_OPEN_SET 'b'
#define NOT_EXPAND 'c'
#define inf 1 >> 30

  class PathNode
  {
  public:
    /* -------------------- */
    Eigen::Vector3i index;
    Eigen::Matrix<double, 6, 1> state;
    double g_score, f_score;
    Eigen::Vector3d input;
    double duration;
    double time; // dyn
    int time_idx;
    PathNode *parent;
    char node_state;

    /* -------------------- */
    PathNode()
    {
      parent = NULL;
      node_state = NOT_EXPAND;
    }
    ~PathNode(){};
  };

  typedef PathNode *PathNodePtr; // pointer to path nodes

  class NodeComparator
  {
  public:
    bool operator()(PathNodePtr node1, PathNodePtr node2)
    {
      return node1->f_score > node2->f_score;
    }
  };

  template <typename T>
  struct matrix_hash : std::unary_function<T, size_t>
  {
    std::size_t operator()(T const &matrix) const
    {
      size_t seed = 0;
      for (size_t i = 0; i < matrix.size(); ++i)
      {
        auto elem = *(matrix.data() + i);
        seed ^= std::hash<typename T::Scalar>()(elem) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
      }
      return seed;
    }
  };

  class NodeHashTable
  {
  private:
    /* data */
    std::unordered_map<Eigen::Vector3i, PathNodePtr, matrix_hash<Eigen::Vector3i>> data_3d_;
    std::unordered_map<Eigen::Vector4i, PathNodePtr, matrix_hash<Eigen::Vector4i>> data_4d_;

  public:
    NodeHashTable(/* args */)
    {
    }
    ~NodeHashTable()
    {
    }
    void insert(Eigen::Vector3i idx, PathNodePtr node)
    {
      data_3d_.insert(std::make_pair(idx, node));
    }
    void insert(Eigen::Vector3i idx, int time_idx, PathNodePtr node)
    {
      data_4d_.insert(std::make_pair(Eigen::Vector4i(idx(0), idx(1), idx(2), time_idx), node));
    }

    PathNodePtr find(Eigen::Vector3i idx)
    {
      auto iter = data_3d_.find(idx);
      return iter == data_3d_.end() ? NULL : iter->second;
    }
    PathNodePtr find(Eigen::Vector3i idx, int time_idx)
    {
      auto iter = data_4d_.find(Eigen::Vector4i(idx(0), idx(1), idx(2), time_idx));
      return iter == data_4d_.end() ? NULL : iter->second;
    }

    void clear()
    {
      data_3d_.clear();
      data_4d_.clear();
    }
  };

  class KinodynamicAstar
  {
  private:
    /* ---------- main data structure ---------- */
    std::vector<PathNodePtr> path_node_pool_; // pointers to the path nodes
    int use_node_num_, iter_num_;
    NodeHashTable expanded_nodes_;                                                        // Nodes that expand at any given parent node
    std::priority_queue<PathNodePtr, std::vector<PathNodePtr>, NodeComparator> open_set_; // this is a priority queue
    std::vector<PathNodePtr> path_nodes_;                                                 // final path nodes

    /* ---------- record data ---------- */
    Eigen::Vector3d start_vel_, end_vel_, start_acc_;
    Eigen::Matrix<double, 6, 6> phi_; // state transit matrix

    DynamicEDTOctomap *OctoEDT; // pointer to the EDT of Octomap
    octomap::OcTree *octomap_tree;
    octomap::AbstractOcTree *abstract_tree;
    bool is_shot_succ_ = false;
    Eigen::MatrixXd coef_shot_;
    double t_shot_;
    bool has_path_ = false;

    /* ---------- parameter ---------- */
    /* search */
    double max_tau_ = 0.25;
    double init_max_tau_ = 0.8;
    double max_vel_ = 3.0;
    double max_acc_ = 3.0;
    double w_time_ = 10.0;
    double horizon_;
    double lambda_heu_;
    double margin_;
    int allocate_num_;
    int check_num_;
    double tie_breaker_ = 1.0 + 1.0 / 10000;

    /* map */
    double resolution_, inv_resolution_, time_resolution_, inv_time_resolution_;
    Eigen::Vector3d origin_, map_size_3d_;
    Eigen::Vector3d min_, max_;
    double time_origin_;

    Eigen::Vector3d droneLoc;

    /* helper */
    Eigen::Vector3i posToIndex(Eigen::Vector3d pt); // convert position to index in map
    int timeToIndex(double time);                   // current time to index in the time array
    void retrievePath(PathNodePtr end_node);        // get the final path

    /* shot trajectory */
    std::vector<double> cubic(double a, double b, double c, double d);
    std::vector<double> quartic(double a, double b, double c, double d, double e);
    bool computeShotTraj(Eigen::VectorXd state1, Eigen::VectorXd state2, double time_to_goal);
    double estimateHeuristic(Eigen::VectorXd x1, Eigen::VectorXd x2, double &optimal_time);
    double get_EDT_cost(float distance);

    /* state propagation */
    void stateTransit(Eigen::Matrix<double, 6, 1> &state0, Eigen::Matrix<double, 6, 1> &state1,
                      Eigen::Vector3d um, double tau);

  public:
    KinodynamicAstar(){};
    ~KinodynamicAstar();

    enum
    {
      REACH_HORIZON = 1,
      REACH_END = 2,
      NO_PATH = 3
    };

    /* main API */
    void setParam(ros::NodeHandle &nh);
    void init(octomap::point3d min, octomap::point3d max, Eigen::Vector3d dronePose);
    void reset();
    int search(Eigen::Vector3d start_pt, Eigen::Vector3d start_vel, Eigen::Vector3d start_acc,
               Eigen::Vector3d end_pt, Eigen::Vector3d end_vel, float &time_to_desination, bool init, visualization_msgs::MarkerArray MMD_map_vis, ros::Publisher MMD_map,
               visualization_msgs::MarkerArray A_star_vis, ros::Publisher A_star_pub, std::string path_to_weights, bool dynamic = false,
               double time_start = -1.0); // main function which starts the Kinodynamic Planner

    void setEnvironment(DynamicEDTOctomap *ptr, octomap::OcTree *octomap_tree, octomap::AbstractOcTree *abstract_tree, octomap::point3d map_start_pt, octomap::point3d map_end_pt);

    std::vector<Eigen::Vector3d> getKinoTraj(double delta_t); // this is used to get the kinodynamic trajectory

    void getSamples(double &ts, std::vector<Eigen::Vector3d> &point_set,
                    std::vector<Eigen::Vector3d> &start_end_derivatives);

    std::vector<PathNodePtr> getVisitedNodes();

    float determine_mmd_threshold_value(Eigen::MatrixXf noise_distribution2, int num_samples_of_distance_distribution);

    typedef std::shared_ptr<KinodynamicAstar> Ptr;
  };

} // namespace fast_planner

#endif
