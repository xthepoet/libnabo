/*

Copyright (c) 2010--2011, Stephane Magnenat, ASL, ETHZ, Switzerland
You can contact the author at <stephane at magnenat dot net>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL ETH-ASL BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef __NABO_PRIVATE_H
#define __NABO_PRIVATE_H

#include "nabo.h"

#ifdef BOOST_STDINT
	#include <boost/cstdint.hpp>
	using boost::uint32_t;
#else // BOOST_STDINT
	#include <stdint.h>
#endif // BOOST_STDINT

// OpenCL
#ifdef HAVE_OPENCL
	#define __CL_ENABLE_EXCEPTIONS
	#include "CL/cl.hpp"
#endif // HAVE_OPENCL

// Unused macro, add support for your favorite compiler
#if defined(__GNUC__)
	#define _UNUSED __attribute__ ((unused))
#else
	#define _UNUSED
#endif

/*!	\file nabo_private.h
	\brief header for implementation
	\ingroup private
*/

namespace Nabo
{
	//! \defgroup private private implementation
	//@{
	
	//! Euclidean distance
	template<typename T, typename A, typename B>
	inline T dist2(const A& v0, const B& v1)
	{
		return (v0 - v1).squaredNorm();
	}

	//! L1norm
	template<typename T, typename A, typename B>
	inline T dist1(const A& v0, const B& v1)
	{
	  //return (v0 - v1).abs().sum();
	  T dist(0);
	  for (int j=0; j<v0.cols(); ++j) {
	    for (int i=0; i<v0.rows(); ++i) {
	      T diff = v0(i,j) - v1(i,j);
	      if (diff > 0)
		dist += diff;
	      else
		dist -= diff;
	    }
	  }
	  return dist;   
	}

	//! Brute-force nearest neighbour
	template<typename T>
	struct BruteForceSearch: public NearestNeighbourSearch<T>
	{
		typedef typename NearestNeighbourSearch<T>::Vector Vector;
		typedef typename NearestNeighbourSearch<T>::Matrix Matrix;
		typedef typename NearestNeighbourSearch<T>::Index Index;
		typedef typename NearestNeighbourSearch<T>::IndexVector IndexVector;
		typedef typename NearestNeighbourSearch<T>::IndexMatrix IndexMatrix;
		
		using NearestNeighbourSearch<T>::dim;
		using NearestNeighbourSearch<T>::creationOptionFlags;
		using NearestNeighbourSearch<T>::checkSizesKnn;
		using NearestNeighbourSearch<T>::minBound;
		using NearestNeighbourSearch<T>::maxBound;

		//! constructor, calls NearestNeighbourSearch<T>(cloud)
		BruteForceSearch(const Matrix& cloud, const Index dim, const unsigned creationOptionFlags);
		virtual unsigned long knn(const Matrix& query, IndexMatrix& indices, Matrix& dists2, const Index k, const T epsilon, const unsigned optionFlags, const T maxRadius) const;
		virtual unsigned long knn(const Matrix& query, IndexMatrix& indices, Matrix& dists2, const Vector& maxRadii, const Index k = 1, const T epsilon = 0, const unsigned optionFlags = 0) const;
	};
	
	//! KDTree, unbalanced, points in leaves, stack, implicit bounds, ANN_KD_SL_MIDPT, optimised implementation
	template<typename T, typename Heap>
	struct KDTreeUnbalancedPtInLeavesImplicitBoundsStackOpt: public NearestNeighbourSearch<T>
	{
		typedef typename NearestNeighbourSearch<T>::Vector Vector;
		typedef typename NearestNeighbourSearch<T>::Matrix Matrix;
		typedef typename NearestNeighbourSearch<T>::Index Index;
		typedef typename NearestNeighbourSearch<T>::IndexVector IndexVector;
		typedef typename NearestNeighbourSearch<T>::IndexMatrix IndexMatrix;
		
		using NearestNeighbourSearch<T>::dim;
		using NearestNeighbourSearch<T>::cloud;
		using NearestNeighbourSearch<T>::creationOptionFlags;
		using NearestNeighbourSearch<T>::minBound;
		using NearestNeighbourSearch<T>::maxBound;
		using NearestNeighbourSearch<T>::checkSizesKnn;
		
	protected:
		//! indices of points during kd-tree construction
		typedef std::vector<Index> BuildPoints;
		//! iterator to indices of points during kd-tree construction
		typedef typename BuildPoints::iterator BuildPointsIt;
		//! const-iterator to indices of points during kd-tree construction
		typedef typename BuildPoints::const_iterator BuildPointsCstIt;
		
		//! size of bucket
		const unsigned bucketSize;
		
		//! number of bits required to store dimension index + number of dimensions
		const uint32_t dimBitCount;
		//! mask to access dim
		const uint32_t dimMask;
		
		//! create the compound index containing the dimension and the index of the child or the bucket size
		inline uint32_t createDimChildBucketSize(const uint32_t dim, const uint32_t childIndex) const
		{ return dim | (childIndex << dimBitCount); }
		//! get the dimension out of the compound index
		inline uint32_t getDim(const uint32_t dimChildBucketSize) const
		{ return dimChildBucketSize & dimMask; }
		//! get the child index or the bucket size out of the coumpount index
		inline uint32_t getChildBucketSize(const uint32_t dimChildBucketSize) const
		{ return dimChildBucketSize >> dimBitCount; }
		
		struct BucketEntry;
		
		//! search node
		struct Node
		{
			uint32_t dimChildBucketSize; //!< cut dimension for split nodes (dimBitCount lsb), index of right node or number of bucket(rest). Note that left index is current+1
			union
			{
				T cutVal; //!< for split node, split value
				uint32_t bucketIndex; //!< for leaf node, pointer to bucket
			};
			
			//! construct a split node
			Node(const uint32_t dimChild, const T cutVal):
				dimChildBucketSize(dimChild), cutVal(cutVal) {}
			//! construct a leaf node
			Node(const uint32_t bucketSize, const uint32_t bucketIndex):
				dimChildBucketSize(bucketSize), bucketIndex(bucketIndex) {}
		};
		//! dense vector of search nodes, provides better memory performances than many small objects
		typedef std::vector<Node> Nodes;
		
		//! entry in a bucket
		struct BucketEntry
		{
			const T* pt; //!< pointer to first value of point data, 0 if end of bucket
			Index index; //!< index of point
			
			//! create a new bucket entry for a point in the data
			/** \param pt pointer to first component of the point, components must be continuous
			 * \param index index of the point in the data
			 */
			BucketEntry(const T* pt = 0, const Index index = 0): pt(pt), index(index) {}
		};
		
		//! bucket data
		typedef std::vector<BucketEntry> Buckets;
		
		//! search nodes
		Nodes nodes;
		
		//! buckets
		Buckets buckets;
		
		//! return the bounds of points from [first..last[ on dimension dim
		std::pair<T,T> getBounds(const BuildPointsIt first, const BuildPointsIt last, const unsigned dim);
		//! construct nodes for points [first..last[ inside the hyperrectangle [minValues..maxValues]
		unsigned buildNodes(const BuildPointsIt first, const BuildPointsIt last, const Vector minValues, const Vector maxValues);
		
		//! search one point, call recurseKnn with the correct template parameters
		/** \param query pointer to query coordinates 
		 *	\param indices indices of nearest neighbours, must be of size k x query.cols()
		 *	\param dists2 squared distances to nearest neighbours, must be of size k x query.cols() 
		 *	\param i index of point to search
		 * 	\param heap reference to heap
		 * 	\param off reference to array of offsets
		 *	\param maxError error factor (1 + epsilon) 
		 *	\param maxRadius2 square of maximum radius
		 *	\param allowSelfMatch whether to allow self match
		 *	\param collectStatistics whether to collect statistics
		 *	\param sortResults wether to sort results
		 */
		unsigned long onePointKnn(const Matrix& query, IndexMatrix& indices, Matrix& dists2, int i, Heap& heap, std::vector<T>& off, const T maxError, const T maxRadius2, const bool allowSelfMatch, const bool collectStatistics, const bool sortResults) const;
		
		//! recursive search, strongly inspired by ANN and [Arya & Mount, Algorithms for fast vector quantization, 1993]
		/**	\param query pointer to query coordinates 
		 * 	\param n index of node to visit
		 * 	\param rd squared dist to this rect
		 * 	\param heap reference to heap
		 * 	\param off reference to array of offsets
		 * 	\param maxError error factor (1 + epsilon) 
		 *	\param maxRadius2 square of maximum radius
		 */
		template<bool allowSelfMatch, bool collectStatistics>
		unsigned long recurseKnn(const T* query, const unsigned n, T rd, Heap& heap, std::vector<T>& off, const T maxError, const T maxRadius2) const;
		
	public:
		//! constructor, calls NearestNeighbourSearch<T>(cloud)
		KDTreeUnbalancedPtInLeavesImplicitBoundsStackOpt(const Matrix& cloud, const Index dim, const unsigned creationOptionFlags, const Parameters& additionalParameters);
		virtual unsigned long knn(const Matrix& query, IndexMatrix& indices, Matrix& dists2, const Index k, const T epsilon, const unsigned optionFlags, const T maxRadius) const;
		virtual unsigned long knn(const Matrix& query, IndexMatrix& indices, Matrix& dists2, const Vector& maxRadii, const Index k = 1, const T epsilon = 0, const unsigned optionFlags = 0) const;
	};
	
	#ifdef HAVE_OPENCL
	
	//! OpenCL support for nearest neighbour search	
	template<typename T>
	struct OpenCLSearch: public NearestNeighbourSearch<T>
	{
		typedef typename NearestNeighbourSearch<T>::Vector Vector;
		typedef typename NearestNeighbourSearch<T>::Matrix Matrix;
		typedef typename NearestNeighbourSearch<T>::Index Index;
		typedef typename NearestNeighbourSearch<T>::IndexVector IndexVector;
		typedef typename NearestNeighbourSearch<T>::IndexMatrix IndexMatrix;
		
		using NearestNeighbourSearch<T>::dim;
		using NearestNeighbourSearch<T>::cloud;
		using NearestNeighbourSearch<T>::creationOptionFlags;
		using NearestNeighbourSearch<T>::checkSizesKnn;
		
	protected:
		const cl_device_type deviceType; //!< the type of device to run CL code on (CL_DEVICE_TYPE_CPU or CL_DEVICE_TYPE_GPU)
		cl::Context& context; //!< the CL context
		mutable cl::Kernel knnKernel; //!< the kernel to perform knnSearch, mutable because it is stateful, but conceptually const
		cl::CommandQueue queue; //!< the command queue
		cl::Buffer cloudCL; //!< the buffer for the input data
		
		//! constructor, calls NearestNeighbourSearch<T>(cloud)
		OpenCLSearch(const Matrix& cloud, const Index dim, const unsigned creationOptionFlags, const cl_device_type deviceType);
		//! Initialize CL support
		/** \param clFileName name of file containing CL code
		 * \param kernelName name of the CL kernel function
		 * \param additionalDefines additional CL code to pass to compiler
		 */
		void initOpenCL(const char* clFileName, const char* kernelName, const std::string& additionalDefines = "");
	
	public:
		virtual unsigned long knn(const Matrix& query, IndexMatrix& indices, Matrix& dists2, const Index k, const T epsilon, const unsigned optionFlags, const T maxRadius) const;
	};
	
	//! KDTree, balanced, points in leaves, stack, implicit bounds, balance aspect ratio
	template<typename T>
	struct BruteForceSearchOpenCL: public OpenCLSearch<T>
	{
		typedef typename NearestNeighbourSearch<T>::Vector Vector;
		typedef typename NearestNeighbourSearch<T>::Matrix Matrix;
		typedef typename NearestNeighbourSearch<T>::Index Index;
		
		using OpenCLSearch<T>::initOpenCL;
		
		//! constructor, calls OpenCLSearch<T>(cloud, ...)
		BruteForceSearchOpenCL(const Matrix& cloud, const Index dim, const unsigned creationOptionFlags, const cl_device_type deviceType);
	};
	
	//! KDTree, balanced, points in leaves, stack, implicit bounds, balance aspect ratio
	template<typename T>
	struct KDTreeBalancedPtInLeavesStackOpenCL: public OpenCLSearch<T>
	{
		typedef typename NearestNeighbourSearch<T>::Vector Vector;
		typedef typename NearestNeighbourSearch<T>::Matrix Matrix;
		typedef typename NearestNeighbourSearch<T>::Index Index;
		typedef typename NearestNeighbourSearch<T>::IndexVector IndexVector;
		typedef typename NearestNeighbourSearch<T>::IndexMatrix IndexMatrix;
		
		using NearestNeighbourSearch<T>::dim;
		using NearestNeighbourSearch<T>::cloud;
		using NearestNeighbourSearch<T>::creationOptionFlags;
		using NearestNeighbourSearch<T>::minBound;
		using NearestNeighbourSearch<T>::maxBound;
		
		using OpenCLSearch<T>::context;
		using OpenCLSearch<T>::knnKernel;
		
		using OpenCLSearch<T>::initOpenCL;
		
	protected:
		//! Point during kd-tree construction
		struct BuildPoint
		{
			Vector pos; //!< point
			size_t index; //!< index of point in cloud
			//! Construct a build point, at a given pos with a specific index
			BuildPoint(const Vector& pos =  Vector(), const size_t index = 0): pos(pos), index(index) {}
		};
		//! points during kd-tree construction
		typedef std::vector<BuildPoint> BuildPoints;
		//! iterator to points during kd-tree construction
		typedef typename BuildPoints::iterator BuildPointsIt;
		//! const-iterator to points during kd-tree construction
		typedef typename BuildPoints::const_iterator BuildPointsCstIt;
		
		//! Functor to compare point values on a given dimension
		struct CompareDim
		{
			size_t dim; //!< dimension on which to compare
			//! Build the functor for a specific dimension
			CompareDim(const size_t dim):dim(dim){}
			//! Compare the values of p0 and p1 on dim, and return whether p0[dim] < p1[dim]
			bool operator() (const BuildPoint& p0, const BuildPoint& p1) { return p0.pos(dim) < p1.pos(dim); }
		};
		
		//! Tree node for CL
		struct Node
		{
			int dim; //!< dimension of the cut, or, if negative, index of the point: -1 == invalid, <= -2 = index of pt
			T cutVal; //!< value of the cut
			//! Build a tree node, with a given dimension and value to cut on, or, if leaf and dim <= -2, the index of the point as (-dim-2)
			Node(const int dim = -1, const T cutVal = 0):dim(dim), cutVal(cutVal) {}
		};
		//! dense vector of search nodes
		typedef std::vector<Node> Nodes;
		
		Nodes nodes; //!< search nodes
		cl::Buffer nodesCL; //!< CL buffer for search nodes
		
		
		inline size_t childLeft(size_t pos) const { return 2*pos + 1; } //!< Return the left child of pos
		inline size_t childRight(size_t pos) const { return 2*pos + 2; } //!< Return the right child of pos
		inline size_t parent(size_t pos) const { return (pos-1)/2; } //!< Return the parent of pos
		size_t getTreeDepth(size_t size) const; //!< Return the max depth of a tree of a given size
		size_t getTreeSize(size_t size) const; //!< Return the storage size of tree of a given size
		
		//! Recurse to build nodes
		void buildNodes(const BuildPointsIt first, const BuildPointsIt last, const size_t pos, const Vector minValues, const Vector maxValues);
		
	public:
		//! constructor, calls OpenCLSearch<T>(cloud, ...)
		KDTreeBalancedPtInLeavesStackOpenCL(const Matrix& cloud, const Index dim, const unsigned creationOptionFlags, const cl_device_type deviceType);
	};
	
	//! KDTree, balanced, points in nodes, stack, implicit bounds, balance aspect ratio
	template<typename T>
	struct KDTreeBalancedPtInNodesStackOpenCL: public OpenCLSearch<T>
	{
		typedef typename NearestNeighbourSearch<T>::Vector Vector;
		typedef typename NearestNeighbourSearch<T>::Matrix Matrix;
		typedef typename NearestNeighbourSearch<T>::Index Index;
		typedef typename NearestNeighbourSearch<T>::IndexVector IndexVector;
		typedef typename NearestNeighbourSearch<T>::IndexMatrix IndexMatrix;
		
		using NearestNeighbourSearch<T>::dim;
		using NearestNeighbourSearch<T>::cloud;
		using NearestNeighbourSearch<T>::creationOptionFlags;
		using NearestNeighbourSearch<T>::minBound;
		using NearestNeighbourSearch<T>::maxBound;
		
		using OpenCLSearch<T>::context;
		using OpenCLSearch<T>::knnKernel;
		
		using OpenCLSearch<T>::initOpenCL;
		
	protected:
		//! a point during kd-tree construction is just its index
		typedef Index BuildPoint;
		//! points during kd-tree construction
		typedef std::vector<BuildPoint> BuildPoints;
		//! iterator to points during kd-tree construction
		typedef typename BuildPoints::iterator BuildPointsIt;
		//! const-iterator to points during kd-tree construction
		typedef typename BuildPoints::const_iterator BuildPointsCstIt;
		
		//! Functor to compare point values on a given dimension
		struct CompareDim
		{
			const Matrix& cloud; //!< reference to data points used to compare
			size_t dim; //!< dimension on which to compare
			//! Build the functor for a specific dimension on a specific cloud
			CompareDim(const Matrix& cloud, const size_t dim): cloud(cloud), dim(dim){}
			//! Compare the values of p0 and p1 on dim, and return whether p0[dim] < p1[dim]
			bool operator() (const BuildPoint& p0, const BuildPoint& p1) { return cloud.coeff(dim, p0) < cloud.coeff(dim, p1); }
		};
		
		//! Tree node for CL
		struct Node
		{
			int dim; //!< dimension of the cut, or, if -1 == leaf, -2 == invalid
			Index index; //!< index of the point to cut
			//! Build a tree node, with a given index and a given dimension to cut on
			Node(const int dim = -2, const Index index = 0):dim(dim), index(index) {}
		};
		//! dense vector of search nodes
		typedef std::vector<Node> Nodes;
		
		Nodes nodes; //!< search nodes
		cl::Buffer nodesCL;  //!< CL buffer for search nodes
		
		
		inline size_t childLeft(size_t pos) const { return 2*pos + 1; } //!< Return the left child of pos
		inline size_t childRight(size_t pos) const { return 2*pos + 2; } //!< Return the right child of pos
		inline size_t parent(size_t pos) const { return (pos-1)/2; } //!< Return the parent of pos
		size_t getTreeDepth(size_t size) const; //!< Return the max depth of a tree of a given size
		size_t getTreeSize(size_t size) const; //!< Return the storage size of tree of a given size
		
		//! Recurse to build nodes
		void buildNodes(const BuildPointsIt first, const BuildPointsIt last, const size_t pos, const Vector minValues, const Vector maxValues);
		
	public:
		//! constructor, calls OpenCLSearch<T>(cloud, ...)
		KDTreeBalancedPtInNodesStackOpenCL(const Matrix& cloud, const Index dim, const unsigned creationOptionFlags, const cl_device_type deviceType);
	};
	
	#endif // HAVE_OPENCL
	
	//@}
}

#endif // __NABO_H
