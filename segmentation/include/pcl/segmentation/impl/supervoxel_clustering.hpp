/*
 * Software License Agreement (BSD License)
 *
 *  Point Cloud Library (PCL) - www.pointclouds.org
 *
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * Author : jpapon@gmail.com
 * Email  : jpapon@gmail.com
 *
 */

#ifndef PCL_SEGMENTATION_SUPERVOXEL_CLUSTERING_HPP_
#define PCL_SEGMENTATION_SUPERVOXEL_CLUSTERING_HPP_

#include <pcl/segmentation/supervoxel_clustering.h>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT>
pcl::SupervoxelClustering<PointT>::SupervoxelClustering (float voxel_resolution, float seed_resolution, bool use_single_camera_transform, bool prune_close_seeds) :
  resolution_ (voxel_resolution),
  seed_resolution_ (seed_resolution),
  adjacency_octree_ (),
  voxel_centroid_cloud_ (),
  color_importance_ (0.1f),
  spatial_importance_ (0.4f),
  normal_importance_ (1.0f),
  ignore_input_normals_ (false),
  prune_close_seeds_ (prune_close_seeds),
  label_colors_ (0)
{
  adjacency_octree_.reset (new OctreeAdjacencyT (resolution_));
  if (use_single_camera_transform)
    adjacency_octree_->setTransformFunction (boost::bind (&SupervoxelClustering::transformFunction, this, _1));  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT>
pcl::SupervoxelClustering<PointT>::~SupervoxelClustering ()
{

}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::setInputCloud (const typename pcl::PointCloud<PointT>::ConstPtr& cloud)
{
  if ( cloud->size () == 0 )
  {
    PCL_ERROR ("[pcl::SupervoxelClustering::setInputCloud] Empty cloud set, doing nothing \n");
    return;
  }
  
  input_ = cloud;
  adjacency_octree_->setInputCloud (cloud);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::extract (std::map<uint32_t,typename Supervoxel::Ptr > &supervoxel_clusters)
{
  //timer_.reset ();
  //double t_start = timer_.getTime ();
  //std::cout << "Init compute  \n";
  bool segmentation_is_possible = initCompute ();
  if ( !segmentation_is_possible )
  {
    deinitCompute ();
    return;
  }
  
  //std::cout << "Preparing for segmentation \n";
  segmentation_is_possible = prepareForSegmentation ();
  if ( !segmentation_is_possible )
  {
    deinitCompute ();
    return;
  }
  
  //double t_prep = timer_.getTime ();
  //std::cout << "Placing Seeds" << std::endl;
  std::vector<int> seed_indices;
  selectInitialSupervoxelSeeds (seed_indices);
  //std::cout << "Creating helpers "<<std::endl;
  createSupervoxelHelpers (seed_indices);
  //double t_seeds = timer_.getTime ();
  
  
  //std::cout << "Expanding the supervoxels" << std::endl;
  int max_depth = static_cast<int> (1.8f*seed_resolution_/resolution_);
  expandSupervoxels (max_depth);
  //double t_iterate = timer_.getTime ();
    
  //std::cout << "Making Supervoxel structures" << std::endl;
  makeSupervoxels (supervoxel_clusters);
  //double t_supervoxels = timer_.getTime ();
  
 // std::cout << "--------------------------------- Timing Report --------------------------------- \n";
 // std::cout << "Time to prep (normals, neighbors, voxelization)="<<t_prep-t_start<<" ms\n";
 // std::cout << "Time to seed clusters                          ="<<t_seeds-t_prep<<" ms\n";
 // std::cout << "Time to expand clusters                        ="<<t_iterate-t_seeds<<" ms\n";
 // std::cout << "Time to create supervoxel structures           ="<<t_supervoxels-t_iterate<<" ms\n";
 // std::cout << "Total run time                                 ="<<t_supervoxels-t_start<<" ms\n";
 // std::cout << "--------------------------------------------------------------------------------- \n";
  
  deinitCompute ();
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::refineSupervoxels (int num_itr, std::map<uint32_t,typename Supervoxel::Ptr > &supervoxel_clusters)
{
  if (supervoxel_helpers_.size () == 0)
  {
    PCL_ERROR ("[pcl::SupervoxelClustering::refineVoxelNormals] Supervoxels not extracted, doing nothing - (Call extract first!) \n");
    return;
  }

  int max_depth = static_cast<int> (1.8f*seed_resolution_/resolution_);
  for (int i = 0; i < num_itr; ++i)
  {
    for (typename HelperListT::iterator sv_itr = supervoxel_helpers_.begin (); sv_itr != supervoxel_helpers_.end (); ++sv_itr)
    {
      sv_itr->refineNormals ();
    }
    
    reseedSupervoxels ();
    expandSupervoxels (max_depth);
  }
  

  makeSupervoxels (supervoxel_clusters);

}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename PointT> bool
pcl::SupervoxelClustering<PointT>::prepareForSegmentation ()
{
  
  // if user forgot to pass point cloud or if it is empty
  if ( input_->points.size () == 0 )
    return (false);
  
  //Add the new cloud of data to the octree
  //std::cout << "Populating adjacency octree with new cloud \n";
  //double prep_start = timer_.getTime ();
  adjacency_octree_->addPointsFromInputCloud ();
  //double prep_end = timer_.getTime ();
  //std::cout<<"Time elapsed populating octree with next frame ="<<prep_end-prep_start<<" ms\n";
  
  //Compute normals and insert data for centroids into data field of octree
  //double normals_start = timer_.getTime ();
  computeVoxelData ();
  //double normals_end = timer_.getTime ();
  //std::cout << "Time elapsed finding normals and pushing into octree ="<<normals_end-normals_start<<" ms\n";
    
  return true;
}

template <typename PointT> void
pcl::SupervoxelClustering<PointT>::computeVoxelData ()
{
  voxel_centroid_cloud_.reset (new VoxelCloudT);
  voxel_centroid_cloud_->resize (adjacency_octree_->getLeafCount ());
  typename LeafVectorT::iterator leaf_itr = adjacency_octree_->begin ();
  typename VoxelCloudT::iterator cent_cloud_itr = voxel_centroid_cloud_->begin ();
  for (int idx = 0 ; leaf_itr != adjacency_octree_->end (); ++leaf_itr, ++cent_cloud_itr, ++idx)
  {
    VoxelData& new_voxel_data = (*leaf_itr)->getData ();
    //Add the point to the centroid cloud
    new_voxel_data.getPoint (*cent_cloud_itr);
    //Push correct index in
    new_voxel_data.idx_ = idx;
  }
  
  //If input point type does not have normals, we need to compute them
  if (!pcl::traits::has_normal<PointT>::value || ignore_input_normals_)
  {
    for (leaf_itr = adjacency_octree_->begin (), cent_cloud_itr = voxel_centroid_cloud_->begin (); leaf_itr != adjacency_octree_->end (); ++leaf_itr, ++cent_cloud_itr)
    {
      VoxelData& new_voxel_data = (*leaf_itr)->getData ();
      //For every point, get its neighbors, build an index vector, compute normal
      std::vector<int> indices;
      indices.reserve (81); 
      //Push this point
      indices.push_back (new_voxel_data.idx_);
      for (typename LeafContainerT::const_iterator neighb_itr=(*leaf_itr)->cbegin (); neighb_itr!=(*leaf_itr)->cend (); ++neighb_itr)
      {
        VoxelData& neighb_voxel_data = (*neighb_itr)->getData ();
        //Push neighbor index
        indices.push_back (neighb_voxel_data.idx_);
        //Get neighbors neighbors, push onto cloud
        for (typename LeafContainerT::const_iterator neighb_neighb_itr=(*neighb_itr)->cbegin (); neighb_neighb_itr!=(*neighb_itr)->cend (); ++neighb_neighb_itr)
        {
          VoxelData& neighb2_voxel_data = (*neighb_neighb_itr)->getData ();
          indices.push_back (neighb2_voxel_data.idx_);
        }
      }
      //Compute normal
      //Is this temp vector really the only way to do this? Why can't I substitute vectormap for vector?
      Eigen::Vector4f temp_vec;
      pcl::computePointNormal (*voxel_centroid_cloud_, indices,temp_vec, new_voxel_data.voxel_centroid_.curvature);
      new_voxel_data.voxel_centroid_.getNormalVector4fMap () = temp_vec;
      pcl::flipNormalTowardsViewpoint (new_voxel_data.voxel_centroid_, 0.0f,0.0f,0.0f, new_voxel_data.voxel_centroid_.normal_x,
                                                                                       new_voxel_data.voxel_centroid_.normal_y,
                                                                                       new_voxel_data.voxel_centroid_.normal_z);
      //Put curvature and normal into voxel_centroid_cloud_
      new_voxel_data.getPoint (*cent_cloud_itr);
    }  
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::expandSupervoxels ( int depth )
{
  
  
  for (int i = 1; i < depth; ++i)
  {
      //Expand the the supervoxels by one iteration
      for (typename HelperListT::iterator sv_itr = supervoxel_helpers_.begin (); sv_itr != supervoxel_helpers_.end (); ++sv_itr)
      {
        sv_itr->expand ();
      }
      
      //Update the centers to reflect new centers
      for (typename HelperListT::iterator sv_itr = supervoxel_helpers_.begin (); sv_itr != supervoxel_helpers_.end (); )
      {
        if (sv_itr->size () == 0)
        {
          sv_itr = supervoxel_helpers_.erase (sv_itr);
        }
        else
        {
          sv_itr->updateCentroid ();
          ++sv_itr;
        } 
      }

  }

}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::makeSupervoxels (std::map<uint32_t,typename Supervoxel::Ptr > &supervoxel_clusters)
{
  supervoxel_clusters.clear ();
  for (typename HelperListT::iterator sv_itr = supervoxel_helpers_.begin (); sv_itr != supervoxel_helpers_.end (); ++sv_itr)
  {
    uint32_t label = sv_itr->getLabel ();
    supervoxel_clusters[label].reset (new Supervoxel);
    sv_itr->getCentroid (supervoxel_clusters[label]->centroid_);
    sv_itr->getVoxels (supervoxel_clusters[label]->voxels_);
  }
  //Make sure that color vector is big enough
  initializeLabelColors ();
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::createSupervoxelHelpers (std::vector<int> &seed_indices)
{
  
  supervoxel_helpers_.clear ();
  for (size_t i = 0; i < seed_indices.size (); ++i)
  {
    supervoxel_helpers_.push_back (new SupervoxelHelper(i+1,this));
    //Find which leaf corresponds to this seed index
    LeafContainerT* seed_leaf = adjacency_octree_->at(seed_indices[i]);//adjacency_octree_->getLeafContainerAtPoint (seed_points[i]);
    if (seed_leaf)
    {
      supervoxel_helpers_.back ().addLeaf (seed_leaf);
    }
    else
    {
      PCL_WARN ("Could not find leaf in pcl::SupervoxelClustering<PointT>::createSupervoxelHelpers - supervoxel will be deleted \n");
    }
  }
  
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::selectInitialSupervoxelSeeds (std::vector<int> &seed_indices)
{
  //Initialize octree with voxel centroids
  pcl::octree::OctreePointCloudSearch <VoxelT> seed_octree (seed_resolution_);
  seed_octree.setInputCloud (voxel_centroid_cloud_);
  seed_octree.addPointsFromInputCloud ();
  //std::cout << "Size of octree ="<<seed_octree.getLeafCount ()<<"\n";
  std::vector<VoxelT, Eigen::aligned_allocator<VoxelT> > voxel_centers; 
  int num_seeds = seed_octree.getOccupiedVoxelCenters(voxel_centers); 
  //std::cout << "Number of seed points before filtering="<<voxel_centers.size ()<<std::endl;

  std::vector<int> seed_indices_orig;
  seed_indices_orig.resize (num_seeds, 0);
  seed_indices.clear ();
  std::vector<int> closest_index;
  std::vector<float> distance;
  closest_index.resize(1,0);
  distance.resize(1,0);
  
  voxel_kdtree_.reset (new pcl::search::KdTree<VoxelT>);
  voxel_kdtree_ ->setInputCloud (voxel_centroid_cloud_);
  //Find closest point to seed_resolution octree centroids in voxel_centroid_cloud
  for (int i = 0; i < num_seeds; ++i)  
  {
    voxel_kdtree_->nearestKSearch (voxel_centers[i], 1, closest_index, distance);
    seed_indices_orig[i] = closest_index[0];
  }
  
  //Shift seeds to voxels within set of neighbors with min curvature (iteratively)
  typename VoxelCloudT::Ptr seed_cloud_ (new VoxelCloudT);
  seed_cloud_->reserve (seed_indices_orig.size ());
  std::vector<int> voxel_cloud_indices (num_seeds);
  // This is an important parameter - determines maximum shift - here, it is seed resolution (on an axis aligned plane)
  int search_depth = static_cast<int> (1.0f*seed_resolution_/resolution_);
  for (size_t i = 0; i < seed_indices_orig.size (); ++i)
  {
    int idx = seed_indices_orig[i];
    //Shift based on curvature, number of times based on voxel to seed size ratio
    for (int k = 0; k < search_depth; ++k)
      idx = findNeighborMinCurvature (idx);
    voxel_cloud_indices [i] = idx;
    seed_cloud_->push_back (voxel_centroid_cloud_->at(idx));
  }
  num_seeds = seed_cloud_->size ();
  //If we're not pruning, we're done.
  if (!prune_close_seeds_)
  {
    seed_indices = voxel_cloud_indices;
    return;
  }
   
  voxel_kdtree_.reset (new pcl::search::KdTree<VoxelT> (false));
  voxel_kdtree_ ->setInputCloud (seed_cloud_);
  std::vector<int> neighbors;
  std::vector<float> sqr_distances;
 
  //Now we will check if seeds are near to other seeds
  std::vector<typename SeedNHood::Ptr> seed_nhoods;
  float search_radius = seed_resolution_ / 2;
  for (int i = 0; i < voxel_cloud_indices.size (); ++i)  
  {
    int voxel_idx = voxel_cloud_indices[i];    
    int num_neighbors = voxel_kdtree_->radiusSearch (voxel_centroid_cloud_->at(voxel_idx), search_radius , neighbors, sqr_distances);
    //neighbors are unsorted - we sort them here by INDEX
    std::sort (neighbors.begin(), neighbors.end());
    seed_nhoods.push_back (boost::make_shared<SeedNHood> ());
    seed_nhoods[i]->neighbor_indices_ = neighbors;
    seed_nhoods[i]->voxel_idx_ = voxel_idx;
    seed_nhoods[i]->seed_idx_ = i;
    seed_nhoods[i]->num_active_ = num_neighbors;
  }
  //Now sort by number of num_active
  std::sort (seed_nhoods.begin (), seed_nhoods.end (), SeedNHood() );
  //Now iteratively remove seed with most neighbors, updating num active of all below
  //Stopping condition is 1 in radius (itself) so no other seeds within radius
  int max_in_radius = 1;
  int num_removed = 0;
  while (num_removed < seed_nhoods.size () && seed_nhoods[num_removed]->num_active_ > max_in_radius)
  {
    int idx_to_remove = seed_nhoods[num_removed]->seed_idx_;
    seed_nhoods[num_removed]->num_active_ = -1;
    //Search remaining seeds for idx_to_remove, if found, reduce num active for it
    for (int i = num_removed + 1; i < seed_nhoods.size (); ++i)
    {
      if (std::binary_search(seed_nhoods[i]->neighbor_indices_.begin (), seed_nhoods[i]->neighbor_indices_.end (), idx_to_remove))
      {
        --seed_nhoods[i]->num_active_;
      }
    }
    ++num_removed;
    //Now resort by number of alive seeds within radius 
    std::sort (seed_nhoods.begin () + num_removed, seed_nhoods.end (), SeedNHood() );
  }
  
  //Now clear seed indices and push final indices into it
  seed_indices.clear ();
  for (typename std::vector<typename SeedNHood::Ptr>::iterator itr = seed_nhoods.begin () + num_removed; itr != seed_nhoods.end (); ++itr)
  {
    seed_indices.push_back ((*itr)->voxel_idx_);
  }
  //std::cout <<"Removed "<<num_removed<<" seeds, seed points after filtering="<<seed_indices.size ()<<std::endl;
  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> int
pcl::SupervoxelClustering<PointT>::findNeighborMinCurvature (int idx)
{
  LeafContainerT* leaf_container = adjacency_octree_->at(idx);
  //VoxelData& voxel_data = leaf_container->getData ();
  int min_idx = idx;
  double min_curvature = voxel_centroid_cloud_->points[idx].curvature;
  for (typename LeafContainerT::const_iterator neighb_itr=leaf_container->cbegin (); neighb_itr!=leaf_container->cend (); ++neighb_itr)
  {
    VoxelData& neighb_voxel_data = (*neighb_itr)->getData ();
    if (neighb_voxel_data.voxel_centroid_.curvature < min_curvature)
    {
      min_curvature = neighb_voxel_data.voxel_centroid_.curvature ;
      min_idx = neighb_voxel_data.idx_;
    }
    
    //Get neighbors neighbors, push onto cloud
    //for (typename LeafContainerT::const_iterator neighb_neighb_itr=(*neighb_itr)->cbegin (); neighb_neighb_itr!=(*neighb_itr)->cend (); ++neighb_neighb_itr)
    //{
    //  VoxelData& neighb2_voxel_data = (*neighb_neighb_itr)->getData ();
    //  indices.push_back (neighb2_voxel_data.idx_);
    //}
  }
  return min_idx;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::reseedSupervoxels ()
{
  //Go through each supervoxel and remove all it's leaves
  for (typename HelperListT::iterator sv_itr = supervoxel_helpers_.begin (); sv_itr != supervoxel_helpers_.end (); ++sv_itr)
  {
    sv_itr->removeAllLeaves ();
  }
  
  voxel_kdtree_.reset (new pcl::search::KdTree<VoxelT> (false));
  voxel_kdtree_ ->setInputCloud (voxel_centroid_cloud_);
  
  std::vector<int> closest_index;
  std::vector<float> distance;
  //Now go through each supervoxel, find voxel closest to its center, add it in
  for (typename HelperListT::iterator sv_itr = supervoxel_helpers_.begin (); sv_itr != supervoxel_helpers_.end (); ++sv_itr)
  {
    CentroidT centroid;
    sv_itr->getCentroid (centroid);
    voxel_kdtree_->nearestKSearch (centroid, 1, closest_index, distance);
    
    LeafContainerT* seed_leaf = adjacency_octree_->at (closest_index[0]);
    if (seed_leaf)
    {
      sv_itr->addLeaf (seed_leaf);
    }
    else
    {
      PCL_WARN ("Could not find leaf in pcl::SupervoxelClustering<PointT>::reseedSupervoxels - supervoxel will be deleted \n");
    }
  }
  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::transformFunction (PointT &p)
{
  p.x /= p.z;
  p.y /= p.z;
  p.z = std::log (p.z);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> float
pcl::SupervoxelClustering<PointT>::voxelDistance (const VoxelT &v1, const VoxelT &v2) const
{
  float spatial_dist = (v1.getVector3fMap () - v2.getVector3fMap ()).norm () / seed_resolution_;
  float color_dist =  (v1.getRGBVector3i () - v2.getRGBVector3i ()).norm () / 255.0f;
  //std::cout << color_dist << "\n";
  float cos_angle_normal = 1.0f - std::abs (v1.getNormalVector4fMap ().dot (v2.getNormalVector4fMap ()));
  //std::cout << "s="<<spatial_dist<<"  c="<<color_dist<<"   an="<<cos_angle_normal<<"\n";
  return  cos_angle_normal * normal_importance_ + color_dist * color_importance_+ spatial_dist * spatial_importance_;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////// GETTER FUNCTIONS
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::getSupervoxelAdjacencyList (VoxelAdjacencyList &adjacency_list_arg) const 
{
  adjacency_list_arg.clear ();
    //Add a vertex for each label, store ids in map
  std::map <uint32_t, VoxelID> label_ID_map;
  for (typename HelperListT::const_iterator sv_itr = supervoxel_helpers_.cbegin (); sv_itr != supervoxel_helpers_.cend (); ++sv_itr)
  {
    VoxelID node_id = add_vertex (adjacency_list_arg);
    adjacency_list_arg[node_id] = (sv_itr->getLabel ());
    label_ID_map.insert (std::make_pair (sv_itr->getLabel (), node_id));
  }
  
  for (typename HelperListT::const_iterator sv_itr = supervoxel_helpers_.cbegin (); sv_itr != supervoxel_helpers_.cend (); ++sv_itr)
  {
    uint32_t label = sv_itr->getLabel ();
    std::set<uint32_t> neighbor_labels;
    sv_itr->getNeighborLabels (neighbor_labels);
    for (std::set<uint32_t>::iterator label_itr = neighbor_labels.begin (); label_itr != neighbor_labels.end (); ++label_itr)
    {
      bool edge_added;
      EdgeID edge;
      VoxelID u = (label_ID_map.find (label))->second;
      VoxelID v = (label_ID_map.find (*label_itr))->second;
      boost::tie (edge, edge_added) = add_edge (u,v,adjacency_list_arg);
      //Calc distance between centers, set as edge weight
      if (edge_added)
      {
        //Find the neighbhor with this label
        CentroidT neighb_centroid_data;
        float length = std::numeric_limits<float>::max();
        for (typename HelperListT::const_iterator neighb_itr = supervoxel_helpers_.cbegin (); neighb_itr != supervoxel_helpers_.cend (); ++neighb_itr)
        {
          if (neighb_itr->getLabel () == (*label_itr))
          {
            length = voxelDistance ((sv_itr)->getCentroid (), neighb_itr->getCentroid () );
            break;
          }
        }
        adjacency_list_arg[edge] = length;
      }
    }
      
  }

}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::getSupervoxelAdjacency (std::multimap<uint32_t, uint32_t> &label_adjacency) const
{
  label_adjacency.clear ();
  for (typename HelperListT::const_iterator sv_itr = supervoxel_helpers_.cbegin (); sv_itr != supervoxel_helpers_.cend (); ++sv_itr)
  {
    uint32_t label = sv_itr->getLabel ();
    std::set<uint32_t> neighbor_labels;
    sv_itr->getNeighborLabels (neighbor_labels);
    for (std::set<uint32_t>::iterator label_itr = neighbor_labels.begin (); label_itr != neighbor_labels.end (); ++label_itr)
      label_adjacency.insert (std::pair<uint32_t,uint32_t> (label, *label_itr) );
    //if (neighbor_labels.size () == 0)
    //  std::cout << label<<"(size="<<sv_itr->size () << ") has "<<neighbor_labels.size () << "\n";
  }
  
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> pcl::PointCloud<pcl::PointXYZRGBA>::Ptr
pcl::SupervoxelClustering<PointT>::getColoredCloud () const
{
  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr colored_cloud (new pcl::PointCloud<pcl::PointXYZRGBA>);
  pcl::copyPointCloud (*input_,*colored_cloud);
  
  pcl::PointCloud <pcl::PointXYZRGBA>::iterator i_colored;
  typename pcl::PointCloud <PointT>::const_iterator i_input = input_->begin ();
  std::vector <int> indices;
  std::vector <float> sqr_distances;
  for (i_colored = colored_cloud->begin (); i_colored != colored_cloud->end (); ++i_colored,++i_input)
  {
    if ( !pcl::isFinite<PointT> (*i_input))
      i_colored->rgb = 0;
    else
    {     
      i_colored->rgb = 0;
      LeafContainerT *leaf = adjacency_octree_->getLeafContainerAtPoint (*i_input);
      VoxelData& voxel_data = leaf->getData ();
      if (voxel_data.owner_)
        i_colored->rgba = label_colors_[voxel_data.owner_->getLabel ()];
      
    }
    
  }
  
  return (colored_cloud);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> pcl::PointCloud<pcl::PointXYZRGBA>::Ptr
pcl::SupervoxelClustering<PointT>::getColoredVoxelCloud () const
{
  pcl::PointCloud<pcl::PointXYZRGBA>::Ptr colored_cloud (new pcl::PointCloud<pcl::PointXYZRGBA>);
  for (typename HelperListT::const_iterator sv_itr = supervoxel_helpers_.cbegin (); sv_itr != supervoxel_helpers_.cend (); ++sv_itr)
  {
    typename pcl::PointCloud<VoxelT>::Ptr voxels;
    sv_itr->getVoxels (voxels);
    pcl::PointCloud<pcl::PointXYZRGBA> rgb_copy;
    copyPointCloud (*voxels, rgb_copy);
    
    pcl::PointCloud<pcl::PointXYZRGBA>::iterator rgb_copy_itr = rgb_copy.begin ();
    for ( ; rgb_copy_itr != rgb_copy.end (); ++rgb_copy_itr) 
      rgb_copy_itr->rgba = label_colors_ [sv_itr->getLabel ()];
    
    *colored_cloud += rgb_copy;
  }
  
  return colored_cloud;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> pcl::PointCloud<pcl::PointXYZL>::Ptr
pcl::SupervoxelClustering<PointT>::getLabeledVoxelCloud () const
{
  pcl::PointCloud<pcl::PointXYZL>::Ptr labeled_voxel_cloud (new pcl::PointCloud<pcl::PointXYZL>);
  for (typename HelperListT::const_iterator sv_itr = supervoxel_helpers_.cbegin (); sv_itr != supervoxel_helpers_.cend (); ++sv_itr)
  {
    typename pcl::PointCloud<VoxelT>::Ptr voxels;
    sv_itr->getVoxels (voxels);
    pcl::PointCloud<pcl::PointXYZL> xyzl_copy;
    copyPointCloud (*voxels, xyzl_copy);
    
    pcl::PointCloud<pcl::PointXYZL>::iterator xyzl_copy_itr = xyzl_copy.begin ();
    for ( ; xyzl_copy_itr != xyzl_copy.end (); ++xyzl_copy_itr) 
      xyzl_copy_itr->label = sv_itr->getLabel ();
    
    *labeled_voxel_cloud += xyzl_copy;
  }
  
  return labeled_voxel_cloud;  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> pcl::PointCloud<pcl::PointXYZL>::Ptr
pcl::SupervoxelClustering<PointT>::getLabeledCloud () const
{
  pcl::PointCloud<pcl::PointXYZL>::Ptr labeled_cloud (new pcl::PointCloud<pcl::PointXYZL>);
  pcl::copyPointCloud (*input_,*labeled_cloud);
  
  pcl::PointCloud <pcl::PointXYZL>::iterator i_labeled;
  typename pcl::PointCloud <PointT>::const_iterator i_input = input_->begin ();
  std::vector <int> indices;
  std::vector <float> sqr_distances;
  for (i_labeled = labeled_cloud->begin (); i_labeled != labeled_cloud->end (); ++i_labeled,++i_input)
  {
    if ( !pcl::isFinite<PointT> (*i_input))
      i_labeled->label = 0;
    else
    {     
      i_labeled->label = 0;
      LeafContainerT *leaf = adjacency_octree_->getLeafContainerAtPoint (*i_input);
      VoxelData& voxel_data = leaf->getData ();
      if (voxel_data.owner_)
        i_labeled->label = voxel_data.owner_->getLabel ();
        
    }
      
  }
    
  return (labeled_cloud);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> pcl::PointCloud<pcl::PointNormal>::Ptr
pcl::SupervoxelClustering<PointT>::makeSupervoxelNormalCloud (std::map<uint32_t,typename Supervoxel::Ptr > &supervoxel_clusters)
{
  pcl::PointCloud<pcl::PointNormal>::Ptr normal_cloud (new pcl::PointCloud<pcl::PointNormal>);
  normal_cloud->resize (supervoxel_clusters.size ());
  typename std::map <uint32_t, typename pcl::Supervoxel::Ptr>::iterator sv_itr,sv_itr_end;
  sv_itr = supervoxel_clusters.begin ();
  sv_itr_end = supervoxel_clusters.end ();
  pcl::PointCloud<pcl::PointNormal>::iterator normal_cloud_itr = normal_cloud->begin ();
  for ( ; sv_itr != sv_itr_end; ++sv_itr, ++normal_cloud_itr)
  {
    (sv_itr->second)->getCentroidPointNormal (*normal_cloud_itr);
  }
  return normal_cloud;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> float
pcl::SupervoxelClustering<PointT>::getVoxelResolution () const
{
  return (resolution_);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::setVoxelResolution (float resolution)
{
  resolution_ = resolution;
  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> float
pcl::SupervoxelClustering<PointT>::getSeedResolution () const
{
  return (resolution_);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::setSeedResolution (float seed_resolution)
{
  seed_resolution_ = seed_resolution;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::setColorImportance (float val)
{
  color_importance_ = val;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::setSpatialImportance (float val)
{
  spatial_importance_ = val;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::setNormalImportance (float val)
{
  normal_importance_ = val;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::setIgnoreInputNormals (bool val)
{
  ignore_input_normals_ = val;
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::initializeLabelColors ()
{
  uint32_t max_label = static_cast<uint32_t> (getMaxLabel ());
  //If we already have enough colors, return
  if (label_colors_.size () > max_label)
    return;
  
  //Otherwise, generate new colors until we have enough
  label_colors_.reserve (max_label + 1);
  srand (static_cast<unsigned int> (time (0)));
  while (label_colors_.size () <= max_label )
  {
    uint8_t r = static_cast<uint8_t>( (rand () % 256));
    uint8_t g = static_cast<uint8_t>( (rand () % 256));
    uint8_t b = static_cast<uint8_t>( (rand () % 256));
    label_colors_.push_back (static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(b));
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> int
pcl::SupervoxelClustering<PointT>::getMaxLabel () const
{
  int max_label = 0;
  for (typename HelperListT::const_iterator sv_itr = supervoxel_helpers_.cbegin (); sv_itr != supervoxel_helpers_.cend (); ++sv_itr)
  {
    int temp = sv_itr->getLabel ();
    if (temp > max_label)
      max_label = temp;
  }
  return max_label;
}

namespace pcl
{ 
  namespace octree
  {
    //Explicit overloads for RGB types
    template<>
    void
    pcl::octree::OctreePointCloudAdjacencyContainer<pcl::PointXYZRGB,pcl::SupervoxelClustering<pcl::PointXYZRGB>::VoxelData>::addPoint (const pcl::PointXYZRGB &new_point);
    
    template<>
    void
    pcl::octree::OctreePointCloudAdjacencyContainer<pcl::PointXYZRGBA,pcl::SupervoxelClustering<pcl::PointXYZRGBA>::VoxelData>::addPoint (const pcl::PointXYZRGBA &new_point);
    
    //Explicit overloads for RGB types
    template<> void
    pcl::octree::OctreePointCloudAdjacencyContainer<pcl::PointXYZRGB,pcl::SupervoxelClustering<pcl::PointXYZRGB>::VoxelData>::computeData ();
    
    template<> void
    pcl::octree::OctreePointCloudAdjacencyContainer<pcl::PointXYZRGBA,pcl::SupervoxelClustering<pcl::PointXYZRGBA>::VoxelData>::computeData ();
    
    //Explicit overloads for XYZ types
    template<>
    void
    pcl::octree::OctreePointCloudAdjacencyContainer<pcl::PointXYZ,pcl::SupervoxelClustering<pcl::PointXYZ>::VoxelData>::addPoint (const pcl::PointXYZ &new_point);
    
    template<> void
    pcl::octree::OctreePointCloudAdjacencyContainer<pcl::PointXYZ,pcl::SupervoxelClustering<pcl::PointXYZ>::VoxelData>::computeData ();
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//TODO Move specializations? Change default behavior of OctreeAdjacency?
/*
  pcl::SupervoxelClustering<pcl::PointXYZRGB>::VoxelData::getPoint (pcl::PointXYZRGB &point_arg) const;
  
  template<> void
  pcl::SupervoxelClustering<pcl::PointXYZRGBA>::VoxelData::getPoint (pcl::PointXYZRGBA &point_arg ) const;
template<typename PointT> void
pcl::octree::OctreePointCloudAdjacencyContainer<PointT,typename pcl::SupervoxelClustering<PointT>::VoxelData>::addPoint (const PointT &new_point)
{
}

template<typename PointT> void
pcl::octree::OctreePointCloudAdjacencyContainer<PointT,typename pcl::SupervoxelClustering<PointT>::VoxelData>::computeData ()
{
}
*/

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::SupervoxelHelper::addLeaf (LeafContainerT* leaf_arg)
{
  leaves_.insert (leaf_arg);
  VoxelData& voxel_data = leaf_arg->getData ();
  voxel_data.owner_ = this;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::SupervoxelHelper::removeLeaf (LeafContainerT* leaf_arg)
{
  leaves_.erase (leaf_arg);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::SupervoxelHelper::removeAllLeaves ()
{
  typename SupervoxelHelper::iterator leaf_itr;
  for (leaf_itr = leaves_.begin (); leaf_itr != leaves_.end (); ++leaf_itr)
  {
    VoxelData& voxel = ((*leaf_itr)->getData ());
    voxel.owner_ = 0;
    voxel.distance_ = std::numeric_limits<float>::max ();
  }
  leaves_.clear ();
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::SupervoxelHelper::expand ()
{
  //std::cout << "Expanding sv "<<label_<<", owns "<<leaves_.size ()<<" voxels\n";
  //Buffer of new neighbors - initial size is just a guess of most possible
  std::vector<LeafContainerT*> new_owned;
  new_owned.reserve (leaves_.size () * 9);
  //For each leaf belonging to this supervoxel
  typename SupervoxelHelper::iterator leaf_itr;
  for (leaf_itr = leaves_.begin (); leaf_itr != leaves_.end (); ++leaf_itr)
  {
    //for each neighbor of the leaf
    for (typename LeafContainerT::const_iterator neighb_itr=(*leaf_itr)->cbegin (); neighb_itr!=(*leaf_itr)->cend (); ++neighb_itr)
    {
      //Get a reference to the data contained in the leaf
      VoxelData& neighbor_voxel = ((*neighb_itr)->getData ());
      //TODO this is a shortcut, really we should always recompute distance
      if(neighbor_voxel.owner_ == this)
        continue;
      //Compute distance to the neighbor
      float dist = parent_->voxelDistance (centroid_, neighbor_voxel.voxel_centroid_);
      //If distance is less than previous, we remove it from its owner's list
      //and change the owner to this and distance (we *steal* it!)
      if (dist < neighbor_voxel.distance_)  
      {
        neighbor_voxel.distance_ = dist;
        if (neighbor_voxel.owner_ != this)
        {
          if (neighbor_voxel.owner_)
            (neighbor_voxel.owner_)->removeLeaf(*neighb_itr);
          neighbor_voxel.owner_ = this;
          new_owned.push_back (*neighb_itr);
        }
      }
    }
  }
  //Push all new owned onto the owned leaf set
  typename std::vector<LeafContainerT*>::iterator new_owned_itr;
  for (new_owned_itr=new_owned.begin (); new_owned_itr!=new_owned.end (); ++new_owned_itr)
  {
    leaves_.insert (*new_owned_itr);
  }
  
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::SupervoxelHelper::refineNormals ()
{
  typename SupervoxelHelper::iterator leaf_itr;
  //For each leaf belonging to this supervoxel, get its neighbors, build an index vector, compute normal
  for (leaf_itr = leaves_.begin (); leaf_itr != leaves_.end (); ++leaf_itr)
  {
    VoxelData& voxel_data = (*leaf_itr)->getData ();
    std::vector<int> indices;
    indices.reserve (81); 
    //Push this point
    indices.push_back (voxel_data.idx_);
    for (typename LeafContainerT::const_iterator neighb_itr=(*leaf_itr)->cbegin (); neighb_itr!=(*leaf_itr)->cend (); ++neighb_itr)
    {
      //Get a reference to the data contained in the leaf
      VoxelData& neighbor_voxel_data = ((*neighb_itr)->getData ());
      //If the neighbor is in this supervoxel, use it
      if (neighbor_voxel_data.owner_ == this)
      {
        indices.push_back (neighbor_voxel_data.idx_);
        //Also check its neighbors
        for (typename LeafContainerT::const_iterator neighb_neighb_itr=(*neighb_itr)->cbegin (); neighb_neighb_itr!=(*neighb_itr)->cend (); ++neighb_neighb_itr)
        {
          VoxelData& neighb_neighb_voxel_data = (*neighb_neighb_itr)->getData ();
          if (neighb_neighb_voxel_data.owner_ == this)
            indices.push_back (neighb_neighb_voxel_data.idx_);
        }
        
        
      }
    }
    //recompute normal based on supervoxel if we have enough points
    if (indices.size () >= 4)
    {
      //Is this temp vector really the only way to do this? Why can't I substitute vectormap for vector?
      Eigen::Vector4f temp_vec;
      pcl::computePointNormal (*parent_->voxel_centroid_cloud_, indices, temp_vec, voxel_data.voxel_centroid_.curvature);
      voxel_data.voxel_centroid_.getNormalVector4fMap () = temp_vec;
      pcl::flipNormalTowardsViewpoint (voxel_data.voxel_centroid_, 0.0f,0.0f,0.0f, voxel_data.voxel_centroid_.normal_x,
                                                                                   voxel_data.voxel_centroid_.normal_y,
                                                                                   voxel_data.voxel_centroid_.normal_z);
      //Copy back out into voxel_centroid_cloud
      parent_->voxel_centroid_cloud_->points[voxel_data.idx_] = voxel_data.voxel_centroid_;
    }
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::SupervoxelHelper::updateCentroid ()
{
  CentroidPoint<CentroidT> centroid;
  typename SupervoxelHelper::iterator leaf_itr = leaves_.begin ();
  for ( ; leaf_itr!= leaves_.end (); ++leaf_itr)
  {
    const VoxelData& leaf_data = (*leaf_itr)->getData ();
    centroid.add (leaf_data.voxel_centroid_);
  }
  centroid.get (centroid_);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::SupervoxelHelper::getVoxels (typename pcl::PointCloud<VoxelT>::Ptr &voxels) const
{
  voxels.reset (new pcl::PointCloud<VoxelT>);
  voxels->clear ();
  voxels->resize (leaves_.size ());
  typename pcl::PointCloud<VoxelT>::iterator voxel_itr = voxels->begin ();
  typename SupervoxelHelper::const_iterator leaf_itr;
  for ( leaf_itr = leaves_.begin (); leaf_itr != leaves_.end (); ++leaf_itr, ++voxel_itr)
  {
    const VoxelData& leaf_data = (*leaf_itr)->getData ();
    leaf_data.getPoint (*voxel_itr);
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template <typename PointT> void
pcl::SupervoxelClustering<PointT>::SupervoxelHelper::getNeighborLabels (std::set<uint32_t> &neighbor_labels) const
{
  neighbor_labels.clear ();
  //For each leaf belonging to this supervoxel
  typename SupervoxelHelper::const_iterator leaf_itr;
  for (leaf_itr = leaves_.begin (); leaf_itr != leaves_.end (); ++leaf_itr)
  {
    //for each neighbor of the leaf
    for (typename LeafContainerT::const_iterator neighb_itr=(*leaf_itr)->cbegin (); neighb_itr!=(*leaf_itr)->cend (); ++neighb_itr)
    {
      //Get a reference to the data contained in the leaf
      VoxelData& neighbor_voxel = ((*neighb_itr)->getData ());
      //If it has an owner, and it's not us - get it's owner's label insert into set
      if (neighbor_voxel.owner_ != this && neighbor_voxel.owner_)
      {
        neighbor_labels.insert (neighbor_voxel.owner_->getLabel ());
      }
    }
  }
}


#endif    // PCL_SUPERVOXEL_CLUSTERING_HPP_
 
