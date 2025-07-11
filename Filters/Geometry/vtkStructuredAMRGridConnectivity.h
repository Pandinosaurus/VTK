// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkStructuredAMRGridConnectivity
 *  grid connectivity.
 *
 *
 *  A concrete instance of vtkAbstractGridConnectivity that implements
 *  functionality for computing the neighboring topology within a structured
 *  AMR grid, as well as, generating ghost-layers. Support is provided for
 *  1-D, 2-D (XY,XZ,YZ) and 3-D cell-centered datasets. This implementation
 *  does not have any support for distributed data. For the parallel
 *  implementation see vtkPStructuredAMRGridConnectivity.
 *
 * @sa
 *  vtkPStructuredAMRGridConnectivity vtkAbstractGridConnectivity
 */

#ifndef vtkStructuredAMRGridConnectivity_h
#define vtkStructuredAMRGridConnectivity_h

#include "vtkAbstractGridConnectivity.h"
#include "vtkFiltersGeometryModule.h" // For export macro

#include "vtkStructuredAMRNeighbor.h" // For vtkStructuredAMRNeighbor def.

// C++ includes
#include <map>     // For STL map
#include <ostream> // For STL stream
#include <set>     // For STL set
#include <vector>  // For STL vector

VTK_ABI_NAMESPACE_BEGIN
class VTKFILTERSGEOMETRY_EXPORT vtkStructuredAMRGridConnectivity
  : public vtkAbstractGridConnectivity
{
public:
  static vtkStructuredAMRGridConnectivity* New();
  vtkTypeMacro(vtkStructuredAMRGridConnectivity, vtkAbstractGridConnectivity);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Initializes this instance of vtkStructuredAMRGridConnectivity where N
   * is the total number of grids in the AMR hierarchy. Optionally, if the
   * AMR dataset has a constant refinement, it should be specified during
   * initialization as the code optimizes for it. If a -1 or no refinement
   * ratio is specified a varying refinement ratio is assumed.
   */
  void Initialize(unsigned int NumberOfLevels, unsigned int N, int RefinementRatio = -1);

  /**
   * Computes neighboring information.
   */
  void ComputeNeighbors() override;

  /**
   * Creates ghost layers.
   */
  void CreateGhostLayers(int N = 1) override;

  /**
   * Registers the AMR grid with the given global linear grid ID (starting
   * numbering from 0) and level and refinement ratio. This method is to be
   * used when the refinement ratio is not constant.
   */
  virtual void RegisterGrid(int gridIdx, int level, int refinementRatio, int extents[6],
    vtkUnsignedCharArray* nodesGhostArray, vtkUnsignedCharArray* cellGhostArray,
    vtkPointData* pointData, vtkCellData* cellData, vtkPoints* gridNodes);

  /**
   * Registers the AMR grid with the given global linear grid ID (starting
   * numbering from 0) and level. The extents of the grid are expected to be
   * global node extents.
   */
  virtual void RegisterGrid(int gridIdx, int level, int extents[6],
    vtkUnsignedCharArray* nodesGhostArray, vtkUnsignedCharArray* cellGhostArray,
    vtkPointData* pointData, vtkCellData* cellData, vtkPoints* gridNodes);

  ///@{
  /**
   * Get/Set macro for BalancedRefinement property, default is true. If the
   * refinement is balanced, then, adjacent grids in the AMR hierarchy can
   * only differ by one level. By default, a balanced refinement is assumed.
   */
  vtkSetMacro(BalancedRefinement, bool);
  vtkGetMacro(BalancedRefinement, bool);
  ///@}

  ///@{
  /**
   * Get/Set macro NodeCentered property which indicates if the data is
   * node-centered or cell-centered. By default, node-centered is set to false
   * since AMR datasets are primarily cell-centered.
   */
  vtkSetMacro(NodeCentered, bool);
  vtkGetMacro(NodeCentered, bool);
  ///@}

  ///@{
  /**
   * Get/Set CellCentered property which indicates if the data is cell-centered
   * By default, cell-centered is set to true.
   */
  vtkSetMacro(CellCentered, bool);
  vtkGetMacro(CellCentered, bool);
  ///@}

  /**
   * Returns the number of neighbors for the grid corresponding to the given
   * grid ID.
   */
  int GetNumberOfNeighbors(int gridID);

  /**
   * Returns the ghost extend for the grid corresponding to the given grid ID.
   */
  void GetGhostedExtent(int gridID, int ext[6]);

  /**
   * Returns the AMR neighbor for the patch with the corresponding grid ID.
   */
  vtkStructuredAMRNeighbor GetNeighbor(int gridID, int nei);

protected:
  vtkStructuredAMRGridConnectivity();
  ~vtkStructuredAMRGridConnectivity() override;

  /**
   * Sets the total number of grids(blocks) in the AMR hierarchy
   */
  void SetNumberOfGrids(unsigned int N) override;

  /**
   * Creates the ghosted mask arrays
   */
  void CreateGhostedMaskArrays(int gridID);

  /**
   * Creates the ghosted extent of the given grid
   */
  void CreateGhostedExtent(int gridID, int N);

  /**
   * Sets the ghost extent for the grid corresponding to the given grid ID.
   */
  void SetGhostedExtent(int gridID, int ext[6]);

  /**
   * Gets the coarsened extent for the grid with the given grid index.
   */
  void GetCoarsenedExtent(int gridIdx, int fromLevel, int toLevel, int ext[6]);

  /**
   * Gets the refined extent for the grid with the given grid index.
   */
  void GetRefinedExtent(int gridIdx, int fromLevel, int toLevel, int ext[6]);

  /**
   * Refines the given extent.
   */
  void RefineExtent(int orient[3], int ndim, int fromLevel, int toLevel, int ext[6]);

  /**
   * Given the global i,j,k index of a cell at a coarse level, fromLevel, this
   * method computes the range of cells on the refined grid.
   */
  void GetCellRefinedExtent(
    int orient[3], int ndim, int i, int j, int k, int fromLevel, int toLevel, int ext[6]);

  /**
   * Coarsens the given extent.
   */
  void CoarsenExtent(int orient[3], int ndim, int fromLevel, int toLevel, int ext[6]);

  /**
   * Gets the grid extent for the grid with the given grid ID.
   */
  void GetGridExtent(int gridIdx, int ext[6]);

  /**
   * Returns the level of the grid with the corresponding grid ID.
   */
  int GetGridLevel(int gridIdx);

  /**
   * Checks if the given level has been registered
   */
  bool LevelExists(int level);

  /**
   * Checks if the node is an interior node in the given extent.
   */
  bool IsNodeInterior(int i, int j, int k, int GridExtent[6]);

  /**
   * Checks if the node is within the extent.
   */
  bool IsNodeWithinExtent(int i, int j, int k, int GridExtent[6]);

  /**
   * Checks if the node is on a shared boundary.
   */
  bool IsNodeOnSharedBoundary(int i, int j, int k, int gridId, int gridExt[6]);

  /**
   * Checks if the node is on the boundary of the given extent.
   */
  bool IsNodeOnBoundaryOfExtent(int i, int j, int k, int ext[6]);

  /**
   * Inserts the grid corresponding to the given ID at the prescribed level.
   */
  void InsertGridAtLevel(int level, int gridID);

  /**
   * Loops through the neighbors of this grid and computes the send and rcv
   * extents for the N requested ghost layers.
   */
  void ComputeNeighborSendAndRcvExtent(int gridID, int N);

  /**
   * Computes the whole extent w.r.t. level 0 as well as the AMR dataset
   * description and dimension.
   */
  void ComputeWholeExtent();

  /**
   * Gets the whole extent with respect to the given level.
   * NOTE: This method assument that the whole extent has been computed.
   */
  void GetWholeExtentAtLevel(int level, int ext[6]);

  /**
   * Establishes neighboring relationship between grids i,j wheren i,j are
   * global indices.
   */
  void EstablishNeighbors(int i, int j);

  /**
   * Computes the node orientation tuple for the given i,j,k node.
   */
  void GetNodeOrientation(int i, int j, int k, int gridExt[6], int nodeOrientation[3]);

  /**
   * Establishes the orientation vector and dimension based on the computed
   * data description. The orientation vector is a 3-tuple, which encodes the
   * dimensions that are used. For example, let's say that we want to define
   * the orientation to be in the XZ plane, then, the orient array would be
   * constructed as follows: {0,2 -1}, where -1 indicates a NIL value.
   */
  void GetOrientationVector(int dataDescription, int orient[3], int& ndim);

  /**
   * Checks if a constant refinement ratio has been specified.
   */
  bool HasConstantRefinementRatio();

  /**
   * Sets the refinement ratio at the given level.
   */
  void SetRefinementRatioAtLevel(int level, int r);

  /**
   * Returns the refinement ratio at the given level.
   */
  int GetRefinementRatioAtLevel(int level);

  /**
   * Checks if the extent ext1 and ext2 are equal.
   */
  bool AreExtentsEqual(int ext1[6], int ext2[6]);

  /**
   * Constructs the block topology for the given grid.
   */
  void SetBlockTopology(int gridID);

  /**
   * Returns the number of faces of the block corresponding to the given grid
   * ID that are adjacent to at least one other block. Note, this is not the
   * total number of neighbors for the block. This method simply checks how
   * many out of the 6 block faces have connections. Thus, the return value
   * has an upper-bound of 6.
   */
  int GetNumberOfConnectingBlockFaces(int gridID);

  ///@{
  /**
   * Checks if the block corresponding to the given grid ID has a block
   * adjacent to it in the given block direction.
   * NOTE: The block direction is essentially one of the 6 faces of the
   * block defined as follows:
   * <ul>
   * <li> FRONT  = 0 (+k direction) </li>
   * <li> BACK   = 1 (-k direction) </li>
   * <li> RIGHT  = 2 (+i direction) </li>
   * <li> LEFT   = 3 (-i direction) </li>
   * <li> TOP    = 4 (+j direction) </li>
   * <li> BOTTOM = 5 (-j direction) </li>
   * </ul>
   */
  bool HasBlockConnection(int gridID, int blockDirection)
  {
    // Sanity check
    assert("pre: gridID is out-of-bounds" && (gridID >= 0) &&
      (gridID < static_cast<int>(this->NumberOfGrids)));
    assert("pre: BlockTopology has not been properly allocated" &&
      (this->NumberOfGrids == this->BlockTopology.size()));
    assert("pre: blockDirection is out-of-bounds" && (blockDirection >= 0) && (blockDirection < 6));
    bool status = false;
    if (this->BlockTopology[gridID] & (1 << blockDirection))
    {
      status = true;
    }
    return (status);
  }
  ///@}

  /**
   * Removes a block connection along the given direction for the block
   * corresponding to the given gridID.
   * NOTE: The block direction is essentially one of the 6 faces of the
   * block defined as follows:
   * <ul>
   * <li> FRONT  = 0 (+k direction) </li>
   * <li> BACK   = 1 (-k direction) </li>
   * <li> RIGHT  = 2 (+i direction) </li>
   * <li> LEFT   = 3 (-i direction) </li>
   * <li> TOP    = 4 (+j direction) </li>
   * <li> BOTTOM = 5 (-j direction) </li>
   * </ul>
   */
  void RemoveBlockConnection(int gridID, int blockDirection);

  /**
   * Adds a block connection along the given direction for the block
   * corresponding to the given gridID.
   * NOTE: The block direction is essentially one of the 6 faces of the
   * block defined as follows:
   * <ul>
   * <li> FRONT  = 0 (+k direction) </li>
   * <li> BACK   = 1 (-k direction) </li>
   * <li> RIGHT  = 2 (+i direction) </li>
   * <li> LEFT   = 3 (-i direction) </li>
   * <li> TOP    = 4 (+j direction) </li>
   * <li> BOTTOM = 5 (-j direction) </li>
   * </ul>
   */
  void AddBlockConnection(int gridID, int blockDirection);

  /**
   * Clears all block connections for the block corresponding to the given
   * grid ID.
   */
  void ClearBlockConnections(int gridID);

  /**
   * Marks the ghost property for the given node.
   */
  virtual void MarkNodeProperty(
    int gridId, int i, int j, int k, int gridExt[6], int wholeExt[6], unsigned char& p);

  /**
   * Fills the node ghost arrays for the given grid
   */
  virtual void FillNodesGhostArray(int gridId, vtkUnsignedCharArray* nodesArray);

  /**
   * Fills the cell ghost arrays for the given grid
   */
  virtual void FillCellsGhostArray(int gridId, vtkUnsignedCharArray* cellsArray);

  /**
   * Fills ghost arrays.
   */
  void FillGhostArrays(
    int gridId, vtkUnsignedCharArray* nodesArray, vtkUnsignedCharArray* cellsArray) override;

  /**
   * Compute the AMR neighbor of grid "i" and its neighbor grid "j".

   * Given the structured neighbors computed in normalized space (i.e., at
   * the same level) between the two grids, this method computes the
   * corresponding AMR neighbor which essentially adds other bits of
   * information, such as level, relationship type, etc.

   * NOTE:
   * The extents next1 and next2 for each grid are the normalized extents
   */
  vtkStructuredAMRNeighbor GetAMRNeighbor(int i, int iLevel, int next1[6], int j, int jLevel,
    int next2[6], int normalizedLevel, int levelDiff, vtkStructuredNeighbor& nei);

  /**
   * A Helper method to compute the AMR neighbor overlap extents. The method
   * coarsens/refines the gridOverlap and neiOverlap extents accordingly s.t.
   * they are w.r.t. to the level of the grid they refer to.
   */
  void ComputeAMRNeighborOverlapExtents(int iLevel, int jLevel, int normalizedLevel,
    const vtkStructuredNeighbor& nei, int orient[3], int ndim, int gridOverlapExtent[6],
    int neiOverlapExtent[6]);

  /**
   * Get 1-D orientation.
   */
  int Get1DOrientation(int idx, int ExtentLo, int ExtentHi, int OnLo, int OnHi, int NotOnBoundary);

  /**
   * Prints the extent
   */
  void PrintExtent(std::ostream& os, int ext[6]);

  /**
   * Initializes the ghost data-structures
   */
  void InitializeGhostData(int gridID);

  /**
   * Transfers the data of the registered grid, to the ghosted data-structures.
   */
  void TransferRegisteredDataToGhostedData(int gridID);

  /**
   * Transfers local node-centered neighbor data
   */
  void TransferLocalNodeCenteredNeighborData(int gridID, vtkStructuredAMRNeighbor& nei);

  /**
   * Copy cell center value from a coarser level by direct-injection, i.e., the
   * values within the coarse cell is assumed to be constant.
   */
  void GetLocalCellCentersFromCoarserLevel(int gridID, vtkStructuredAMRNeighbor& nei);

  /**
   * Copy cell center values from a finer level by cell averaging.
   */
  void GetLocalCellCentersFromFinerLevel(int gridID, vtkStructuredAMRNeighbor& nei);

  /**
   * Copy cell center values to fill in the ghost levels from a neighbor at
   * the same level as the grid corresponding to the given grid ID.
   */
  void GetLocalCellCentersAtSameLevel(int gridID, vtkStructuredAMRNeighbor& nei);

  /**
   * Transfers local cell-centered neighbor data
   */
  void TransferLocalCellCenteredNeighborData(int gridID, vtkStructuredAMRNeighbor& nei);

  /**
   * Transfers local neighbor data
   */
  void TransferLocalNeighborData(int gridID, vtkStructuredAMRNeighbor& nei);

  /**
   * Fills in the ghost data from the neighbors
   */
  virtual void TransferGhostDataFromNeighbors(int gridID);

  /**
   * Loops through all arrays and computes the average of the supplied source
   * indices and stores the corresponding average
   */
  void AverageFieldData(
    vtkFieldData* source, vtkIdType* sourceIds, int N, vtkFieldData* target, vtkIdType targetIdx);

  /**
   * Loops through all arrays in the source and for each array, it copies the
   * tuples from sourceIdx to the target at targetIdx. This method assumes
   * that the source and target have a one-to-one array correspondence, that
   * is array i in the source corresponds to array i in the target.
   */
  void CopyFieldData(
    vtkFieldData* source, vtkIdType sourceIdx, vtkFieldData* target, vtkIdType targetIdx);

  unsigned int NumberOfLevels; // The total number of levels;
  int DataDimension;           // The dimension of the data, i.e. 2 or 3
  int DataDescription;         // The data description, i.e., VTK_STRUCTURED_XY_PLANE, etc.
  int WholeExtent[6];          // The whole extent w.r.t. to the root level, level 0.
  int MaxLevel;                // The max level of the AMR hierarchy
  int RefinementRatio;         // The refinement ratio, set in the initialization,iff,
                               // a constant refinement ratio is used. A value of -1
                               // indicates that the refinement ratio is not constant
                               // and the RefinementRatios vector is used instead.

  bool NodeCentered; // Indicates if the data is node-centered
  bool CellCentered; // Indicates if the data is cell-centered

  bool BalancedRefinement; // If Balanced refinement is true, then adjacent
                           // grids in the hierarchy can only differ by one
                           // level.

  // AMRHierarchy stores the set of grid Ids in [0,N] for each level
  std::map<int, std::set<int>> AMRHierarchy;

  // For each grid, [0,N] store the grid extents,level, and list of neighbors
  std::vector<int> GridExtents;             // size of this vector is 6*N
  std::vector<int> GhostedExtents;          // size of this vector is 6*N
  std::vector<unsigned char> BlockTopology; // size of this vector is N
  std::vector<int> GridLevels;              // size of this vector is N
  std::vector<std::vector<vtkStructuredAMRNeighbor>> Neighbors;

  // For each grid, [0,N], store the donor level,grid and cell information, a
  // DonorLevel of -1 indicates that the cell is not receiving any information
  // from a donor.
  std::vector<std::vector<int>> CellCenteredDonorLevel;

  // RefinementRatios stores the refinement ratio at each level, this vector
  // is used only when the refinement ratio varies across levels
  std::vector<int> RefinementRatios;

private:
  vtkStructuredAMRGridConnectivity(const vtkStructuredAMRGridConnectivity&) = delete;
  void operator=(const vtkStructuredAMRGridConnectivity&) = delete;
};

//=============================================================================
//  INLINE METHODS
//=============================================================================

//------------------------------------------------------------------------------
inline int vtkStructuredAMRGridConnectivity::GetNumberOfNeighbors(int gridID)
{
  assert("pre: grid ID is out-of-bounds" && (gridID >= 0) &&
    (gridID < static_cast<int>(this->NumberOfGrids)));
  assert("pre: neighbors vector has not been properly allocated" &&
    (this->Neighbors.size() == this->NumberOfGrids));
  return (static_cast<int>(this->Neighbors[gridID].size()));
}

//------------------------------------------------------------------------------
inline vtkStructuredAMRNeighbor vtkStructuredAMRGridConnectivity::GetNeighbor(int gridID, int nei)
{
  assert("pre: grid ID is out-of-bounds" && (gridID >= 0) &&
    (gridID < static_cast<int>(this->NumberOfGrids)));
  assert("pre: neighbors vector has not been properly allocated" &&
    (this->Neighbors.size() == this->NumberOfGrids));
  assert("pre: nei index is out-of-bounds" && (nei >= 0) &&
    (nei < static_cast<int>(this->Neighbors[gridID].size())));
  return (this->Neighbors[gridID][nei]);
}

//------------------------------------------------------------------------------
inline int vtkStructuredAMRGridConnectivity::Get1DOrientation(
  int idx, int ExtentLo, int ExtentHi, int OnLo, int OnHi, int NotOnBoundary)
{
  if (idx == ExtentLo)
  {
    return OnLo;
  }
  else if (idx == ExtentHi)
  {
    return OnHi;
  }
  return NotOnBoundary;
}

//------------------------------------------------------------------------------
inline int vtkStructuredAMRGridConnectivity::GetNumberOfConnectingBlockFaces(int gridID)
{
  // Sanity check
  assert("pre: gridID is out-of-bounds" && (gridID >= 0) &&
    (gridID < static_cast<int>(this->NumberOfGrids)));
  assert("pre: BlockTopology has not been properly allocated" &&
    (this->NumberOfGrids == this->BlockTopology.size()));

  int count = 0;
  for (int i = 0; i < 6; ++i)
  {
    if (this->HasBlockConnection(gridID, i))
    {
      ++count;
    }
  }
  assert("post: count must be in [0,5]" && (count >= 0 && count <= 6));
  return (count);
}

//------------------------------------------------------------------------------
inline void vtkStructuredAMRGridConnectivity::RemoveBlockConnection(int gridID, int blockDirection)
{
  // Sanity check
  assert("pre: gridID is out-of-bounds" && (gridID >= 0) &&
    (gridID < static_cast<int>(this->NumberOfGrids)));
  assert("pre: BlockTopology has not been properly allocated" &&
    (this->NumberOfGrids == this->BlockTopology.size()));
  assert("pre: blockDirection is out-of-bounds" && (blockDirection >= 0) && (blockDirection < 6));

  this->BlockTopology[gridID] &= ~(1 << blockDirection);
}

//------------------------------------------------------------------------------
inline void vtkStructuredAMRGridConnectivity::AddBlockConnection(int gridID, int blockDirection)
{
  // Sanity check
  assert("pre: gridID is out-of-bounds" && (gridID >= 0) &&
    (gridID < static_cast<int>(this->NumberOfGrids)));
  assert("pre: BlockTopology has not been properly allocated" &&
    (this->NumberOfGrids == this->BlockTopology.size()));
  assert("pre: blockDirection is out-of-bounds" && (blockDirection >= 0) && (blockDirection < 6));
  this->BlockTopology[gridID] |= (1 << blockDirection);
}

//------------------------------------------------------------------------------
inline void vtkStructuredAMRGridConnectivity::ClearBlockConnections(int gridID)
{
  // Sanity check
  assert("pre: gridID is out-of-bounds" && (gridID >= 0) &&
    (gridID < static_cast<int>(this->NumberOfGrids)));
  assert("pre: BlockTopology has not been properly allocated" &&
    (this->NumberOfGrids == this->BlockTopology.size()));
  for (int i = 0; i < 6; ++i)
  {
    this->RemoveBlockConnection(gridID, i);
  } // END for all block directions
}

//------------------------------------------------------------------------------
inline bool vtkStructuredAMRGridConnectivity::AreExtentsEqual(int ext1[6], int ext2[6])
{
  for (int i = 0; i < 6; ++i)
  {
    if (ext1[i] != ext2[i])
    {
      return false;
    }
  } // END for
  return true;
}

//------------------------------------------------------------------------------
inline void vtkStructuredAMRGridConnectivity::PrintExtent(std::ostream& os, int ext[6])
{
  for (int i = 0; i < 6; i += 2)
  {
    os << "[";
    os << ext[i] << " ";
    os << ext[i + 1] << "] ";
  } // END for
}

//------------------------------------------------------------------------------
inline int vtkStructuredAMRGridConnectivity::GetGridLevel(int gridIdx)
{
  assert("pre: grid Index is out-of-bounds!" && (gridIdx < static_cast<int>(this->NumberOfGrids)));
  assert("pre: grid levels vector has not been allocated" &&
    (this->GridLevels.size() == this->NumberOfGrids));
  return (this->GridLevels[gridIdx]);
}

//------------------------------------------------------------------------------
inline void vtkStructuredAMRGridConnectivity::SetRefinementRatioAtLevel(int level, int r)
{
  assert("pre: RefinementRatios vector is not properly allocated" &&
    this->RefinementRatios.size() == this->NumberOfLevels);
  assert("pre: level is out-of-bounds!" && (level >= 0) &&
    (level < static_cast<int>(this->RefinementRatios.size())));
  assert("pre: invalid refinement ratio" && (r >= 2));

  this->RefinementRatios[level] = r;
}

//------------------------------------------------------------------------------
inline int vtkStructuredAMRGridConnectivity::GetRefinementRatioAtLevel(int level)
{
  assert("pre: RefinementRatios vector is not properly allocated" &&
    this->RefinementRatios.size() == this->NumberOfLevels);
  assert("pre: level is out-of-bounds!" && (level >= 0) &&
    (level < static_cast<int>(this->RefinementRatios.size())));
  assert(
    "pre: refinement ratio for level has not been set" && (this->RefinementRatios[level] >= 2));

  return (this->RefinementRatios[level]);
}

//------------------------------------------------------------------------------
inline bool vtkStructuredAMRGridConnectivity::HasConstantRefinementRatio()
{
  if (this->RefinementRatio < 2)
  {
    return false;
  }
  return true;
}

//------------------------------------------------------------------------------
inline void vtkStructuredAMRGridConnectivity::GetGridExtent(int gridIdx, int ext[6])
{
  assert("pre: grid index is out-of-bounds" &&
    ((gridIdx >= 0) && (gridIdx < static_cast<int>(this->GridExtents.size()))));

  for (int i = 0; i < 6; ++i)
  {
    ext[i] = this->GridExtents[gridIdx * 6 + i];
  }
}

//------------------------------------------------------------------------------
inline bool vtkStructuredAMRGridConnectivity::LevelExists(int level)
{
  if (this->AMRHierarchy.find(level) != this->AMRHierarchy.end())
  {
    return true;
  }
  return false;
}

//------------------------------------------------------------------------------
inline void vtkStructuredAMRGridConnectivity::InsertGridAtLevel(int level, int gridID)
{
  if (this->LevelExists(level))
  {
    this->AMRHierarchy[level].insert(gridID);
  }
  else
  {
    std::set<int> grids;
    grids.insert(gridID);
    this->AMRHierarchy[level] = grids;
  }
}

VTK_ABI_NAMESPACE_END
#endif /* VTKSTRUCTUREDAMRGRIDCONNECTIVITY_H_ */
