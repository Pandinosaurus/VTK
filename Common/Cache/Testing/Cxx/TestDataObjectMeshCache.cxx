// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkAlgorithm.h"
#include "vtkCellData.h"
#include "vtkDataObjectMeshCache.h"
#include "vtkDoubleArray.h"
#include "vtkIdTypeArray.h"
#include "vtkLogger.h"
#include "vtkNew.h"
#include "vtkPartitionedDataSetCollection.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkSmartPointer.h"
#include "vtkTable.h"
#include "vtkTestErrorObserver.h"
#include "vtkTestUtilities.h"

namespace
{
//------------------------------------------------------------------------------
vtkSmartPointer<vtkPolyData> CreateData(int nbOfElements, int start)
{
  vtkNew<vtkPolyData> mesh;
  vtkNew<vtkPoints> points;
  vtkNew<vtkCellArray> lines;
  mesh->SetPoints(points);
  mesh->SetLines(lines);
  vtkNew<vtkDoubleArray> pointArray;
  pointArray->SetName("point_array");
  vtkNew<vtkDoubleArray> cellArray;
  cellArray->SetName("cell_array");

  mesh->GetPointData()->AddArray(pointArray);
  mesh->GetCellData()->AddArray(cellArray);

  for (int i = start; i < nbOfElements + start; i++)
  {
    points->InsertNextPoint(i, 0, 0);
    // line with next point. Last cell goes from last point to first.
    lines->InsertNextCell({ i, (i + 1) % nbOfElements });
    pointArray->InsertNextValue(i);
    cellArray->InsertNextValue(i);
  }

  return mesh;
}

//------------------------------------------------------------------------------
void AddOriginalIds(int start, vtkPolyData* mesh)
{
  vtkNew<vtkIdTypeArray> originalPts;
  originalPts->SetName(vtkDataObjectMeshCache::GetTemporaryIdsName().c_str());
  mesh->GetPointData()->AddArray(originalPts);
  vtkNew<vtkIdTypeArray> originalCells;
  originalCells->SetName(vtkDataObjectMeshCache::GetTemporaryIdsName().c_str());
  mesh->GetCellData()->AddArray(originalCells);
  auto nbOfElements = mesh->GetNumberOfPoints();
  for (int i = 0; i < nbOfElements; i++)
  {
    originalPts->InsertNextValue(start + i);
    originalCells->InsertNextValue(start + i);
  }
}

//------------------------------------------------------------------------------
bool TestDefault()
{
  vtkLogScopeFunction(INFO);
  vtkNew<vtkDataObjectMeshCache> cache;
  auto defaultStatus = cache->GetStatus();
  vtkLogIf(ERROR, defaultStatus.enabled(), "Error: default status should be invalid");

  vtkNew<vtkTest::ErrorObserver> observer;
  cache->AddObserver(vtkCommand::WarningEvent, observer);
  // copy unitinialized cache should fail
  vtkNew<vtkPolyData> output;
  cache->CopyCacheToDataObject(output);
  vtkLogIf(ERROR, observer->GetNumberOfWarnings() != 1,
    "Trying to update from empty cache should raise warning message");

  return true;
}

//------------------------------------------------------------------------------
bool TestSupportedData()
{
  vtkLogScopeFunction(INFO);
  vtkNew<vtkDataObjectMeshCache> cache;
  vtkSmartPointer<vtkPolyData> firstMesh = ::CreateData(4, 0);
  vtkLogIf(ERROR, !cache->IsSupportedData(firstMesh), << "PolyData should be supported by cache");

  vtkSmartPointer<vtkPolyData> secondMesh = ::CreateData(4, 0);
  vtkNew<vtkPartitionedDataSetCollection> collection;
  collection->SetNumberOfPartitionedDataSets(2);
  collection->SetPartition(0, 0, firstMesh);
  collection->SetPartition(1, 0, secondMesh);
  vtkLogIf(ERROR, !cache->IsSupportedData(collection),
    << "PartionedDataSetCollection should be supported by cache");

  vtkNew<vtkTable> table;
  collection->SetNumberOfPartitionedDataSets(3);
  collection->SetPartition(2, 0, table);
  vtkLogIf(
    ERROR, cache->IsSupportedData(collection), << "vtkTable should not be supported by cache.");

  collection->SetPartition(2, 0, nullptr);
  vtkLogIf(
    ERROR, !cache->IsSupportedData(collection), << "empty leaf should be supported by cache.");

  return true;
}

//------------------------------------------------------------------------------
bool TestCopyMesh()
{
  vtkLogScopeFunction(INFO);
  vtkNew<vtkDataObjectMeshCache> cache;
  vtkSmartPointer<vtkPolyData> inputMesh = ::CreateData(4, 0);
  cache->SetOriginalDataObject(inputMesh);
  vtkNew<vtkAlgorithm> consumer;
  cache->SetConsumer(consumer);
  cache->UpdateCache(inputMesh);

  vtkNew<vtkPolyData> output;
  cache->CopyCacheToDataObject(output);
  // arrays should not have been forwarded
  inputMesh->GetCellData()->Initialize();
  inputMesh->GetPointData()->Initialize();
  vtkTestUtilities::CompareDataObjects(inputMesh, output);
  vtkLogIf(ERROR, output->GetPointData()->GetNumberOfArrays() != 0,
    "Cache was not configured to forward point arrays: PointData should be empty.");
  vtkLogIf(ERROR, output->GetCellData()->GetNumberOfArrays() != 0,
    "Cache was not configured to forward cell arrays: CellData should be empty.");

  return true;
}

//------------------------------------------------------------------------------
bool TestForwardAttributes()
{
  vtkLogScopeFunction(INFO);
  vtkNew<vtkDataObjectMeshCache> cache;
  vtkSmartPointer<vtkPolyData> inputMesh = ::CreateData(4, 0);
  cache->SetOriginalDataObject(inputMesh);
  vtkNew<vtkAlgorithm> consumer;
  cache->SetConsumer(consumer);
  cache->UpdateCache(inputMesh);

  // fast forward attributes
  cache->PreserveAttributesOn();

  // forward pointdata
  cache->ForwardAttribute(vtkDataObject::POINT);
  vtkNew<vtkPolyData> output;
  cache->CopyCacheToDataObject(output);
  vtkLogIf(ERROR, output->GetPointData()->GetNumberOfArrays() != 1,
    "Output should have exactly 1 point data array. Have "
      << output->GetPointData()->GetNumberOfArrays());
  vtkLogIf(
    ERROR, !output->GetPointData()->HasArray("point_array"), "Expected data array not found!");
  vtkLogIf(ERROR, output->GetCellData()->GetNumberOfArrays() != 0,
    "Output should not have any cell data. Have " << output->GetPointData()->GetNumberOfArrays());

  // forward celldata too
  cache->ForwardAttribute(vtkDataObject::CELL);
  output->Initialize();
  cache->CopyCacheToDataObject(output);
  vtkTestUtilities::CompareDataObjects(output, inputMesh);

  // create new, smaller mesh.
  auto smallerMesh = ::CreateData(3, 0);
  // links to ids [1 - 3] of inputMesh
  ::AddOriginalIds(1, smallerMesh);
  cache->UpdateCache(smallerMesh);
  cache->PreserveAttributesOff();
  output->Initialize();
  cache->CopyCacheToDataObject(output);

  auto baseline = ::CreateData(3, 1);
  vtkTestUtilities::CompareFieldData(output->GetPointData(), baseline->GetPointData());
  vtkTestUtilities::CompareFieldData(output->GetCellData(), baseline->GetCellData());

  // do not forward cell
  cache->RemoveOriginalIds(vtkDataObject::CELL);
  output->Initialize();
  cache->CopyCacheToDataObject(output);
  vtkLogIf(ERROR, output->GetCellData()->GetNumberOfArrays() != 0,
    "Output should not have any cell data. Have " << output->GetPointData()->GetNumberOfArrays());
  vtkTestUtilities::CompareFieldData(output->GetPointData(), baseline->GetPointData());

  return true;
}

//------------------------------------------------------------------------------
bool TestCacheInvalidation()
{
  vtkLogScopeFunction(INFO);
  vtkNew<vtkDataObjectMeshCache> cache;
  vtkSmartPointer<vtkPolyData> inputMesh = ::CreateData(4, 0);
  cache->SetOriginalDataObject(inputMesh);
  vtkNew<vtkAlgorithm> consumer;
  cache->SetConsumer(consumer);
  cache->UpdateCache(inputMesh);

  // modified consumer
  consumer->Modified();
  auto consumerModifiedStatus = cache->GetStatus();
  vtkLogIf(ERROR, consumerModifiedStatus.enabled(),
    "Error: status should be invalid when consumer changes");
  vtkLogIf(ERROR, consumerModifiedStatus.ConsumerUnmodified, "Error: consumer was modified");
  // reset cache to a valid state
  cache->UpdateCache(inputMesh);

  // modified input MeshMTime
  inputMesh->GetPoints()->Modified();
  auto inputMeshModifiedStatus = cache->GetStatus();
  vtkLogIf(ERROR, inputMeshModifiedStatus.enabled(),
    "Error: status should be invalid when input mesh changes");
  vtkLogIf(ERROR, inputMeshModifiedStatus.OriginalMeshUnmodified, "Error: input was modified");
  // reset cache to a valid state
  cache->UpdateCache(inputMesh);

  inputMesh->Modified();
  auto inputModifiedStatus = cache->GetStatus();
  vtkLogIf(ERROR, !inputModifiedStatus.enabled(),
    "Error: status should be valid when input changes but from mesh");
  vtkLogIf(
    ERROR, !inputModifiedStatus.OriginalMeshUnmodified, "Error: input mesh was not modified");

  return true;
}

//------------------------------------------------------------------------------
bool TestDataObjectMeshTime()
{
  vtkSmartPointer<vtkPolyData> firstMesh = ::CreateData(4, 0);
  vtkSmartPointer<vtkPolyData> secondMesh = ::CreateData(4, 0);

  vtkNew<vtkPartitionedDataSetCollection> collection;
  collection->SetNumberOfPartitionedDataSets(2);
  collection->SetPartition(1, 0, secondMesh);
  collection->SetPartition(0, 0, firstMesh);

  auto firstMTime = vtkDataObjectMeshCache::GetDataObjectMeshMTime(firstMesh);
  vtkLogIf(ERROR, firstMTime != firstMesh->GetMeshMTime(),
    "Incorrect MeshMtime computation for a polydata");
  auto secondMTime = vtkDataObjectMeshCache::GetDataObjectMeshMTime(secondMesh);
  auto collecMTime = vtkDataObjectMeshCache::GetDataObjectMeshMTime(collection);
  vtkLogIf(ERROR, secondMTime != collecMTime, "Incorrect MeshMTime for composite");

  vtkNew<vtkTable> table;
  collection->SetNumberOfPartitionedDataSets(3);
  collection->SetPartition(2, 0, table);
  collecMTime = vtkDataObjectMeshCache::GetDataObjectMeshMTime(collection);
  vtkLogIf(ERROR, secondMTime != collecMTime,
    "Incorrect MeshMTime for composite: vtkTable should not interfer");

  collection->SetPartition(2, 0, nullptr);
  collecMTime = vtkDataObjectMeshCache::GetDataObjectMeshMTime(collection);
  vtkLogIf(ERROR, secondMTime != collecMTime,
    "Incorrect MeshMTime for composite: empty leaf should not interfer");

  return true;
}
}

//------------------------------------------------------------------------------
int TestDataObjectMeshCache(int vtkNotUsed(argc), char* vtkNotUsed(argv)[])
{
  return (::TestDefault() && ::TestSupportedData() && ::TestCopyMesh() &&
           ::TestForwardAttributes() && ::TestCacheInvalidation() && ::TestDataObjectMeshTime())
    ? EXIT_SUCCESS
    : EXIT_FAILURE;
}
