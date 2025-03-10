// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkExtractVOI.h"

#include "vtkCellData.h"
#include "vtkExtractStructuredGridHelper.h"
#include "vtkImageData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkStructuredData.h"

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkExtractVOI);

// Construct object to extract all of the input data.
vtkExtractVOI::vtkExtractVOI()
{
  this->VOI[0] = this->VOI[2] = this->VOI[4] = 0;
  this->VOI[1] = this->VOI[3] = this->VOI[5] = VTK_INT_MAX;

  this->SampleRate[0] = this->SampleRate[1] = this->SampleRate[2] = 1;
  this->IncludeBoundary = 0;

  this->Internal = vtkExtractStructuredGridHelper::New();
}

//------------------------------------------------------------------------------
vtkExtractVOI::~vtkExtractVOI()
{
  if (this->Internal != nullptr)
  {
    this->Internal->Delete();
  }
}

//------------------------------------------------------------------------------
int vtkExtractVOI::RequestUpdateExtent(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // get the info objects
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);

  // Re-init helper to full whole extent. This is needed since `RequestData`
  // modifies the helper to limit to the input extents rather than whole
  // extents.
  int wholeExtent[6];
  inInfo->Get(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), wholeExtent);
  this->Internal->Initialize(
    this->VOI, wholeExtent, this->SampleRate, (this->IncludeBoundary == 1));

  if (!this->Internal->IsValid())
  {
    return 0;
  }

  int i;
  bool emptyExtent = false;
  int uExt[6];
  for (i = 0; i < 3; i++)
  {
    if (this->Internal->GetSize(i) < 1)
    {
      uExt[0] = uExt[2] = uExt[4] = 0;
      uExt[1] = uExt[3] = uExt[5] = -1;
      emptyExtent = true;
      break;
    }
  }

  if (!emptyExtent)
  {
    // Find input update extent based on requested output
    // extent
    int oUExt[6];
    outputVector->GetInformationObject(0)->Get(
      vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), oUExt);

    if ((this->SampleRate[0] == 1) && (this->SampleRate[1] == 1) && (this->SampleRate[2] == 1))
    {
      memcpy(uExt, oUExt, sizeof(int) * 6);
    } // END if sub-sampling
    else
    {
      int oWExt[6]; // Account for partitioning
      this->Internal->GetOutputWholeExtent(oWExt);
      for (i = 0; i < 3; i++)
      {
        int idx = oUExt[2 * i] - oWExt[2 * i]; // Extent value to index
        if (idx < 0 || idx >= this->Internal->GetSize(i))
        {
          vtkWarningMacro("Requested extent outside whole extent.");
          idx = 0;
        }
        uExt[2 * i] = this->Internal->GetMappedExtentValueFromIndex(i, idx);
        int jdx = oUExt[2 * i + 1] - oWExt[2 * i]; // Extent value to index
        if (jdx < idx || jdx >= this->Internal->GetSize(i))
        {
          vtkWarningMacro("Requested extent outside whole extent.");
          jdx = 0;
        }
        uExt[2 * i + 1] = this->Internal->GetMappedExtentValueFromIndex(i, jdx);
      }
    } // END else if sub-sampling

  } // END if extent is not empty

  inInfo->Set(vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), uExt, 6);
  // We can handle anything.
  inInfo->Set(vtkStreamingDemandDrivenPipeline::EXACT_EXTENT(), 0);

  return 1;
}

//------------------------------------------------------------------------------
int vtkExtractVOI::RequestInformation(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  double in_origin[3];
  double in_spacing[3];
  double out_spacing[3];
  double out_origin[3];
  double direction[9];

  // get the info objects
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation* outInfo = outputVector->GetInformationObject(0);

  int wholeExtent[6], outWholeExt[6];

  inInfo->Get(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), wholeExtent);
  inInfo->Get(vtkDataObject::SPACING(), in_spacing);
  inInfo->Get(vtkDataObject::ORIGIN(), in_origin);

  this->Internal->Initialize(
    this->VOI, wholeExtent, this->SampleRate, (this->IncludeBoundary == 1));

  if (!this->Internal->IsValid())
  {
    vtkDebugMacro("Error while initializing filter.");
    return 0;
  }

  bool hasDirection = false;
  if (inInfo->Has(vtkDataObject::DIRECTION()))
  {
    hasDirection = true;
    inInfo->Get(vtkDataObject::DIRECTION(), direction);
    outInfo->Set(vtkDataObject::DIRECTION(), direction, 9);
  }

  this->Internal->GetOutputWholeExtent(outWholeExt);

  if ((this->SampleRate[0] == 1) && (this->SampleRate[1] == 1) && (this->SampleRate[2] == 1))
  {
    memcpy(out_spacing, in_spacing, sizeof(double) * 3);
    memcpy(out_origin, in_origin, sizeof(double) * 3);
    memcpy(outWholeExt, this->VOI, sizeof(int) * 6);
  } // END if no sub-sampling
  else
  {
    // Calculate out_origin and out_spacing
    for (int dim = 0; dim < 3; ++dim)
    {
      out_spacing[dim] = in_spacing[dim] * this->SampleRate[dim];
      if (!hasDirection)
      {
        out_origin[dim] = in_origin[dim] + this->VOI[dim * 2] * in_spacing[dim];
      }
    }
    if (hasDirection)
    {
      vtkImageData::TransformContinuousIndexToPhysicalPoint(
        this->VOI[0], this->VOI[2], this->VOI[4], in_origin, in_spacing, direction, out_origin);
    }
  } // END else if sub-sampling

  outInfo->Set(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT(), outWholeExt, 6);
  outInfo->Set(vtkDataObject::SPACING(), out_spacing, 3);
  outInfo->Set(vtkDataObject::ORIGIN(), out_origin, 3);

  return 1;
}

//------------------------------------------------------------------------------
int vtkExtractVOI::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // Reset internal helper to the actual extents of the piece we're working on:
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  vtkImageData* inGrid = vtkImageData::GetData(inInfo);
  this->Internal->Initialize(
    this->VOI, inGrid->GetExtent(), this->SampleRate, (this->IncludeBoundary != 0));
  if (!this->Internal->IsValid())
  {
    return 0;
  }

  // Set the output extent -- this is how RequestDataImpl knows what to copy.
  vtkInformation* outInfo = outputVector->GetInformationObject(0);
  vtkImageData* output = vtkImageData::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));
  output->SetExtent(this->Internal->GetOutputWholeExtent());

  return this->RequestDataImpl(inputVector, outputVector) ? 1 : 0;
}

//------------------------------------------------------------------------------
bool vtkExtractVOI::RequestDataImpl(
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  if ((this->SampleRate[0] < 1) || (this->SampleRate[1] < 1) || (this->SampleRate[2] < 1))
  {
    vtkErrorMacro("SampleRate must be >= 1 in all 3 dimensions!");
    return false;
  }

  // get the info objects
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation* outInfo = outputVector->GetInformationObject(0);

  // get the input and output
  vtkImageData* input = vtkImageData::SafeDownCast(inInfo->Get(vtkDataObject::DATA_OBJECT()));
  vtkImageData* output = vtkImageData::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));

  if (input->GetNumberOfPoints() == 0)
  {
    return true;
  }

  // compute output spacing
  double inSpacing[3];
  input->GetSpacing(inSpacing);
  double outSpacing[3];
  outSpacing[0] = inSpacing[0] * this->SampleRate[0];
  outSpacing[1] = inSpacing[1] * this->SampleRate[1];
  outSpacing[2] = inSpacing[2] * this->SampleRate[2];
  output->SetSpacing(outSpacing);

  vtkPointData* pd = input->GetPointData();
  vtkCellData* cd = input->GetCellData();
  vtkPointData* outPD = output->GetPointData();
  vtkCellData* outCD = output->GetCellData();

  int* inExt = input->GetExtent();

  // Compute output data origin:
  double inOrigin[3];
  input->GetOrigin(inOrigin);
  double outMinExt[3];
  bool resampled = false;
  for (int dim = 0; dim < 3; ++dim)
  {
    if (this->SampleRate[dim] == 1)
    {
      // Old origin will work for this dimension since we don't reset extents.
      outMinExt[dim] = inExt[dim * 2];
    }
    else
    {
      resampled = true;
      // Extent minimum is reset to 0, need to update origin.
      // Get the input extent value matching output extent 0 (the origin)
      outMinExt[dim] = this->Internal->GetMappedExtentValue(dim, 0);
    }
  }
  if (resampled)
  {
    // Find the new origin, based on the min extent.
    double outOrigin[3];
    input->TransformContinuousIndexToPhysicalPoint(outMinExt, outOrigin);
    output->SetOrigin(outOrigin);
  }
  else
  {
    output->SetOrigin(inOrigin);
  }
  output->SetDirectionMatrix(input->GetDirectionMatrix());

  vtkDebugMacro(<< "Extracting Grid");
  this->Internal->CopyPointsAndPointData(inExt, output->GetExtent(), pd, nullptr, outPD, nullptr);
  this->Internal->CopyCellData(inExt, output->GetExtent(), cd, outCD);

  return true;
}

//------------------------------------------------------------------------------
void vtkExtractVOI::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "VOI: \n";
  os << indent << "  Imin,Imax: (" << this->VOI[0] << ", " << this->VOI[1] << ")\n";
  os << indent << "  Jmin,Jmax: (" << this->VOI[2] << ", " << this->VOI[3] << ")\n";
  os << indent << "  Kmin,Kmax: (" << this->VOI[4] << ", " << this->VOI[5] << ")\n";

  os << indent << "Sample Rate: (" << this->SampleRate[0] << ", " << this->SampleRate[1] << ", "
     << this->SampleRate[2] << ")\n";

  os << indent << "Include Boundary: " << (this->IncludeBoundary ? "On\n" : "Off\n");
}
VTK_ABI_NAMESPACE_END
