// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkAMRCutPlane.h"

#include "vtkCell.h"
#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkCompositeDataPipeline.h"
#include "vtkCutter.h"
#include "vtkDoubleArray.h"
#include "vtkIndent.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkMultiProcessController.h"
#include "vtkObjectFactory.h"
#include "vtkOverlappingAMR.h"
#include "vtkPlane.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkUniformGrid.h"
#include "vtkUnstructuredGrid.h"

#include <algorithm>
#include <cassert>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkAMRCutPlane);

vtkCxxSetObjectMacro(vtkAMRCutPlane, Controller, vtkMultiProcessController);
//------------------------------------------------------------------------------
vtkAMRCutPlane::vtkAMRCutPlane()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
  this->LevelOfResolution = 0;
  this->InitialRequest = true;
  for (int i = 0; i < 3; ++i)
  {
    this->Center[i] = 0.0;
    this->Normal[i] = 0.0;
  }
  this->Controller = nullptr;
  this->SetController(vtkMultiProcessController::GetGlobalController());
  this->UseNativeCutter = true;
}

//------------------------------------------------------------------------------
vtkAMRCutPlane::~vtkAMRCutPlane()
{
  this->SetController(nullptr);
  this->BlocksToLoad.clear();
}

//------------------------------------------------------------------------------
void vtkAMRCutPlane::PrintSelf(std::ostream& oss, vtkIndent indent)
{
  this->Superclass::PrintSelf(oss, indent);
  oss << indent << "LevelOfResolution: " << this->LevelOfResolution << endl;
  oss << indent << "UseNativeCutter: " << this->UseNativeCutter << endl;
  oss << indent << "Controller: " << this->Controller << endl;
  oss << indent << "Center: ";
  for (int i = 0; i < 3; ++i)
  {
    oss << this->Center[i] << " ";
  }
  oss << endl;
  oss << indent << "Normal: ";
  for (int i = 0; i < 3; ++i)
  {
    oss << this->Normal[i] << " ";
  }
  oss << endl;
}

//------------------------------------------------------------------------------
int vtkAMRCutPlane::FillInputPortInformation(int vtkNotUsed(port), vtkInformation* info)
{
  assert("pre: information object is nullptr!" && (info != nullptr));
  info->Set(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkOverlappingAMR");
  return 1;
}

//------------------------------------------------------------------------------
int vtkAMRCutPlane::FillOutputPortInformation(int vtkNotUsed(port), vtkInformation* info)
{
  assert("pre: information object is nullptr!" && (info != nullptr));
  info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkMultiBlockDataSet");
  return 1;
}

//------------------------------------------------------------------------------
int vtkAMRCutPlane::RequestInformation(vtkInformation* vtkNotUsed(rqst),
  vtkInformationVector** inputVector, vtkInformationVector* vtkNotUsed(outputVector))
{
  this->BlocksToLoad.clear();

  vtkInformation* input = inputVector[0]->GetInformationObject(0);
  assert("pre: input information object is nullptr" && (input != nullptr));

  if (input->Has(vtkCompositeDataPipeline::COMPOSITE_DATA_META_DATA()))
  {
    vtkOverlappingAMR* metadata = vtkOverlappingAMR::SafeDownCast(
      input->Get(vtkCompositeDataPipeline::COMPOSITE_DATA_META_DATA()));

    vtkPlane* cutPlane = this->GetCutPlane(metadata);
    assert("Cut plane is nullptr" && (cutPlane != nullptr));

    this->ComputeAMRBlocksToLoad(cutPlane, metadata);
    cutPlane->Delete();
  }

  this->Modified();
  return 1;
}

//------------------------------------------------------------------------------
int vtkAMRCutPlane::RequestUpdateExtent(vtkInformation* vtkNotUsed(rqst),
  vtkInformationVector** inputVector, vtkInformationVector* vtkNotUsed(outputVector))
{
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  assert("pre: inInfo is nullptr" && (inInfo != nullptr));

  inInfo->Set(vtkCompositeDataPipeline::UPDATE_COMPOSITE_INDICES(), this->BlocksToLoad.data(),
    static_cast<int>(this->BlocksToLoad.size()));
  return 1;
}

//------------------------------------------------------------------------------
int vtkAMRCutPlane::RequestData(vtkInformation* vtkNotUsed(rqst),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // STEP 0: Get input object
  vtkOverlappingAMR* input = vtkOverlappingAMR::GetData(inputVector[0]);
  if (!input)
  {
    vtkErrorMacro("Input AMR dataset is nullptr!");
    return 0;
  }

  // STEP 1: Get output object
  auto output = vtkMultiBlockDataSet::GetData(outputVector);
  if (!output)
  {
    vtkErrorMacro("Output multi-block dataset is nullptr!");
    return 0;
  }

  if (this->IsAMRData2D(input))
  {
    // Return an empty multi-block, we cannot cut a 2-D dataset
    return 1;
  }

  auto cutPlane = vtk::TakeSmartPointer(this->GetCutPlane(input));
  if (!cutPlane)
  {
    vtkErrorMacro("Cut plane is nullptr!");
    return 0;
  }

  output->CopyStructure(input);

  auto inIter = vtk::TakeSmartPointer(input->NewIterator());
  for (inIter->InitTraversal(); !inIter->IsDoneWithTraversal(); inIter->GoToNextItem())
  {
    if (this->CheckAbort())
    {
      break;
    }
    auto grid = vtkUniformGrid::SafeDownCast(inIter->GetCurrentDataObject());
    if (this->UseNativeCutter == 1)
    {
      if (grid != nullptr)
      {
        vtkNew<vtkCutter> myCutter;
        myCutter->SetInputData(grid);
        myCutter->SetCutFunction(cutPlane);
        myCutter->SetContainerAlgorithm(this);
        myCutter->Update();
        output->SetDataSet(inIter, myCutter->GetOutput());
      }
      else
      {
        output->SetDataSet(inIter, nullptr);
      }
    }
    else
    {
      if (grid != nullptr)
      {
        output->SetDataSet(inIter, this->CutAMRBlock(cutPlane, grid));
      }
      else
      {
        output->SetDataSet(inIter, nullptr);
      }
    }
  }
  return 1;
}

//------------------------------------------------------------------------------
vtkSmartPointer<vtkUnstructuredGrid> vtkAMRCutPlane::CutAMRBlock(
  vtkPlane* cutPlane, vtkUniformGrid* grid)
{
  assert("pre: grid is nullptr" && (grid != nullptr));

  vtkNew<vtkUnstructuredGrid> mesh;
  vtkNew<vtkPoints> meshPts;
  meshPts->SetDataTypeToDouble();
  vtkNew<vtkCellArray> cells;

  // Maps points from the input grid to the output grid
  std::map<vtkIdType, vtkIdType> grdPntMapping;
  std::vector<vtkIdType> extractedCells;

  vtkIdType cellIdx = 0;
  for (; cellIdx < grid->GetNumberOfCells(); ++cellIdx)
  {
    if (grid->IsCellVisible(cellIdx) && this->PlaneIntersectsCell(cutPlane, grid->GetCell(cellIdx)))
    {
      extractedCells.push_back(cellIdx);
      this->ExtractCellFromGrid(grid, grid->GetCell(cellIdx), grdPntMapping, meshPts, cells);
    } // END if
  }   // END for all cells

  // Sanity checks
  assert("post: Number of mesh points should match map size!" &&
    (static_cast<int>(grdPntMapping.size()) == meshPts->GetNumberOfPoints()));
  assert("post: Number of cells mismatch" &&
    (cells->GetNumberOfCells() == static_cast<int>(extractedCells.size())));

  // Insert the points
  mesh->SetPoints(meshPts);

  std::vector<int> types;
  if (grid->GetDataDimension() == 3)
  {
    types.resize(cells->GetNumberOfCells(), VTK_VOXEL);
  }
  else
  {
    vtkErrorMacro("Cannot cut a grid of dimension=" << grid->GetDataDimension());
    return nullptr;
  }

  // Insert the cells
  mesh->SetCells(types.data(), cells);

  // Extract fields
  this->ExtractPointDataFromGrid(
    grid, grdPntMapping, mesh->GetNumberOfPoints(), mesh->GetPointData());
  this->ExtractCellDataFromGrid(grid, extractedCells, mesh->GetCellData());

  return mesh;
}

//------------------------------------------------------------------------------
void vtkAMRCutPlane::ExtractCellFromGrid(vtkUniformGrid* grid, vtkCell* cell,
  std::map<vtkIdType, vtkIdType>& grdPntMapping, vtkPoints* nodes, vtkCellArray* cells)
{
  assert("pre: grid is nullptr" && (grid != nullptr));
  assert("pre: cell is nullptr" && (cell != nullptr));
  assert("pre: cells is nullptr" && (cells != nullptr));

  cells->InsertNextCell(cell->GetNumberOfPoints());
  for (vtkIdType nodeIdx = 0; nodeIdx < cell->GetNumberOfPoints(); ++nodeIdx)
  {
    // Get the point ID w.r.t. the grid
    vtkIdType meshPntIdx = cell->GetPointId(nodeIdx);
    assert("pre: mesh point ID should within grid range point ID" &&
      (meshPntIdx < grid->GetNumberOfPoints()));

    if (grdPntMapping.find(meshPntIdx) != grdPntMapping.end())
    {
      // Point already exists in nodes
      cells->InsertCellPoint(grdPntMapping[meshPntIdx]);
    }
    else
    {
      // Push point to the end of the list
      vtkIdType nidx = nodes->GetNumberOfPoints();
      double* pnt = grid->GetPoint(meshPntIdx);
      nodes->InsertPoint(nidx, pnt);
      assert("post: number of points should be increased by 1" &&
        (nodes->GetNumberOfPoints() == (nidx + 1)));
      grdPntMapping[meshPntIdx] = nidx;
      cells->InsertCellPoint(nidx);
    }
  } // END for all nodes
}

//------------------------------------------------------------------------------
void vtkAMRCutPlane::ExtractPointDataFromGrid(vtkUniformGrid* grid,
  std::map<vtkIdType, vtkIdType>& gridPntMapping, vtkIdType NumNodes, vtkPointData* PD)
{
  assert("pre: grid is nullptr!" && (grid != nullptr));
  assert("pre: target point data is nullptr!" && (PD != nullptr));

  if ((grid->GetPointData()->GetNumberOfArrays() == 0) || (gridPntMapping.empty()))
  {
    // Nothing to extract short-circuit here
    return;
  }

  vtkPointData* GPD = grid->GetPointData();
  for (int fieldArray = 0; fieldArray < GPD->GetNumberOfArrays(); ++fieldArray)
  {
    vtkDataArray* sourceArray = GPD->GetArray(fieldArray);
    int dataType = sourceArray->GetDataType();
    vtkDataArray* array = vtkDataArray::CreateDataArray(dataType);
    assert("pre: failed to create array!" && (array != nullptr));

    array->SetName(sourceArray->GetName());
    array->SetNumberOfComponents(sourceArray->GetNumberOfComponents());
    array->SetNumberOfTuples(NumNodes);

    // Copy tuples from source array
    std::map<vtkIdType, vtkIdType>::iterator iter = gridPntMapping.begin();
    for (; iter != gridPntMapping.end(); ++iter)
    {
      vtkIdType srcIdx = iter->first;
      vtkIdType targetIdx = iter->second;
      assert("pre: source node index is out-of-bounds" && (srcIdx >= 0) &&
        (srcIdx < grid->GetNumberOfPoints()));
      assert(
        "pre: target node index is out-of-bounds" && (targetIdx >= 0) && (targetIdx < NumNodes));
      array->SetTuple(targetIdx, srcIdx, sourceArray);
    } // END for all extracted nodes

    PD->AddArray(array);
    array->Delete();
  } // END for all arrays
}

//------------------------------------------------------------------------------
void vtkAMRCutPlane::ExtractCellDataFromGrid(
  vtkUniformGrid* grid, std::vector<vtkIdType>& cellIdxList, vtkCellData* CD)
{
  assert("pre: grid is nullptr!" && (grid != nullptr));
  assert("pre: target cell data is nullptr!" && (CD != nullptr));

  if ((grid->GetCellData()->GetNumberOfArrays() == 0) || (cellIdxList.empty()))
  {
    // Nothing to extract short-circuit here
    return;
  }

  int NumCells = static_cast<int>(cellIdxList.size());
  vtkCellData* GCD = grid->GetCellData();
  for (int fieldArray = 0; fieldArray < GCD->GetNumberOfArrays(); ++fieldArray)
  {
    vtkDataArray* sourceArray = GCD->GetArray(fieldArray);
    int dataType = sourceArray->GetDataType();
    vtkDataArray* array = vtkDataArray::CreateDataArray(dataType);
    assert("pre: failed to create array!" && (array != nullptr));

    array->SetName(sourceArray->GetName());
    array->SetNumberOfComponents(sourceArray->GetNumberOfComponents());
    array->SetNumberOfTuples(NumCells);

    // Copy tuples from source array
    for (int i = 0; i < NumCells; ++i)
    {
      vtkIdType cellIdx = cellIdxList[i];
      assert("pre: cell index is out-of-bounds!" && (cellIdx >= 0) &&
        (cellIdx < grid->GetNumberOfCells()));
      array->SetTuple(i, cellIdx, sourceArray);
    } // END for all extracted cells

    CD->AddArray(array);
    array->Delete();
  } // END for all arrays
}

//------------------------------------------------------------------------------
vtkPlane* vtkAMRCutPlane::GetCutPlane(vtkOverlappingAMR* metadata)
{
  assert("pre: metadata is nullptr" && (metadata != nullptr));

  vtkPlane* pl = vtkPlane::New();

  double bounds[6];
  metadata->GetBounds(bounds);

  // Get global bounds
  double minBounds[3] = { bounds[0], bounds[2], bounds[4] };
  double maxBounds[3] = { bounds[1], bounds[3], bounds[5] };

  this->InitializeCenter(minBounds, maxBounds);

  pl->SetNormal(this->Normal);
  pl->SetOrigin(this->Center);
  return (pl);
}

//------------------------------------------------------------------------------
void vtkAMRCutPlane::ComputeAMRBlocksToLoad(vtkPlane* p, vtkOverlappingAMR* m)
{
  assert("pre: Plane object is nullptr" && (p != nullptr));
  assert("pre: metadata is nullptr" && (m != nullptr));

  // Store A,B,C,D from the plane equation
  double plane[4];
  plane[0] = p->GetNormal()[0];
  plane[1] = p->GetNormal()[1];
  plane[2] = p->GetNormal()[2];
  plane[3] = p->GetNormal()[0] * p->GetOrigin()[0] + p->GetNormal()[1] * p->GetOrigin()[1] +
    p->GetNormal()[2] * p->GetOrigin()[2];

  double bounds[6];

  int NumLevels = m->GetNumberOfLevels();
  int maxLevelToLoad = (this->LevelOfResolution < NumLevels) ? this->LevelOfResolution : NumLevels;

  unsigned int level = 0;
  for (; level <= static_cast<unsigned int>(maxLevelToLoad); ++level)
  {
    unsigned int dataIdx = 0;
    for (; dataIdx < m->GetNumberOfBlocks(level); ++dataIdx)
    {
      m->GetBounds(level, dataIdx, bounds);
      if (this->PlaneIntersectsAMRBox(plane, bounds))
      {
        unsigned int amrGridIdx = m->GetAbsoluteBlockIndex(level, dataIdx);
        this->BlocksToLoad.push_back(amrGridIdx);
      }
    } // END for all data
  }   // END for all levels

  std::sort(this->BlocksToLoad.begin(), this->BlocksToLoad.end());
}

//------------------------------------------------------------------------------
void vtkAMRCutPlane::InitializeCenter(double min[3], double max[3])
{
  if (!this->InitialRequest)
  {
    return;
  }

  this->Center[0] = 0.5 * (max[0] - min[0]);
  this->Center[1] = 0.5 * (max[1] - min[1]);
  this->Center[2] = 0.5 * (max[2] - min[2]);
  this->InitialRequest = false;
}

//------------------------------------------------------------------------------
bool vtkAMRCutPlane::PlaneIntersectsCell(vtkPlane* pl, vtkCell* cell)
{
  assert("pre: plane is nullptr" && (pl != nullptr));
  assert("pre: cell is nullptr!" && (cell != nullptr));
  return (this->PlaneIntersectsAMRBox(pl, cell->GetBounds()));
}
//------------------------------------------------------------------------------
bool vtkAMRCutPlane::PlaneIntersectsAMRBox(vtkPlane* pl, double bounds[6])
{
  assert("pre: plane is nullptr" && (pl != nullptr));

  // Store A,B,C,D from the plane equation
  double plane[4];
  plane[0] = pl->GetNormal()[0];
  plane[1] = pl->GetNormal()[1];
  plane[2] = pl->GetNormal()[2];
  plane[3] = pl->GetNormal()[0] * pl->GetOrigin()[0] + pl->GetNormal()[1] * pl->GetOrigin()[1] +
    pl->GetNormal()[2] * pl->GetOrigin()[2];

  return (this->PlaneIntersectsAMRBox(plane, bounds));
}

//------------------------------------------------------------------------------
bool vtkAMRCutPlane::PlaneIntersectsAMRBox(double plane[4], double bounds[6])
{
  bool lowPnt = false;
  bool highPnt = false;

  for (int i = 0; i < 8; ++i)
  {
    // Get box coordinates
    double x = (i & 1) ? bounds[1] : bounds[0];
    double y = (i & 2) ? bounds[3] : bounds[2];
    double z = (i & 3) ? bounds[5] : bounds[4];

    // Plug-in coordinates to the plane equation
    double v = plane[3] - plane[0] * x - plane[1] * y - plane[2] * z;

    if (v == 0.0) // Point is on a plane
    {
      return true;
    }

    if (v < 0.0)
    {
      lowPnt = true;
    }
    else
    {
      highPnt = true;
    }

    if (lowPnt && highPnt)
    {
      return true;
    }
  }

  return false;
}

//------------------------------------------------------------------------------
bool vtkAMRCutPlane::IsAMRData2D(vtkOverlappingAMR* input)
{
  assert("pre: Input AMR dataset is nullptr" && (input != nullptr));

  return input->GetGridDescription() != vtkStructuredData::VTK_STRUCTURED_XYZ_GRID;
}
VTK_ABI_NAMESPACE_END
