#include "ros/ros.h"
#include <octomap/octomap.h>
#include <octomap_msgs/Octomap.h>
#include <octomap_msgs/conversions.h>
#include <octomap/ColorOcTree.h>
#include <octomap/OcTreeNode.h>
#include <iostream>
#include "datatypes_squirrel.h"
#include <pcl/kdtree/kdtree_flann.h>
#include <message_filters/subscriber.h>
#include <message_filters/synchronizer.h>
#include <message_filters/sync_policies/approximate_time.h>

using namespace octomap;
using namespace std;
using namespace message_filters;
typedef sync_policies::ApproximateTime<octomap_msgs::Octomap,octomap_msgs::Octomap> MySyncPolicy;
class PublishOctomap
{

  private:
    ros::NodeHandle nh;
    ros::Subscriber cloud_sub;
    ros::Publisher publisher;
    pcl::KdTreeFLANN <Point> kdtree;
    pcl::PCDReader reader;
    message_filters::Subscriber<octomap_msgs::Octomap> binary_sub;
    message_filters::Subscriber<octomap_msgs::Octomap> full_sub;
    Synchronizer<MySyncPolicy> sync;
    PointCloud::Ptr cloud;
    OcTree *original_tree;
  public:

    string map_filename;
    void load_map()
    {
      original_tree = new OcTree(map_filename);
      for(OcTree::leaf_iterator it = original_tree->begin_leafs(),end = original_tree->end_leafs(); it!= end; ++it)
      {
        if(it->getOccupancy() > 0.5)
        {
          point3d point = it.getCoordinate();
          pcl::PointXYZ pt_pcl;
          pt_pcl.x = point.x();
          pt_pcl.y = point.y();
          pt_pcl.z = point.z();
          cloud->points.push_back(pt_pcl);
        }
      }

      fprintf(stderr,"%d\n",cloud->points.size());
      kdtree.setInputCloud(cloud);

    }
    PublishOctomap():cloud(new PointCloud),binary_sub(nh,"octomap_movable_binary",50),full_sub(nh, "octomap_movable_full", 50),sync(MySyncPolicy(10), binary_sub, full_sub)
    {

      publisher = nh.advertise<octomap_msgs::Octomap>("/octomap_movable_color",10);//Publising the filtered pointcloud
//      reader.read("/home/dewan/octo_maps/18012017.pcd",*cloud);
      sync.registerCallback(boost::bind(&PublishOctomap::callback,this, _1, _2));

    }
    void callback(const octomap_msgs::OctomapConstPtr &map_binary, const octomap_msgs::OctomapConstPtr &map_full)
    {

      octomap::OcTree* octree = new octomap::OcTree(map_full->resolution);
      octomap::ColorOcTree* octree_color = new octomap::ColorOcTree(map_binary->resolution);
      std::stringstream datastream;
      if(map_binary->data.size() > 0)
      {
        datastream.write((const char*) &map_binary->data[0], map_binary->data.size());
        octree_color->readBinaryData(datastream);
      }
      if(map_full->data.size() > 0)
      {
        octomap::AbstractOcTree* tree = octomap::AbstractOcTree::createTree(map_full->id,map_full->resolution);
        datastream.write((const char*) &map_full->data[0], map_full->data.size());
        tree->readData(datastream);
        octree = dynamic_cast<octomap::OcTree*>(tree);
      }

      for(octomap::OcTree::leaf_iterator it = octree->begin_leafs(),end = octree->end_leafs(); it!= end; ++it)
      {
        if(it->getOccupancy() < 0.9)
        {
          octomap::point3d pt = it.getCoordinate();
          octree_color->deleteNode(pt,it.getDepth());
        }
      }
      for(octomap::ColorOcTree::leaf_iterator it = octree_color->begin_leafs(),end = octree_color->end_leafs(); it!= end; ++it)
      {

        octomap::point3d point = it.getCoordinate();
        pcl::PointXYZ pt_pcl;
        pt_pcl.x = point.x();
        pt_pcl.y = point.y();
        pt_pcl.z = point.z();
        std::vector <int> pointIdxRadiusSearch;
        std::vector <float> pointRadiusSquaredDistance;
        if(kdtree.nearestKSearch(pt_pcl,1,pointIdxRadiusSearch,pointRadiusSquaredDistance) > 0)
          if(sqrt(pointRadiusSquaredDistance[0]) < 0.1)
            octree_color->deleteNode(point,it.getDepth());
          else
            it->setColor(0,255,0);

      //  fprintf(stderr,"%f\n",it->getOccupancy());


      }
      octomap_msgs::Octomap msg_pub;
      octomap_msgs::fullMapToMsg(*octree_color, msg_pub);
      msg_pub.header.frame_id = "map";
      publisher.publish(msg_pub);
      //delete octree;
      delete octree_color;
      delete octree;
    }

};




int main(int argc, char **argv)
{
	ros::init(argc, argv, "squirrel_dynamic_filter_publish_color_octomap");

  PublishOctomap publish_octomap;

  publish_octomap.map_filename = argv[1];
  publish_octomap.load_map();


	while(ros::ok())
	{
		ros::spinOnce();
  }
  return 1;

}
