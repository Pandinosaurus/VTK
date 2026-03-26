// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkTransformFilter.h"

#include "vtkCellData.h"
#include "vtkDoubleArray.h"
#include "vtkFloatArray.h"
#include "vtkImageData.h"
#include "vtkImageDataToPointSet.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkLinearTransform.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkPointSet.h"
#include "vtkRectilinearGrid.h"
#include "vtkRectilinearGridToPointSet.h"
#include "vtkSmartPointer.h"
#include "vtkStructuredGrid.h"

#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkTransformFilter);
vtkCxxSetObjectMacro(vtkTransformFilter, Transform, vtkAbstractTransform);

//------------------------------------------------------------------------------
vtkTransformFilter::vtkTransformFilter()
{
  this->Transform = nullptr;
  this->OutputPointsPrecision = vtkAlgorithm::DEFAULT_PRECISION;
  this->TransformAllInputVectors = false;
}

//------------------------------------------------------------------------------
vtkTransformFilter::~vtkTransformFilter()
{
  this->SetTransform(nullptr);
}

//------------------------------------------------------------------------------
int vtkTransformFilter::FillInputPortInformation(int vtkNotUsed(port), vtkInformation* info)
{
  info->Remove(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE());
  info->Append(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkPointSet");
  info->Append(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkImageData");
  info->Append(vtkAlgorithm::INPUT_REQUIRED_DATA_TYPE(), "vtkRectilinearGrid");
  return 1;
}

//------------------------------------------------------------------------------
int vtkTransformFilter::RequestDataObject(
  vtkInformation* request, vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkImageData* inImage = vtkImageData::GetData(inputVector[0]);
  vtkRectilinearGrid* inRect = vtkRectilinearGrid::GetData(inputVector[0]);

  if (inImage || inRect)
  {
    vtkStructuredGrid* output = vtkStructuredGrid::GetData(outputVector);
    if (!output)
    {
      vtkNew<vtkStructuredGrid> newOutput;
      outputVector->GetInformationObject(0)->Set(vtkDataObject::DATA_OBJECT(), newOutput);
    }
    return 1;
  }
  else
  {
    return this->Superclass::RequestDataObject(request, inputVector, outputVector);
  }
}

//------------------------------------------------------------------------------
vtkPointSet* vtkTransformFilter::SanitizeInput(vtkInformationVector* inputVector)
{
  vtkSmartPointer<vtkPointSet> input = vtkPointSet::GetData(inputVector);
  if (!input)
  {
    // Try converting image data.
    vtkImageData* inImage = vtkImageData::GetData(inputVector);
    if (inImage)
    {
      vtkNew<vtkImageDataToPointSet> image2points;
      image2points->SetInputData(inImage);
      image2points->SetContainerAlgorithm(this);
      image2points->Update();
      input = image2points->GetOutput();
    }
  }

  if (!input)
  {
    // Try converting rectilinear grid.
    vtkRectilinearGrid* inRect = vtkRectilinearGrid::GetData(inputVector);
    if (inRect)
    {
      vtkNew<vtkRectilinearGridToPointSet> rect2points;
      rect2points->SetInputData(inRect);
      rect2points->SetContainerAlgorithm(this);
      rect2points->Update();
      input = rect2points->GetOutput();
    }
  }

  return input;
}

//------------------------------------------------------------------------------
void vtkTransformFilter::InitializeOutput(vtkPointSet* input, vtkPointSet* output)
{
  // First, copy the input to the output as a starting point
  output->ShallowCopy(input);

  // Allocate transformed points
  vtkPoints* inPts = input->GetPoints();
  vtkNew<vtkPoints> newPts;
  // Set the desired precision for the points in the output.
  if (this->OutputPointsPrecision == vtkAlgorithm::DEFAULT_PRECISION)
  {
    newPts->SetDataType(inPts->GetDataType());
  }
  else if (this->OutputPointsPrecision == vtkAlgorithm::SINGLE_PRECISION)
  {
    newPts->SetDataType(VTK_FLOAT);
  }
  else if (this->OutputPointsPrecision == vtkAlgorithm::DOUBLE_PRECISION)
  {
    newPts->SetDataType(VTK_DOUBLE);
  }
  vtkIdType numPts = inPts->GetNumberOfPoints();
  newPts->Reserve(numPts);
  output->SetPoints(newPts);

  vtkDataArray* inVectors;
  vtkDataArray* inNormals;
  vtkPointData* pd = input->GetPointData();
  vtkPointData* outPD = output->GetPointData();

  inVectors = pd->GetVectors();
  inNormals = pd->GetNormals();

  // always transform Vectors and Normals
  vtkSmartPointer<vtkDataArray> newVectors;
  if (inVectors)
  {
    newVectors.TakeReference(this->CreateFromArray(inVectors));
    outPD->SetVectors(newVectors);
  }
  vtkSmartPointer<vtkDataArray> newNormals;
  if (inNormals)
  {
    newNormals.TakeReference(this->CreateFromArray(inNormals));
    outPD->SetNormals(newNormals);
  }

  // Initialize new empty arrays when required.
  // Looks like Transform need empty but allocated buffers
  if (this->TransformAllInputVectors)
  {
    int nArrays = pd->GetNumberOfArrays();
    vtkSmartPointer<vtkDataArray> outArray;
    for (int arrayIndex = 0; arrayIndex < nArrays; arrayIndex++)
    {
      vtkDataArray* inputArray = pd->GetArray(arrayIndex);
      if (inputArray != inVectors && inputArray != inNormals &&
        inputArray->GetNumberOfComponents() == 3)
      {
        outArray.TakeReference(this->CreateFromArray(inputArray));
        outPD->AddArray(outArray);
      }
    }
  }
}

//------------------------------------------------------------------------------
void vtkTransformFilter::TransformPointData(vtkPointSet* input, vtkPointSet* output)
{
  auto inPts = input->GetPoints();
  auto newPts = output->GetPoints();
  auto pd = input->GetPointData();
  auto outPD = output->GetPointData();

  auto inVectors = pd->GetVectors();
  auto inNormals = pd->GetNormals();

  std::vector<vtkDataArray*> inAdditionalVectors;
  std::vector<vtkDataArray*> outAdditionalVectors;
  if (this->TransformAllInputVectors)
  {
    int nArrays = pd->GetNumberOfArrays();
    vtkSmartPointer<vtkDataArray> outArray;
    for (int arrayIndex = 0; arrayIndex < nArrays; arrayIndex++)
    {
      auto inArray = pd->GetArray(arrayIndex);
      if (inArray != inVectors && inArray != inNormals && inArray->GetNumberOfComponents() == 3)
      {
        inAdditionalVectors.push_back(inArray);
        outAdditionalVectors.push_back(outPD->GetArray(inArray->GetName()));
      }
    }
  }

  // Loop over all points, updating position
  //
  if (inVectors || inNormals || !inAdditionalVectors.empty())
  {
    auto newNormals = outPD->GetNormals();
    auto newVectors = outPD->GetVectors();
    this->Transform->TransformPointsNormalsVectors(inPts, newPts, inNormals, newNormals, inVectors,
      newVectors, inAdditionalVectors.size(), inAdditionalVectors.data(),
      outAdditionalVectors.data());
  }
  else
  {
    this->Transform->TransformPoints(inPts, newPts);
  }
}

//------------------------------------------------------------------------------
void vtkTransformFilter::TransformCellData(vtkPointSet* input, vtkPointSet* output)
{
  // Can only transform cell normals/vectors if the transform
  // is linear.
  vtkLinearTransform* lt = vtkLinearTransform::SafeDownCast(this->Transform);
  if (!lt)
  {
    return;
  }

  vtkCellData* cd = input->GetCellData();
  vtkCellData* outCD = output->GetCellData();
  auto inCellVectors = cd->GetVectors();
  auto inCellNormals = cd->GetNormals();

  vtkSmartPointer<vtkDataArray> newCellVectors;
  vtkSmartPointer<vtkDataArray> newCellNormals;
  if (inCellVectors)
  {

    newCellVectors.TakeReference(this->CreateFromArray(inCellVectors));
    outCD->AddArray(newCellVectors);
    lt->TransformVectors(inCellVectors, newCellVectors);
  }

  if (inCellNormals)
  {
    newCellNormals.TakeReference(this->CreateFromArray(inCellNormals));
    outCD->AddArray(newCellNormals);
    lt->TransformNormals(inCellNormals, newCellNormals);
  }

  if (this->TransformAllInputVectors)
  {
    vtkSmartPointer<vtkDataArray> tmpOutArray;
    for (int i = 0; i < cd->GetNumberOfArrays(); i++)
    {
      if (this->CheckAbort())
      {
        break;
      }
      vtkDataArray* tmpArray = cd->GetArray(i);
      if (tmpArray != inCellVectors && tmpArray != inCellNormals &&
        tmpArray->GetNumberOfComponents() == 3)
      {
        tmpOutArray.TakeReference(this->CreateFromArray(tmpArray));
        lt->TransformVectors(tmpArray, tmpOutArray);
        outCD->AddArray(tmpOutArray);
      }
    }
  }
}

//------------------------------------------------------------------------------
int vtkTransformFilter::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  vtkSmartPointer<vtkPointSet> input = this->SanitizeInput(inputVector[0]);

  if (!input)
  {
    vtkErrorMacro(<< "Invalid or missing input");
    return 0;
  }

  // Check input
  //
  if (this->Transform == nullptr)
  {
    vtkErrorMacro(<< "No transform defined!");
    return 1;
  }

  if (!input->GetPoints())
  {
    return 1;
  }

  vtkDebugMacro(<< "Executing transform filter");

  vtkPointSet* output = vtkPointSet::GetData(outputVector);
  this->InitializeOutput(input, output);
  this->UpdateProgress(.2);
  this->TransformPointData(input, output);
  this->UpdateProgress(.6);
  this->TransformCellData(input, output);
  this->UpdateProgress(.8);

  this->UpdateProgress(1.);

  return 1;
}

//------------------------------------------------------------------------------
vtkMTimeType vtkTransformFilter::GetMTime()
{
  vtkMTimeType mTime = this->MTime.GetMTime();
  vtkMTimeType transMTime;

  if (this->Transform)
  {
    transMTime = this->Transform->GetMTime();
    mTime = (transMTime > mTime ? transMTime : mTime);
  }

  return mTime;
}

//------------------------------------------------------------------------------
vtkDataArray* vtkTransformFilter::CreateNewDataArray(vtkDataArray* input)
{
  if (this->OutputPointsPrecision == vtkAlgorithm::DEFAULT_PRECISION && input != nullptr)
  {
    return input->NewInstance();
  }

  switch (this->OutputPointsPrecision)
  {
    case vtkAlgorithm::DOUBLE_PRECISION:
      return vtkDoubleArray::New();
    case vtkAlgorithm::SINGLE_PRECISION:
    default:
      return vtkFloatArray::New();
  }
}

//------------------------------------------------------------------------------
vtkDataArray* vtkTransformFilter::CreateFromArray(vtkDataArray* input)
{
  auto output = this->CreateNewDataArray(input);
  output->SetName(input->GetName());
  output->SetNumberOfComponents(input->GetNumberOfComponents());
  output->ReserveTuples(input->GetNumberOfTuples());
  return output;
}

//------------------------------------------------------------------------------
void vtkTransformFilter::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Transform: " << this->Transform << "\n";
  os << indent << "Output Points Precision: " << this->OutputPointsPrecision << "\n";
}
VTK_ABI_NAMESPACE_END
