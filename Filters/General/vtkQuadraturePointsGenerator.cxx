// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkQuadraturePointsGenerator.h"

#include "vtkArrayDispatch.h"
#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkCellType.h"
#include "vtkDataArray.h"
#include "vtkDataArrayRange.h"
#include "vtkDataSetAttributes.h"
#include "vtkDoubleArray.h"
#include "vtkIdTypeArray.h"
#include "vtkInformation.h"
#include "vtkInformationQuadratureSchemeDefinitionVectorKey.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkType.h"

#include "vtkQuadraturePointsUtilities.hxx"
#include "vtkQuadratureSchemeDefinition.h"

#include <sstream>
#include <vector>

using std::ostringstream;

//------------------------------------------------------------------------------
VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkQuadraturePointsGenerator);

//------------------------------------------------------------------------------
vtkQuadraturePointsGenerator::vtkQuadraturePointsGenerator()
{
  this->SetNumberOfInputPorts(1);
  this->SetNumberOfOutputPorts(1);
}

//------------------------------------------------------------------------------
vtkQuadraturePointsGenerator::~vtkQuadraturePointsGenerator() = default;

//------------------------------------------------------------------------------
int vtkQuadraturePointsGenerator::FillOutputPortInformation(int, vtkInformation* info)
{
  info->Set(vtkDataObject::DATA_TYPE_NAME(), "vtkPolyData");
  return 1;
}

//------------------------------------------------------------------------------
int vtkQuadraturePointsGenerator::RequestData(
  vtkInformation*, vtkInformationVector** input, vtkInformationVector* output)
{
  vtkDataObject* tmpDataObj;
  // Get the input.
  tmpDataObj = input[0]->GetInformationObject(0)->Get(vtkDataObject::DATA_OBJECT());
  vtkDataSet* datasetIn = vtkDataSet::SafeDownCast(tmpDataObj);
  // Get the output.
  tmpDataObj = output->GetInformationObject(0)->Get(vtkDataObject::DATA_OBJECT());
  vtkPolyData* pdOut = vtkPolyData::SafeDownCast(tmpDataObj);

  // Quick sanity check.
  if (datasetIn == nullptr || pdOut == nullptr || datasetIn->GetNumberOfCells() == 0 ||
    datasetIn->GetNumberOfPoints() == 0 || datasetIn->GetCellData() == nullptr ||
    datasetIn->GetCellData()->GetNumberOfArrays() == 0)
  {
    vtkErrorMacro("Filter data has not been configured correctly. Aborting.");
    return 1;
  }

  // Generate points for the selected data array.
  // user specified the offsets array.
  this->Generate(datasetIn, this->GetInputArrayToProcess(0, input), pdOut);

  return 1;
}

namespace
{

struct GenerateWorker
{
  template <typename OffsetArrayT>
  void operator()(OffsetArrayT* offsetArray, vtkDataArray* data, vtkDataSet* usgIn,
    vtkPolyData* pdOut, std::vector<vtkQuadratureSchemeDefinition*>& dict,
    vtkQuadraturePointsGenerator* self)
  {
    const auto offsets = vtk::DataArrayValueRange<1>(offsetArray);

    const vtkIdType numCells = usgIn->GetNumberOfCells();
    const vtkIdType numVerts = pdOut->GetNumberOfPoints();
    vtkIdType previous = -1;

    bool shallowok = true;

    for (vtkIdType cellId = 0; cellId < numCells; cellId++)
    {
      if (self->CheckAbort())
      {
        break;
      }
      vtkIdType offset = static_cast<vtkIdType>(offsets[cellId]);

      if (offset != previous + 1)
      {
        shallowok = false;
        break;
      }

      const int cellType = usgIn->GetCellType(cellId);
      if (dict[cellType] == nullptr)
      {
        previous = offset;
      }
      else
      {
        previous = offset + dict[cellType]->GetNumberOfQuadraturePoints();
      }
    }

    if ((previous + 1) != numVerts)
    {
      shallowok = false;
    }

    if (shallowok)
    {
      // ok, all the original cells are here, we can shallow copy the array
      // from input to output
      pdOut->GetPointData()->AddArray(data);
    }
    else
    {
      // in this case, we have to duplicate the valid tuples
      vtkDataArray* V_out = data->NewInstance();
      V_out->SetName(data->GetName());
      V_out->SetNumberOfComponents(data->GetNumberOfComponents());
      V_out->CopyComponentNames(data);
      for (vtkIdType cellId = 0; cellId < numCells; cellId++)
      {
        vtkIdType offset = static_cast<vtkIdType>(offsets[cellId]);
        const int cellType = usgIn->GetCellType(cellId);

        // a simple check to see if a scheme really exists for this cell type.
        // should not happen if the cell type has not been modified.
        if (dict[cellType] == nullptr)
        {
          continue;
        }

        int np = dict[cellType]->GetNumberOfQuadraturePoints();
        for (int id = 0; id < np; id++)
        {
          V_out->InsertNextTuple(offset + id, data);
        }
      }
      V_out->Squeeze();
      pdOut->GetPointData()->AddArray(V_out);
      V_out->Delete();
    }
  }
};

} // end anon namespace

//------------------------------------------------------------------------------
int vtkQuadraturePointsGenerator::GenerateField(
  vtkDataSet* datasetIn, vtkDataArray* data, vtkDataArray* offsets, vtkPolyData* pdOut)
{
  vtkInformation* info = offsets->GetInformation();
  vtkInformationQuadratureSchemeDefinitionVectorKey* key =
    vtkQuadratureSchemeDefinition::DICTIONARY();
  if (!key->Has(info))
  {
    vtkErrorMacro(<< "Dictionary is not present in array " << offsets->GetName() << " " << offsets
                  << " Aborting.");
    return 0;
  }

  if (offsets->GetNumberOfComponents() != 1)
  {
    vtkErrorMacro("Expected offset array to only have a single component.");
    return 0;
  }

  int dictSize = key->Size(info);
  std::vector<vtkQuadratureSchemeDefinition*> dict(dictSize);
  key->GetRange(info, dict.data(), 0, 0, dictSize);

  // Use a fast path that assumes the offsets are integral:
  using vtkArrayDispatch::Integrals;
  using Dispatcher = vtkArrayDispatch::DispatchByValueType<Integrals>;

  GenerateWorker worker;
  if (!Dispatcher::Execute(offsets, worker, data, datasetIn, pdOut, dict, this))
  { // Fallback to slow path for other arrays:
    worker(offsets, data, datasetIn, pdOut, dict, this);
  }

  return 1;
}

//------------------------------------------------------------------------------
int vtkQuadraturePointsGenerator::Generate(
  vtkDataSet* datasetIn, vtkDataArray* offsets, vtkPolyData* pdOut)
{
  if (datasetIn == nullptr || offsets == nullptr || pdOut == nullptr)
  {
    vtkErrorMacro("configuration error");
    return 0;
  }

  if (offsets->GetNumberOfComponents() != 1)
  {
    vtkErrorMacro("Expected offsets array to have only a single component.");
    return 0;
  }

  // Strategy:
  // create the points then move the FieldData to PointData

  const char* offsetName = offsets->GetName();
  if (offsetName == nullptr)
  {
    vtkErrorMacro("offset array has no name, Skipping");
    return 1;
  }

  // get the dictionary
  vtkInformation* info = offsets->GetInformation();
  vtkInformationQuadratureSchemeDefinitionVectorKey* key =
    vtkQuadratureSchemeDefinition::DICTIONARY();
  if (!key->Has(info))
  {
    vtkErrorMacro(<< "Dictionary is not present in array " << offsets->GetName() << " Aborting.");
    return 0;
  }
  int dictSize = key->Size(info);
  std::vector<vtkQuadratureSchemeDefinition*> dict(dictSize);
  key->GetRange(info, dict.data(), 0, 0, dictSize);

  // Grab the point set.
  vtkDataArray* X = datasetIn->GetPoints()->GetData();

  // Create the result array.
  vtkDoubleArray* qPts = vtkDoubleArray::New();
  vtkIdType nCells = datasetIn->GetNumberOfCells();
  qPts->Allocate(3 * nCells); // Expect at least one point per cell
  qPts->SetNumberOfComponents(3);

  // For all cells interpolate.
  using Dispatcher = vtkArrayDispatch::Dispatch;
  vtkQuadraturePointsUtilities::InterpolateWorker worker;
  if (!Dispatcher::Execute(X, worker, datasetIn, nCells, dict, qPts, this))
  { // fall back to slow path:
    worker(X, datasetIn, nCells, dict, qPts, this);
  }

  // Add the interpolated quadrature points to the output
  vtkIdType nVerts = qPts->GetNumberOfTuples();
  vtkPoints* p = vtkPoints::New();
  p->SetDataTypeToDouble();
  p->SetData(qPts);
  qPts->Delete();
  pdOut->SetPoints(p);
  p->Delete();
  // Generate vertices at the quadrature points
  vtkIdTypeArray* va = vtkIdTypeArray::New();
  va->SetNumberOfTuples(2 * nVerts);
  vtkIdType* verts = va->GetPointer(0);
  for (int i = 0; i < nVerts; ++i)
  {
    verts[0] = 1;
    verts[1] = i;
    verts += 2;
  }
  vtkCellArray* cells = vtkCellArray::New();
  cells->AllocateExact(nVerts, va->GetNumberOfValues() - nVerts);
  cells->ImportLegacyFormat(va);
  pdOut->SetVerts(cells);
  cells->Delete();
  va->Delete();

  // then loop over all fields to map the field array to the points
  int nArrays = datasetIn->GetFieldData()->GetNumberOfArrays();
  for (int i = 0; i < nArrays; ++i)
  {
    if (this->CheckAbort())
    {
      break;
    }
    vtkDataArray* array = datasetIn->GetFieldData()->GetArray(i);
    if (array == nullptr)
      continue;

    const char* arrayOffsetName =
      array->GetInformation()->Get(vtkQuadratureSchemeDefinition::QUADRATURE_OFFSET_ARRAY_NAME());
    if (arrayOffsetName == nullptr)
    {
      // not an error, since non-quadrature point field data may
      //  be present.
      vtkDebugMacro(<< "array " << array->GetName() << " has no offset array name, Skipping");
      continue;
    }

    if (strcmp(offsetName, arrayOffsetName) != 0)
    {
      // not an error, this array does not belong with the current
      // quadrature scheme definition.
      vtkDebugMacro(<< "array " << array->GetName()
                    << " has another offset array : " << arrayOffsetName << ", Skipping");
      continue;
    }

    this->GenerateField(datasetIn, array, offsets, pdOut);
  }

  return 1;
}

//------------------------------------------------------------------------------
void vtkQuadraturePointsGenerator::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}
VTK_ABI_NAMESPACE_END
