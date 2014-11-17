 
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
 
 #ifndef PCL_SEGMENTATION_SEQUENTIAL_SUPERVOXEL_CLUSTERING_H_
 #define PCL_SEGMENTATION_SEQUENTIAL_SUPERVOXEL_CLUSTERING_H_
 
#include <pcl/segmentation/supervoxel_clustering.h>
#include <pcl/octree/octree_pointcloud_sequential.h>

 namespace pcl
 {
   /** \brief Supervoxel container class - stores a cluster extracted using supervoxel clustering 
    */
   class SequentialSV : public Supervoxel
   {
    public:
      typedef pcl::PointXYZRGBNormal CentroidT;
      typedef pcl::PointXYZRGBNormal VoxelT;
      typedef boost::shared_ptr<SequentialSV> Ptr;
      typedef boost::shared_ptr<const SequentialSV> ConstPtr;

      using Supervoxel::centroid_;
      using Supervoxel::label_;
      using Supervoxel::voxels_;

      SequentialSV (uint32_t label = 0) :
        Supervoxel (label)
      {  } 

      //! \brief Maps voxel index to measured weight - used by tracking
      std::map <size_t, float> voxel_weight_map_;

    public:
      EIGEN_MAKE_ALIGNED_OPERATOR_NEW
   };

  /** \brief NEW MESSAGE
  *  \author Jeremie Papon (jpapon@gmail.com)
  *  \ingroup segmentation
  */
  template <typename PointT>
  class PCL_EXPORTS SequentialSVClustering : public pcl::PCLBase<PointT>
  {
    protected:
      class SequentialSupervoxelHelper;
      friend class SequentialSupervoxelHelper;
    public:
      typedef typename SequentialSV::CentroidT CentroidT;
      typedef typename SequentialSV::VoxelT VoxelT;
      
      class SequentialVoxelData : public SupervoxelClustering<PointT>::VoxelData
      {
      public:
        
        SequentialVoxelData (float initial_distance = std::numeric_limits<float>::max ()):
          distance_ (initial_distance),
          idx_ (-1),
          new_leaf_ (true),
          has_changed_ (false),
          owner_ (0),
          frame_occluded_ (0)
        {
          voxel_centroid_.getVector4fMap ().setZero ();
          voxel_centroid_.getNormalVector4fMap ().setZero ();
          voxel_centroid_.getRGBAVector4i ().setZero ();
          voxel_centroid_.curvature = 0.0;
          previous_centroid_ = voxel_centroid_;
        }
        
        template<typename PointOutT>
        void
        getPoint (PointOutT &point_arg) const
        {
          copyPoint (voxel_centroid_, point_arg);
        }
        
        bool 
        isNew () const { return new_leaf_; }
        
        void
        setNew (bool new_arg) { new_leaf_ = new_arg; }
        
        bool 
        isChanged () const { return has_changed_; }
        
        void 
        setChanged (bool new_val) { has_changed_ = new_val; }
        
        void
        prepareForNewFrame (const int &points_last_frame)
        {
          new_leaf_ = false;
          has_changed_ = false;
          previous_centroid_ = voxel_centroid_;
          point_accumulator_ = CentroidPoint<PointT> ();
          owner_ = 0;
        }
        
        void
        revertToLastPoint ()
        {
          voxel_centroid_ = previous_centroid_;
        }
        
        void initLastPoint ()
        {
          previous_centroid_ = voxel_centroid_;
        }
        
        VoxelT voxel_centroid_;
        CentroidPoint<PointT> point_accumulator_;
        float distance_;
        int idx_;
    
        CentroidT previous_centroid_;
        SequentialSupervoxelHelper* owner_;
        bool has_changed_, new_leaf_;
        int frame_occluded_;
        
      public:
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW
      };
      
      typedef pcl::octree::OctreePointCloudSequentialContainer<PointT, SequentialVoxelData> LeafContainerT;
      typedef std::vector <LeafContainerT*> LeafVectorT;
      typedef std::map<uint32_t,typename Supervoxel::Ptr> SupervoxelMapT;
      typedef std::map<uint32_t,typename SequentialSV::Ptr> SequentialSVMapT;

      typedef typename pcl::PointCloud<PointT> PointCloudT;
      typedef typename pcl::PointCloud<VoxelT> VoxelCloudT;
      typedef typename pcl::octree::OctreePointCloudSequential<PointT, LeafContainerT> OctreeSequentialT;
      typedef typename pcl::search::KdTree<PointT> KdTreeT;

    protected:
      typedef typename SupervoxelClustering<PointT>::SeedNHood SeedNHood;
      
      using PCLBase <PointT>::initCompute;
      using PCLBase <PointT>::deinitCompute;
      using PCLBase <PointT>::input_;
      bool use_single_camera_transform_;
      float seed_prune_radius_;
      
      /** \brief Transform function used to normalize voxel density versus distance from camera */
      void
      transformFunction (PointT &p);
      
      /** \brief Transform function used to normalize voxel density versus distance from camera */
      void
      transformFunctionVoxel (VoxelT &p);
      /** \brief This selects points to use as initial supervoxel centroids
       *  \param[out] seed_indices The selected leaf indices
       */
      void
      selectInitialSupervoxelSeeds (std::vector<size_t> &seed_indices);
      /** \brief This method initializes the label_colors_ vector (assigns random colors to labels)
       * \note Checks to see if it is already big enough - if so, does not reinitialize it
       */
      void
      initializeLabelColors ();
    public:
      /** \brief Set the resolution of the octree voxels */
      void
      setVoxelResolution (float resolution);
      
      /** \brief Get the resolution of the octree voxels */
      float 
      getVoxelResolution () const;
      
      /** \brief Set the resolution of the octree seed voxels */
      void
      setSeedResolution (float seed_resolution);
      
      /** \brief Get the resolution of the octree seed voxels */
      float 
      getSeedResolution () const;
      
      /** \brief Set the importance of color for supervoxels */
      void
      setColorImportance (float val);
      
      /** \brief Set the importance of spatial distance for supervoxels */
      void
      setSpatialImportance (float val);
      
      /** \brief Set the importance of scalar normal product for supervoxels */
      void
      setNormalImportance (float val);
      
      void
      setSeedPruneRadius (float radius)
      {
        seed_prune_radius_ = radius;
      }
      /** \brief Set to ignore input normals and calculate normals internally 
       *          \note Default is False - ie, SupervoxelClustering will use normals provided in PointT if there are any
       *          \note You should only need to set this if eg PointT=PointXYZRGBNormal but you don't want to use the normals it contains
       */
      void
      setIgnoreInputNormals (bool val);
      
      /** \brief This method sets the cloud to be supervoxelized
       * \param[in] cloud The cloud to be supervoxelize
       */
      virtual void
      setInputCloud (const typename pcl::PointCloud<PointT>::ConstPtr& cloud);
      
      /** \brief Returns a deep copy of the voxel centroid cloud */
      template<typename PointOutT>
      typename pcl::PointCloud<PointOutT>::Ptr
      getVoxelCentroidCloud () const
      {
        typename pcl::PointCloud<PointOutT>::Ptr centroid_copy (new pcl::PointCloud<PointOutT>);
        copyPointCloud (*voxel_centroid_cloud_, *centroid_copy);
        return centroid_copy;
      }

      typedef boost::adjacency_list<boost::setS, boost::setS, boost::undirectedS, uint32_t, float> VoxelAdjacencyList;
      typedef VoxelAdjacencyList::vertex_descriptor VoxelID;
      typedef VoxelAdjacencyList::edge_descriptor EdgeID;

    public:
      /** \brief Constructor that sets default values for member variables. 
        *  \param[in] voxel_resolution The resolution (in meters) of voxels used
        *  \param[in] seed_resolution The average size (in meters) of resulting supervoxels
        *  \param[in] use_single_camera_transform Set to true if point density in cloud falls off with distance from origin (such as with a cloud coming from one stationary camera), set false if input cloud is from multiple captures from multiple locations.
        */
      SequentialSVClustering (float voxel_resolution, float seed_resolution, bool use_single_camera_transform = true, bool prune_close_seeds=true);
      
      /** \brief This destructor destroys the cloud, normals and search method used for
        * finding neighbors. In other words it frees memory.
        */
      virtual
      ~SequentialSVClustering ();
      
      /** \brief This method launches the segmentation algorithm and returns the supervoxels that were
       * obtained during the segmentation.
       * \param[out] supervoxel_clusters A map of labels to pointers to supervoxel structures
       */
      virtual void
      extract (std::map<uint32_t,typename SequentialSV::Ptr > &supervoxel_clusters);
      
      /** \brief Returns the current maximum (highest) label */
      int
      getMaxLabel () const;
      
      void
      setMinWeight (float min_weight)
      {
        min_weight_ = min_weight;
      }
      
      void 
      setUseOcclusionTesting (bool use_occlusion_testing)
      {
        use_occlusion_testing_ = use_occlusion_testing;
      }
      
      void 
      setFullExpandLeaves (bool do_full_expansion)
      {
        do_full_expansion_ = do_full_expansion;
      }
      void
      buildVoxelCloud ();
      
      /** \brief This function builds new supervoxels which are conditioned on the voxel_weight_maps contained in supervoxel_clusters 
        */
      void
      extractNewConditionedSupervoxels (SequentialSVMapT &supervoxel_clusters, bool add_new_seeds);
      
      pcl::PointCloud<pcl::PointXYZL>::Ptr
      getLabeledCloud () const;
      
      pcl::PointCloud<pcl::PointXYZL>::Ptr
      getLabeledVoxelCloud () const;
      
      pcl::PointCloud<pcl::PointXYZRGBA>::Ptr
      getColoredVoxelCloud () const;
      
      
      pcl::PointCloud<pcl::PointXYZRGBA>::Ptr
      getColoredCloud () const;
      
      void 
      getSupervoxelAdjacency (std::multimap<uint32_t, uint32_t> &label_adjacency) const;
    protected:
      bool
      prepareForSegmentation ();
      
      void
      computeVoxelData ();
      
      void
      createHelpersFromWeightMaps (SequentialSVMapT &supervoxel_clusters, std::vector<size_t> &existing_seed_indices);
      
      void
      clearOwnersSetCentroids ();
      
      void
      expandSupervoxelsFast ( int depth );
      
      int
      findNeighborMinCurvature (int idx);
      
      
      /** \brief This method appends internal supervoxel helpers to the list based on the provided seed points
       *  \param[in] seed_indices Indices of the leaves to use as seeds
       */
      void
      appendHelpersFromSeedIndices (std::vector<size_t> &seed_indices);
      
      /** \brief Constructs the map of supervoxel clusters from the internal supervoxel helpers */
      void
      makeSupervoxels (std::map<uint32_t,typename SequentialSV::Ptr > &supervoxel_clusters);

      /** \brief This selects new leaves to use as supervoxel seeds
       *  \param[out] seed_indices The selected leaf indices
       */
      void
      selectNewSupervoxelSeeds (std::vector<size_t> &existing_seed_indices, std::vector<size_t> &seed_indices);
      
      void
      createHelpersFromSeedIndices (std::vector<size_t> &seed_indices);
      
      /** \brief Distance function used for comparing voxelDatas */
      float
      voxelDistance (const VoxelT &v1, const VoxelT &v2) const;
      
      /** \brief Stores the resolution used in the octree */
      float resolution_;
      
      /** \brief Stores the resolution used to seed the superpixels */
      float seed_resolution_;

      /** \brief Contains a KDtree for the voxelized cloud */
      typename pcl::search::KdTree<VoxelT>::Ptr voxel_kdtree_;

      /** \brief Stores the colors used for the superpixel labels*/
      std::vector<uint32_t> label_colors_;
      
      /** \brief Octree Sequential structure with leaves at voxel resolution */
      typename OctreeSequentialT::Ptr sequential_octree_;
      
      /** \brief Contains the Voxelized centroid Cloud */
      typename VoxelCloudT::Ptr voxel_centroid_cloud_;

      /** \brief Importance of color in clustering */
      float color_importance_;
      /** \brief Importance of distance from seed center in clustering */
      float spatial_importance_;
      /** \brief Importance of similarity in normals for clustering */
      float normal_importance_;
      /** \brief Option to ignore normals in input Pointcloud. Defaults to false */
      bool ignore_input_normals_; 

      bool prune_close_seeds_;
      
      StopWatch timer_;
      
      float min_weight_;
      bool do_full_expansion_;
      bool use_occlusion_testing_;
      
      /** \brief Internal storage class for supervoxels 
       * \note Stores pointers to leaves of clustering internal octree, 
       * \note so should not be used outside of clustering class 
       */
      class SequentialSupervoxelHelper
      {
      public:
        
        /** \brief Comparator for LeafContainerT pointers - used for sorting set of leaves
         * \note Compares by index in the overall leaf_vector. Order isn't important, so long as it is fixed.
         */
        struct compareLeaves
        {
          bool operator() (LeafContainerT* const &left, LeafContainerT* const &right) const
          {
            return left->getData ().idx_ < right->getData ().idx_;
          }
        };
        typedef std::set<LeafContainerT*, typename SequentialSupervoxelHelper::compareLeaves> LeafSetT;
        typedef typename LeafSetT::iterator iterator;
        typedef typename LeafSetT::const_iterator const_iterator;
        
        SequentialSupervoxelHelper (uint32_t label, SequentialSVClustering* parent_arg):
        label_ (label),
        parent_ (parent_arg)
        { }
        
        void
        addLeaf (LeafContainerT* leaf_arg);
        
        void
        removeLeaf (LeafContainerT* leaf_arg);
        
        void
        removeAllLeaves ();
        
        void 
        expand ();
        
        void 
        updateCentroid ();
        
        void 
        getVoxels (typename pcl::PointCloud<VoxelT>::Ptr &voxels) const;
        
        typedef float (SequentialSVClustering::*DistFuncPtr)(const SequentialVoxelData &v1, const SequentialVoxelData &v2);
        
        uint32_t
        getLabel () const 
        { return label_; }
        
        void
        getNeighborLabels (std::set<uint32_t> &neighbor_labels) const;
        
        void
        getCentroid (CentroidT &centroid_arg) const
        { 
          centroid_arg = centroid_; 
        }
        
        CentroidT
        getCentroid () const
        { 
          return centroid_;
        }
        
        size_t
        size () const { return leaves_.size (); }
      private:
        //Stores leaves
        LeafSetT leaves_;
        uint32_t label_;
        CentroidT centroid_;
        SequentialSVClustering* parent_;
      public:
        //Type VoxelData may have fixed-size Eigen objects inside
        EIGEN_MAKE_ALIGNED_OPERATOR_NEW
      };
      
      //Make boost::ptr_list can access the private class SupervoxelHelper
      friend void boost::checked_delete<> (const typename pcl::SequentialSVClustering<PointT>::SequentialSupervoxelHelper *);
      
      typedef boost::ptr_list<SequentialSupervoxelHelper> HelperListT;
      HelperListT supervoxel_helpers_;
    public:
      EIGEN_MAKE_ALIGNED_OPERATOR_NEW
  };
  
 }

 #ifdef PCL_NO_PRECOMPILE
 #include <pcl/segmentation/impl/sequential_supervoxel_clustering.hpp>
 #endif

 #endif //PCL_SEGMENTATION_SEQUENTIAL_SUPERVOXEL_CLUSTERING_H_
