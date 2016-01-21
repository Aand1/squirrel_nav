// InflationLayer.cpp --- 
// 
// Filename: InflationLayer.cpp
// Description: 
// Author: Federico Boniardi
// Maintainer: boniardi@informatik.uni-freiburg.de
// Created: Mi Jan 20 14:57:28 2016 (+0100)
// Version: 0.1.0
// Last-Updated: 
//           By: 
//     Update #: 0
// URL: 
// Keywords: 
// Compatibility: 
// 
// 

// Commentary:
// /*********************************************************************
//  *
//  * Software License Agreement (BSD License)
//  *
//  *  Copyright (c) 2008, 2013, Willow Garage, Inc.
//  *  All rights reserved.
//  *
//  *  Redistribution and use in source and binary forms, with or without
//  *  modification, are permitted provided that the following conditions
//  *  are met:
//  *
//  *   * Redistributions of source code must retain the above copyright
//  *     notice, this list of conditions and the following disclaimer.
//  *   * Redistributions in binary form must reproduce the above
//  *     copyright notice, this list of conditions and the following
//  *     disclaimer in the documentation and/or other materials provided
//  *     with the distribution.
//  *   * Neither the name of Willow Garage, Inc. nor the names of its
//  *     contributors may be used to endorse or promote products derived
//  *     from this software without specific prior written permission.
//  *
//  *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//  *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//  *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//  *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
//  *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//  *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//  *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//  *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//  *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//  *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
//  *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//  *  POSSIBILITY OF SUCH DAMAGE.
//  *
//  * Author: Eitan Marder-Eppstein
//  *         David V. Lu!!
//  *********************************************************************/

// Change Log:
//

// Code:

#include "squirrel_navigation/InflationLayer.h"

PLUGINLIB_EXPORT_CLASS(squirrel_navigation::InflationLayer, costmap_2d::Layer)

namespace squirrel_navigation {

InflationLayer::InflationLayer( void ) :
    inflation_radius_(0),
    inscribed_radius_from_footprint_(true),
    weight_(0),
    cell_inflation_radius_(0),
    cached_cell_inflation_radius_(0),
    dsrv_(NULL),
    seen_(NULL),
    cached_costs_(NULL),
    cached_distances_(NULL),
    last_min_x_(-std::numeric_limits<float>::max()),
    last_min_y_(-std::numeric_limits<float>::max()),
    last_max_x_(std::numeric_limits<float>::max()),
    last_max_y_(std::numeric_limits<float>::max())
{
  inflation_access_ = new boost::recursive_mutex();
}

InflationLayer::~InflationLayer( void )
{
  deleteKernels();
  if (dsrv_)
    delete dsrv_;
};

void InflationLayer::onInitialize( void )
{
  {
    boost::unique_lock < boost::recursive_mutex > lock(*inflation_access_);
    ros::NodeHandle nh("~/" + name_), g_nh;
    current_ = true;
    if (seen_)
      delete[] seen_;
    seen_ = NULL;
    seen_size_ = 0;
    need_reinflation_ = false;

    dynamic_reconfigure::Server<InflationLayerPluginConfig>::CallbackType cb = boost::bind(
        &InflationLayer::reconfigureCB, this, _1, _2);

    if ( dsrv_ != NULL ) {
      dsrv_->clearCallback();
      dsrv_->setCallback(cb);
    } else {
      dsrv_ = new dynamic_reconfigure::Server<InflationLayerPluginConfig>(ros::NodeHandle("~/" + name_));
      dsrv_->setCallback(cb);
    }
  }

  matchSize();
}

void InflationLayer::reconfigureCB( InflationLayerPluginConfig &config, uint32_t level )
{
  inscribed_radius_from_footprint_ = config.inscribed_radius_from_footprint;

  double inscribed_radius;
  if ( not inscribed_radius_from_footprint_ )
    inscribed_radius = config.inscribed_radius;
  else
    inscribed_radius = layered_costmap_->getInscribedRadius();
  
  setInflationParameters(inscribed_radius,config.inflation_radius, config.cost_scaling_factor);

  if (enabled_ != config.enabled) {
    enabled_ = config.enabled;
    need_reinflation_ = true;
  }
}

void InflationLayer::matchSize( void )
{
  boost::unique_lock < boost::recursive_mutex > lock(*inflation_access_);
  costmap_2d::Costmap2D* costmap = layered_costmap_->getCostmap();
  resolution_ = costmap->getResolution();
  cell_inflation_radius_ = cellDistance(inflation_radius_);
  computeCaches();

  unsigned int size_x = costmap->getSizeInCellsX(), size_y = costmap->getSizeInCellsY();
  if (seen_)
    delete[] seen_;
  seen_size_ = size_x * size_y;
  seen_ = new bool[seen_size_];
}

void InflationLayer::updateBounds(double robot_x, double robot_y, double robot_yaw, double* min_x,
                                  double* min_y, double* max_x, double* max_y )
{
  if ( need_reinflation_ ) {
    last_min_x_ = *min_x;
    last_min_y_ = *min_y;
    last_max_x_ = *max_x;
    last_max_y_ = *max_y;
    // For some reason when I make these -<double>::max() it does not
    // work with Costmap2D::worldToMapEnforceBounds(), so I'm using
    // -<float>::max() instead.
    *min_x = -std::numeric_limits<float>::max();
    *min_y = -std::numeric_limits<float>::max();
    *max_x = std::numeric_limits<float>::max();
    *max_y = std::numeric_limits<float>::max();
    need_reinflation_ = false;
  } else {
    double tmp_min_x = last_min_x_;
    double tmp_min_y = last_min_y_;
    double tmp_max_x = last_max_x_;
    double tmp_max_y = last_max_y_;
    last_min_x_ = *min_x;
    last_min_y_ = *min_y;
    last_max_x_ = *max_x;
    last_max_y_ = *max_y;
    // We need to include in the inflation cells outside the bounding
    // box by the amount of the cell_inflation_radius_.  Cells
    // up to that distance outside the box can still influence the costs
    // stored in cells inside the box.
    *min_x = std::min(tmp_min_x, *min_x) - inflation_radius_;
    *min_y = std::min(tmp_min_y, *min_y) - inflation_radius_;
    *max_x = std::max(tmp_max_x, *max_x) + inflation_radius_;
    *max_y = std::max(tmp_max_y, *max_y) + inflation_radius_;
  }
}

void InflationLayer::onFootprintChanged( void )
{
  if ( inscribed_radius_from_footprint_ )
    inscribed_radius_ = layered_costmap_->getInscribedRadius();
  
  cell_inflation_radius_ = cellDistance(inflation_radius_);
  computeCaches();
  need_reinflation_ = true;

  ROS_DEBUG("InflationLayer::onFootprintChanged(): num footprint points: %lu,"
            " inscribed_radius_ = %.3f, inflation_radius_ = %.3f",
            layered_costmap_->getFootprint().size(), inscribed_radius_, inflation_radius_);
}

void InflationLayer::updateCosts( costmap_2d::Costmap2D& master_grid, int min_i, int min_j, int max_i, int max_j )
{
  boost::unique_lock < boost::recursive_mutex > lock(*inflation_access_);
  if (!enabled_)
    return;

  // make sure the inflation queue is empty at the beginning of the cycle (should always be true)
  ROS_ASSERT_MSG(inflation_queue_.empty(), "The inflation queue must be empty at the beginning of inflation");

  unsigned char* master_array = master_grid.getCharMap();
  unsigned int size_x = master_grid.getSizeInCellsX(), size_y = master_grid.getSizeInCellsY();

  if (seen_ == NULL) {
    ROS_WARN("InflationLayer::updateCosts(): seen_ array is NULL");
    seen_size_ = size_x * size_y;
    seen_ = new bool[seen_size_];
  }
  else if (seen_size_ != size_x * size_y)
  {
    ROS_WARN("InflationLayer::updateCosts(): seen_ array size is wrong");
    delete[] seen_;
    seen_size_ = size_x * size_y;
    seen_ = new bool[seen_size_];
  }
  memset(seen_, false, size_x * size_y * sizeof(bool));

  min_i = std::max(0, min_i);
  min_j = std::max(0, min_j);
  max_i = std::min(int(size_x), max_i);
  max_j = std::min(int(size_y), max_j);
  
  for (int j = min_j; j < max_j; j++) {
    for (int i = min_i; i < max_i; i++) {
      int index = master_grid.getIndex(i, j);
      unsigned char cost = master_array[index];
      if (cost == costmap_2d::LETHAL_OBSTACLE) {
        enqueue(master_array, index, i, j, i, j);
      }
    }
  }

  while (!inflation_queue_.empty())
  {
    // get the highest priority cell and pop it off the priority queue
    const CellData& current_cell = inflation_queue_.top();

    unsigned int index = current_cell.index_;
    unsigned int mx = current_cell.x_;
    unsigned int my = current_cell.y_;
    unsigned int sx = current_cell.src_x_;
    unsigned int sy = current_cell.src_y_;

    // pop once we have our cell info
    inflation_queue_.pop();

    // attempt to put the neighbors of the current cell onto the queue
    if (mx > 0)
      enqueue(master_array, index - 1, mx - 1, my, sx, sy);
    if (my > 0)
      enqueue(master_array, index - size_x, mx, my - 1, sx, sy);
    if (mx < size_x - 1)
      enqueue(master_array, index + 1, mx + 1, my, sx, sy);
    if (my < size_y - 1)
      enqueue(master_array, index + size_x, mx, my + 1, sx, sy);
  }
}

inline void InflationLayer::enqueue( unsigned char* grid, unsigned int index, unsigned int mx,
                                     unsigned int my, unsigned int src_x, unsigned int src_y )
{
  // set the cost of the cell being inserted
  if (!seen_[index])
  {
    // we compute our distance table one cell further than the inflation radius dictates so we can make the check below
    double distance = distanceLookup(mx, my, src_x, src_y);

    // we only want to put the cell in the queue if it is within the inflation radius of the obstacle point
    if (distance > cell_inflation_radius_)
      return;

    // assign the cost associated with the distance from an obstacle to the cell
    unsigned char cost = costLookup(mx, my, src_x, src_y);
    unsigned char old_cost = grid[index];

    if (old_cost == costmap_2d::NO_INFORMATION && cost >= costmap_2d::INSCRIBED_INFLATED_OBSTACLE)
      grid[index] = cost;
    else
      grid[index] = std::max(old_cost, cost);
    // push the cell data onto the queue and mark
    seen_[index] = true;
    CellData data(distance, index, mx, my, src_x, src_y);
    inflation_queue_.push(data);
  }
}

void InflationLayer::computeCaches( void )
{
  if ( cell_inflation_radius_ == 0 )
    return;

  // based on the inflation radius... compute distance and cost caches
  if ( cell_inflation_radius_ != cached_cell_inflation_radius_ ) {
    deleteKernels();

    cached_costs_ = new unsigned char*[cell_inflation_radius_ + 2];
    cached_distances_ = new double*[cell_inflation_radius_ + 2];

    for (unsigned int i = 0; i <= cell_inflation_radius_ + 1; ++i) {
      cached_costs_[i] = new unsigned char[cell_inflation_radius_ + 2];
      cached_distances_[i] = new double[cell_inflation_radius_ + 2];
      for (unsigned int j = 0; j <= cell_inflation_radius_ + 1; ++j) {
        cached_distances_[i][j] = hypot(i, j);
      }
    }

    cached_cell_inflation_radius_ = cell_inflation_radius_;
  }

  for (unsigned int i = 0; i <= cell_inflation_radius_ + 1; ++i) {
    for (unsigned int j = 0; j <= cell_inflation_radius_ + 1; ++j) {
      cached_costs_[i][j] = computeCost(cached_distances_[i][j]);
    }
  }
}

void InflationLayer::deleteKernels( void )
{
  if ( cached_distances_ != NULL ) {
    for (unsigned int i = 0; i <= cached_cell_inflation_radius_ + 1; ++i) {
      if (cached_distances_[i])
        delete[] cached_distances_[i];
    }
    if ( cached_distances_ )
      delete[] cached_distances_;
    cached_distances_ = NULL;
  }

  if ( cached_costs_ != NULL ) {
    for (unsigned int i = 0; i <= cached_cell_inflation_radius_ + 1; ++i) {
      if (cached_costs_[i])
        delete[] cached_costs_[i];
    }
    delete[] cached_costs_;
    cached_costs_ = NULL;
  }
}

void InflationLayer::setInflationParameters( double inscribed_radius, double inflation_radius, double cost_scaling_factor )
{
  if ( weight_ != cost_scaling_factor or
       inflation_radius_ != inflation_radius or
       inscribed_radius_ != inscribed_radius )
  {
    // Lock here so that reconfiguring the inflation radius doesn't cause segfaults
    // when accessing the cached arrays
    boost::unique_lock < boost::recursive_mutex > lock(*inflation_access_);

    inflation_radius_ = inflation_radius;
    inscribed_radius_ = inscribed_radius;
    cell_inflation_radius_ = cellDistance(inflation_radius_);
    weight_ = cost_scaling_factor;
    need_reinflation_ = true;
    computeCaches();
  }
}

}  // namespace costmap_2d
// 
// InflationLayer.cpp ends here