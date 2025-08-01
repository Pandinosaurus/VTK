// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkAMRBaseReader.h"
#include "vtkAMRDataSetCache.h"
#include "vtkCallbackCommand.h"
#include "vtkCellData.h"
#include "vtkCompositeDataPipeline.h"
#include "vtkDataArray.h"
#include "vtkDataArraySelection.h"
#include "vtkDataObject.h"
#include "vtkIndent.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMultiProcessController.h"
#include "vtkOverlappingAMR.h"
#include "vtkOverlappingAMRMetaData.h"
#include "vtkParallelAMRUtilities.h"
#include "vtkPointData.h"
#include "vtkSmartPointer.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkTimerLog.h"
#include "vtkUniformGrid.h"

#include <cassert>

VTK_ABI_NAMESPACE_BEGIN
vtkCxxSetObjectMacro(vtkAMRBaseReader, Controller, vtkMultiProcessController);

vtkAMRBaseReader::vtkAMRBaseReader()
{
  this->LoadedMetaData = false;
  this->NumBlocksFromCache = 0;
  this->NumBlocksFromFile = 0;
  this->EnableCaching = 0;
  this->Cache = nullptr;
  this->FileName = nullptr;
  this->Controller = nullptr;
}

//------------------------------------------------------------------------------
vtkAMRBaseReader::~vtkAMRBaseReader()
{
  this->PointDataArraySelection->RemoveObserver(this->SelectionObserver);
  this->CellDataArraySelection->RemoveObserver(this->SelectionObserver);
  this->SelectionObserver->Delete();
  this->CellDataArraySelection->Delete();
  this->PointDataArraySelection->Delete();

  if (this->Cache != nullptr)
  {
    this->Cache->Delete();
  }

  if (this->Metadata != nullptr)
  {
    this->Metadata->Delete();
  }

  delete[] this->FileName;
  this->FileName = nullptr;

  this->SetController(nullptr);
}

//------------------------------------------------------------------------------
int vtkAMRBaseReader::FillOutputPortInformation(int vtkNotUsed(port), vtkInformation* info)
{
  info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkOverlappingAMR");
  return 1;
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::PrintSelf(std::ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::Initialize()
{
  vtkTimerLog::MarkStartEvent("vtkAMRBaseReader::Initialize");

  this->SetNumberOfInputPorts(0);
  this->FileName = nullptr;
  this->MaxLevel = 0;
  this->Metadata = nullptr;
  this->SetController(vtkMultiProcessController::GetGlobalController());
  this->InitialRequest = true;
  this->Cache = vtkAMRDataSetCache::New();

  this->CellDataArraySelection = vtkDataArraySelection::New();
  this->PointDataArraySelection = vtkDataArraySelection::New();
  this->SelectionObserver = vtkCallbackCommand::New();
  this->SelectionObserver->SetCallback(&vtkAMRBaseReader::SelectionModifiedCallback);
  this->SelectionObserver->SetClientData(this);
  this->CellDataArraySelection->AddObserver(vtkCommand::ModifiedEvent, this->SelectionObserver);
  this->PointDataArraySelection->AddObserver(vtkCommand::ModifiedEvent, this->SelectionObserver);

  vtkTimerLog::MarkEndEvent("vtkAMRBaseReader::Initialize");
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::SelectionModifiedCallback(vtkObject*, unsigned long, void* clientdata, void*)
{
  static_cast<vtkAMRBaseReader*>(clientdata)->Modified();
}

//------------------------------------------------------------------------------
int vtkAMRBaseReader::GetNumberOfPointArrays()
{
  return (this->PointDataArraySelection->GetNumberOfArrays());
}

//------------------------------------------------------------------------------
int vtkAMRBaseReader::GetNumberOfCellArrays()
{
  return (this->CellDataArraySelection->GetNumberOfArrays());
}

//------------------------------------------------------------------------------
const char* vtkAMRBaseReader::GetPointArrayName(int index)
{
  return (this->PointDataArraySelection->GetArrayName(index));
}

//------------------------------------------------------------------------------
const char* vtkAMRBaseReader::GetCellArrayName(int index)
{
  return (this->CellDataArraySelection->GetArrayName(index));
}

//------------------------------------------------------------------------------
int vtkAMRBaseReader::GetPointArrayStatus(const char* name)
{
  return (this->PointDataArraySelection->ArrayIsEnabled(name));
}

//------------------------------------------------------------------------------
int vtkAMRBaseReader::GetCellArrayStatus(const char* name)
{
  return (this->CellDataArraySelection->ArrayIsEnabled(name));
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::SetPointArrayStatus(const char* name, int status)
{

  if (status)
  {
    this->PointDataArraySelection->EnableArray(name);
  }
  else
  {
    this->PointDataArraySelection->DisableArray(name);
  }
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::SetCellArrayStatus(const char* name, int status)
{
  if (status)
  {
    this->CellDataArraySelection->EnableArray(name);
  }
  else
  {
    this->CellDataArraySelection->DisableArray(name);
  }
}

//------------------------------------------------------------------------------
int vtkAMRBaseReader::GetBlockProcessId(int blockIdx)
{
  // If this is reader instance is serial, return Process 0
  // as the Process ID for the corresponding block.
  if (!this->IsParallel())
  {
    return 0;
  }

  int N = this->Controller->GetNumberOfProcesses();
  return (blockIdx % N);
}

//------------------------------------------------------------------------------
bool vtkAMRBaseReader::IsBlockMine(int blockIdx)
{
  // If this reader instance does not run in parallel, then,
  // all blocks are owned by this reader.
  if (!this->IsParallel())
  {
    return true;
  }

  int myRank = this->Controller->GetLocalProcessId();
  return myRank == this->GetBlockProcessId(blockIdx);
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::InitializeArraySelections()
{
  if (this->InitialRequest)
  {
    this->PointDataArraySelection->DisableAllArrays();
    this->CellDataArraySelection->DisableAllArrays();
    this->InitialRequest = false;
  }
}

//------------------------------------------------------------------------------
bool vtkAMRBaseReader::IsParallel()
{
  if (this->Controller == nullptr)
  {
    return false;
  }

  return this->Controller->GetNumberOfProcesses() > 1;
}

//------------------------------------------------------------------------------
int vtkAMRBaseReader::RequestInformation(
  vtkInformation* rqst, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  if (this->LoadedMetaData)
  {
    return (1);
  }

  this->Superclass::RequestInformation(rqst, inputVector, outputVector);

  if (this->Metadata == nullptr)
  {
    this->Metadata = vtkOverlappingAMR::New();
  }
  else
  {
    this->Metadata->Initialize();
  }
  this->FillMetaData();
  vtkInformation* info = outputVector->GetInformationObject(0);
  assert("pre: output information object is nullptr" && (info != nullptr));
  info->Set(vtkCompositeDataPipeline::COMPOSITE_DATA_META_DATA(), this->Metadata);
  if (this->Metadata && this->Metadata->GetInformation()->Has(vtkDataObject::DATA_TIME_STEP()))
  {
    double dataTime = this->Metadata->GetInformation()->Get(vtkDataObject::DATA_TIME_STEP());
    info->Set(vtkStreamingDemandDrivenPipeline::TIME_STEPS(), &dataTime, 1);

    vtkTimerLog::MarkStartEvent("vtkAMRBaseReader::GenerateParentChildInformation");
    this->Metadata->GenerateParentChildInformation();
    vtkTimerLog::MarkEndEvent("vtkAMRBaseReader::GenerateParentChildInformation");
  }

  info->Set(CAN_HANDLE_PIECE_REQUEST(), 1);
  this->LoadedMetaData = true;
  return 1;
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::SetupBlockRequest(vtkInformation* outInf)
{
  assert("pre: output information is nullptr" && (outInf != nullptr));

  if (outInf->Has(vtkCompositeDataPipeline::UPDATE_COMPOSITE_INDICES()))
  {
    assert("Metadata should not be null" && (this->Metadata != nullptr));
    this->ReadMetaData();

    int size = outInf->Length(vtkCompositeDataPipeline::UPDATE_COMPOSITE_INDICES());
    int* indices = outInf->Get(vtkCompositeDataPipeline::UPDATE_COMPOSITE_INDICES());

    this->BlockMap.clear();
    this->BlockMap.resize(size);

    for (int i = 0; i < size; ++i)
    {
      this->BlockMap[i] = indices[i];
    }
  }
  else
  {
    this->ReadMetaData();

    this->BlockMap.clear();
    int maxLevel = this->MaxLevel < static_cast<int>(this->Metadata->GetNumberOfLevels()) - 1
      ? this->MaxLevel
      : this->Metadata->GetNumberOfLevels() - 1;
    for (int level = 0; level <= maxLevel; level++)
    {
      for (unsigned int id = 0; id < this->Metadata->GetNumberOfBlocks(level); id++)
      {
        int index = this->Metadata->GetAbsoluteBlockIndex(static_cast<unsigned int>(level), id);
        this->BlockMap.push_back(index);
      }
    }
  }
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::GetAMRData(int blockIdx, vtkUniformGrid* block, const char* fieldName)
{
  assert("pre: AMR block is nullptr" && (block != nullptr));
  assert("pre: field name is nullptr" && (fieldName != nullptr));

  // If caching is disabled load the data from file
  if (!this->IsCachingEnabled())
  {
    vtkTimerLog::MarkStartEvent("GetAMRGridDataFromFile");
    this->GetAMRGridData(blockIdx, block, fieldName);
    vtkTimerLog::MarkEndEvent("GetAMRGridDataFromFile");
    return;
  }

  // Caching is enabled.
  // Check the cache to see if the data has already been read.
  // Otherwise, read it and cache it.
  if (this->Cache->HasAMRBlockCellData(blockIdx, fieldName))
  {
    vtkTimerLog::MarkStartEvent("GetAMRGridDataFromCache");
    vtkDataArray* data = this->Cache->GetAMRBlockCellData(blockIdx, fieldName);
    assert("pre: cached data is nullptr!" && (data != nullptr));
    vtkTimerLog::MarkEndEvent("GetAMRGridDataFromCache");

    block->GetCellData()->AddArray(data);
  }
  else
  {
    vtkTimerLog::MarkStartEvent("GetAMRGridDataFromFile");
    this->GetAMRGridData(blockIdx, block, fieldName);
    vtkTimerLog::MarkEndEvent("GetAMRGridDataFromFile");

    vtkTimerLog::MarkStartEvent("CacheAMRData");
    this->Cache->InsertAMRBlockCellData(blockIdx, block->GetCellData()->GetArray(fieldName));
    vtkTimerLog::MarkEndEvent("CacheAMRData");
  }
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::GetAMRPointData(int blockIdx, vtkUniformGrid* block, const char* fieldName)
{
  assert("pre: AMR block is nullptr" && (block != nullptr));
  assert("pre: field name is nullptr" && (fieldName != nullptr));

  // If caching is disabled load the data from file
  if (!this->IsCachingEnabled())
  {
    vtkTimerLog::MarkStartEvent("GetAMRGridPointDataFromFile");
    this->GetAMRGridPointData(blockIdx, block, fieldName);
    vtkTimerLog::MarkEndEvent("GetAMRGridPointDataFromFile");
    return;
  }

  // Caching is enabled.
  // Check the cache to see if the data has already been read.
  // Otherwise, read it and cache it.
  if (this->Cache->HasAMRBlockPointData(blockIdx, fieldName))
  {
    vtkTimerLog::MarkStartEvent("GetAMRGridPointDataFromCache");
    vtkDataArray* data = this->Cache->GetAMRBlockPointData(blockIdx, fieldName);
    assert("pre: cached data is nullptr!" && (data != nullptr));
    vtkTimerLog::MarkEndEvent("GetAMRGridPointDataFromCache");

    block->GetPointData()->AddArray(data);
  }
  else
  {
    vtkTimerLog::MarkStartEvent("GetAMRGridPointDataFromFile");
    this->GetAMRGridPointData(blockIdx, block, fieldName);
    vtkTimerLog::MarkEndEvent("GetAMRGridPointDataFromFile");

    vtkTimerLog::MarkStartEvent("CacheAMRPointData");
    this->Cache->InsertAMRBlockPointData(blockIdx, block->GetPointData()->GetArray(fieldName));
    vtkTimerLog::MarkEndEvent("CacheAMRPointData");
  }
}

//------------------------------------------------------------------------------
vtkUniformGrid* vtkAMRBaseReader::GetAMRBlock(int blockIdx)
{

  // If caching is disabled load the data from file
  if (!this->IsCachingEnabled())
  {
    ++this->NumBlocksFromFile;
    vtkTimerLog::MarkStartEvent("ReadAMRBlockFromFile");
    vtkUniformGrid* gridPtr = this->GetAMRGrid(blockIdx);
    vtkTimerLog::MarkEndEvent("ReadAMRBlockFromFile");
    assert("pre: grid pointer is nullptr" && (gridPtr != nullptr));
    return (gridPtr);
  }

  // Caching is enabled.
  // Check the cache to see if the block has already been read.
  // Otherwise, read it and cache it.
  if (this->Cache->HasAMRBlock(blockIdx))
  {
    ++this->NumBlocksFromCache;
    vtkTimerLog::MarkStartEvent("ReadAMRBlockFromCache");
    vtkUniformGrid* gridPtr = vtkUniformGrid::New();
    vtkUniformGrid* cachedGrid = this->Cache->GetAMRBlock(blockIdx);
    gridPtr->CopyStructure(cachedGrid);
    vtkTimerLog::MarkEndEvent("ReadAMRBlockFromCache");
    return (gridPtr);
  }
  else
  {
    ++this->NumBlocksFromFile;
    vtkTimerLog::MarkStartEvent("ReadAMRBlockFromFile");
    vtkUniformGrid* cachedGrid = vtkUniformGrid::New();
    vtkUniformGrid* gridPtr = this->GetAMRGrid(blockIdx);
    assert("pre: grid pointer is nullptr" && (gridPtr != nullptr));
    vtkTimerLog::MarkEndEvent("ReadAMRBlockFromFile");

    vtkTimerLog::MarkStartEvent("CacheAMRBlock");
    cachedGrid->CopyStructure(gridPtr);
    this->Cache->InsertAMRBlock(blockIdx, cachedGrid);
    vtkTimerLog::MarkEndEvent("CacheAMRBlock");

    return (gridPtr);
  }

  //  assert( "Code should never reach here!" && (false) );
  //  return nullptr;
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::LoadPointData(int blockIdx, vtkUniformGrid* block)
{
  // Sanity check!
  assert("pre: AMR block should not be nullptr" && (block != nullptr));

  for (int i = 0; i < this->GetNumberOfPointArrays(); ++i)
  {
    if (this->GetPointArrayStatus(this->GetPointArrayName(i)))
    {
      this->GetAMRPointData(blockIdx, block, this->GetPointArrayName(i));
    }
  } // END for all point arrays
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::LoadCellData(int blockIdx, vtkUniformGrid* block)
{
  // Sanity check!
  assert("pre: AMR block should not be nullptr" && (block != nullptr));

  for (int i = 0; i < this->GetNumberOfCellArrays(); ++i)
  {
    if (this->GetCellArrayStatus(this->GetCellArrayName(i)))
    {
      this->GetAMRData(blockIdx, block, this->GetCellArrayName(i));
    }
  } // END for all cell arrays
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::LoadRequestedBlocks(vtkOverlappingAMR* output)
{
  assert("pre: AMR data-structure is nullptr" && (output != nullptr));

  // Unlike AssignAndLoadBlocks, this code doesn't have to bother about
  // "distributing" blocks to load among processes when running in parallel.
  // Sinks should ensure that request appropriate blocks (similar to the way
  // requests for pieces or extents work).
  for (size_t block = 0; block < this->BlockMap.size(); ++block)
  {
    // FIXME: this piece of code is very similar to the block in
    // AssignAndLoadBlocks. We should consolidate the two.
    int blockIndex = this->BlockMap[block];
    int blockIdx = this->Metadata->GetOverlappingAMRMetaData()->GetAMRBlockSourceIndex(blockIndex);

    unsigned int metaLevel;
    unsigned int metaIdx;
    this->Metadata->ComputeIndexPair(blockIndex, metaLevel, metaIdx);
    unsigned int level = this->GetBlockLevel(blockIdx);
    assert(level == metaLevel);

    // STEP 0: Get the AMR block
    vtkTimerLog::MarkStartEvent("GetAMRBlock");
    vtkUniformGrid* amrBlock = this->GetAMRBlock(blockIdx);
    vtkTimerLog::MarkEndEvent("GetAMRBlock");
    assert("pre: AMR block is nullptr" && (amrBlock != nullptr));

    // STEP 2: Load any point-data
    vtkTimerLog::MarkStartEvent("vtkARMBaseReader::LoadPointData");
    this->LoadPointData(blockIdx, amrBlock);
    vtkTimerLog::MarkEndEvent("vtkAMRBaseReader::LoadPointData");

    // STEP 3: Load any cell data
    vtkTimerLog::MarkStartEvent("vtkAMRBaseReader::LoadCellData");
    this->LoadCellData(blockIdx, amrBlock);
    vtkTimerLog::MarkEndEvent("vtkAMRBaseReader::LoadCellData");

    // STEP 4: Add dataset
    output->SetDataSet(level, metaIdx, amrBlock);
    amrBlock->FastDelete();
  } // END for all blocks
}

//------------------------------------------------------------------------------
void vtkAMRBaseReader::AssignAndLoadBlocks(vtkOverlappingAMR* output)
{
  assert("pre: AMR data-structure is nullptr" && (output != nullptr));

  // Initialize counter of the number of blocks at each level.
  // This counter is used to compute the block index w.r.t. the
  // hierarchical box data-structure. Note that then number of blocks
  // can change based on user constraints, e.g., the number of levels
  // visible.
  std::vector<int> idxcounter;
  idxcounter.resize(this->GetNumberOfLevels() + 1, 0);

  // Find the number of blocks to be processed. BlockMap.size()
  // has all the blocks that are to be processesed and may be
  // less than or equal to this->GetNumberOfBlocks(), i.e., the
  // total number of blocks.
  int numBlocks = static_cast<int>(this->BlockMap.size());
  for (int block = 0; block < numBlocks; ++block)
  {
    int blockIndex = this->BlockMap[block];
    int blockIdx = this->Metadata->GetOverlappingAMRMetaData()->GetAMRBlockSourceIndex(blockIndex);

    unsigned int metaLevel;
    unsigned int metaIdx;
    this->Metadata->ComputeIndexPair(blockIndex, metaLevel, metaIdx);
    unsigned int level = this->GetBlockLevel(blockIdx);
    assert(level == metaLevel);

    if (this->IsBlockMine(block))
    {
      // STEP 0: Get the AMR block
      vtkTimerLog::MarkStartEvent("GetAMRBlock");
      vtkUniformGrid* amrBlock = this->GetAMRBlock(blockIdx);
      vtkTimerLog::MarkEndEvent("GetAMRBlock");
      assert("pre: AMR block is nullptr" && (amrBlock != nullptr));

      // STEP 2: Load any point-data
      vtkTimerLog::MarkStartEvent("vtkARMBaseReader::LoadPointData");
      this->LoadPointData(blockIdx, amrBlock);
      vtkTimerLog::MarkEndEvent("vtkAMRBaseReader::LoadPointData");

      // STEP 3: Load any cell data
      vtkTimerLog::MarkStartEvent("vtkAMRBaseReader::LoadCellData");
      this->LoadCellData(blockIdx, amrBlock);
      vtkTimerLog::MarkEndEvent("vtkAMRBaseReader::LoadCellData");

      // STEP 4: Add dataset
      output->SetDataSet(level, metaIdx, amrBlock);
      amrBlock->Delete();
    } // END if the block belongs to this process
    else
    {
      output->SetDataSet(level, metaIdx, nullptr);
    }
  } // END for all blocks
}

//------------------------------------------------------------------------------
int vtkAMRBaseReader::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** vtkNotUsed(inputVector), vtkInformationVector* outputVector)
{
  vtkTimerLog::MarkStartEvent("vtkAMRBaseReader::RqstData");
  this->NumBlocksFromCache = 0;
  this->NumBlocksFromFile = 0;

  vtkInformation* outInf = outputVector->GetInformationObject(0);
  vtkOverlappingAMR* output =
    vtkOverlappingAMR::SafeDownCast(outInf->Get(vtkDataObject::DATA_OBJECT()));
  assert("pre: output AMR dataset is nullptr" && (output != nullptr));

  output->Initialize(this->Metadata->GetAMRMetaData());

  // Setup the block request
  vtkTimerLog::MarkStartEvent("vtkAMRBaseReader::SetupBlockRequest");
  this->SetupBlockRequest(outInf);
  vtkTimerLog::MarkEndEvent("vtkAMRBaseReader::SetupBlockRequest");

  if (outInf->Has(vtkCompositeDataPipeline::LOAD_REQUESTED_BLOCKS()))
  {
    this->LoadRequestedBlocks(output);

    // Is blanking information generated when only a subset of blocks is
    // requested? Tricky question, since we need the blanking information when
    // requesting a fixed set of blocks and when when requesting one block at a
    // time in streaming fashion.
  }
  else
  {
#ifdef DEBUGME
    cout << "load " << this->BlockMap.size() << " blocks" << endl;
#endif
    this->AssignAndLoadBlocks(output);

    vtkTimerLog::MarkStartEvent("AMR::Generate Blanking");
    vtkParallelAMRUtilities::BlankCells(output, this->Controller);
    vtkTimerLog::MarkEndEvent("AMR::Generate Blanking");
  }

  // If this instance of the reader is not parallel, block until all processes
  // read their blocks.
  if (this->IsParallel())
  {
    this->Controller->Barrier();
  }

  if (this->Metadata && this->Metadata->GetInformation()->Has(vtkDataObject::DATA_TIME_STEP()))
  {
    double dataTime = this->Metadata->GetInformation()->Get(vtkDataObject::DATA_TIME_STEP());
    output->GetInformation()->Set(vtkDataObject::DATA_TIME_STEP(), dataTime);
  }

  outInf = nullptr;
  output = nullptr;

  vtkTimerLog::MarkEndEvent("vtkAMRBaseReader::RqstData");

  return 1;
}
VTK_ABI_NAMESPACE_END
