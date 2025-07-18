// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkParallelAMRUtilities.h"
#include "vtkAMRBox.h"
#include "vtkCompositeDataIterator.h"
#include "vtkMultiProcessController.h"
#include "vtkOverlappingAMR.h"
#include "vtkOverlappingAMRMetaData.h"
#include "vtkSmartPointer.h"
#include "vtkUniformGrid.h"
#include <cassert>
#include <cmath>
#include <limits>

//------------------------------------------------------------------------------
VTK_ABI_NAMESPACE_BEGIN
void vtkParallelAMRUtilities::PrintSelf(std::ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void vtkParallelAMRUtilities::DistributeProcessInformation(
  vtkOverlappingAMR* amr, vtkMultiProcessController* controller, std::vector<int>& processMap)
{
  processMap.resize(amr->GetNumberOfBlocks(), -1);
  vtkSmartPointer<vtkCompositeDataIterator> iter;
  iter.TakeReference(amr->NewIterator());
  iter->SkipEmptyNodesOn();

  if (!controller || controller->GetNumberOfProcesses() == 1)
  {
    for (iter->GoToFirstItem(); !iter->IsDoneWithTraversal(); iter->GoToNextItem())
    {
      unsigned int index = iter->GetCurrentFlatIndex();
      processMap[index] = 0;
    }
    return;
  }
  int myRank = controller->GetLocalProcessId();
  int numProcs = controller->GetNumberOfProcesses();

  // get the active process ids
  std::vector<int> myBlocks;
  for (iter->GoToFirstItem(); !iter->IsDoneWithTraversal(); iter->GoToNextItem())
  {
    myBlocks.push_back(iter->GetCurrentFlatIndex());
  }

  vtkIdType myNumBlocks = static_cast<vtkIdType>(myBlocks.size());
  std::vector<vtkIdType> numBlocks(numProcs, 0);
  numBlocks[myRank] = myNumBlocks;

  // gather the active process counts
  controller->AllGather(&myNumBlocks, numBlocks.data(), 1);

  // gather the blocks each process owns into one array
  std::vector<vtkIdType> offsets(numProcs, 0);
  vtkIdType currentOffset(0);
  for (int i = 0; i < numProcs; i++)
  {
    offsets[i] = currentOffset;
    currentOffset += numBlocks[i];
  }
  std::vector<int> allBlocks(currentOffset, -1);
  controller->AllGatherV(myBlocks.data(), allBlocks.data(), (vtkIdType)myBlocks.size(),
    numBlocks.data(), offsets.data());

#ifdef DEBUG
  if (myRank == 0)
  {
    for (int i = 0; i < numProcs; i++)
    {
      vtkIdType offset = offsets[i];
      int n = numBlocks[i];
      cout << "Rank " << i << " has: ";
      for (vtkIdType j = offset; j < offset + n; j++)
      {
        cout << allBlocks[j] << " ";
      }
      cout << endl;
    }
  }
#endif
  for (int rank = 0; rank < numProcs; rank++)
  {
    int offset = offsets[rank];
    int n = numBlocks[rank];
    for (int j = offset; j < offset + n; j++)
    {
      int index = allBlocks[j];
      assert(index >= 0);
      processMap[index] = rank;
    }
  }
}

//------------------------------------------------------------------------------
void vtkParallelAMRUtilities::StripGhostLayers(vtkOverlappingAMR* ghostedAMRData,
  vtkOverlappingAMR* strippedAMRData, vtkMultiProcessController* controller)
{
  vtkAMRUtilities::StripGhostLayers(ghostedAMRData, strippedAMRData);

  if (controller != nullptr)
  {
    controller->Barrier();
  }
}

//------------------------------------------------------------------------------
void vtkParallelAMRUtilities::BlankCells(
  vtkOverlappingAMR* amr, vtkMultiProcessController* myController)
{
  vtkOverlappingAMRMetaData* amrMData = amr->GetOverlappingAMRMetaData();
  if (!amrMData)
  {
    return;
  }

  if (!amrMData->HasRefinementRatio())
  {
    amrMData->GenerateRefinementRatio();
  }
  if (!amrMData->HasChildrenInformation())
  {
    amrMData->GenerateParentChildInformation();
  }

  std::vector<int> processorMap;
  vtkParallelAMRUtilities::DistributeProcessInformation(amr, myController, processorMap);
  unsigned int numLevels = amr->GetNumberOfLevels();
  for (unsigned int i = 0; i < numLevels; i++)
  {
    vtkAMRUtilities::BlankGridsAtLevel(amr, i, amrMData->GetChildrenAtLevel(i), processorMap);
  }
}
VTK_ABI_NAMESPACE_END
