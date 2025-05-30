// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkImageRFFT.h"

#include "vtkImageData.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkStreamingDemandDrivenPipeline.h"

#include <cmath>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkImageRFFT);

//------------------------------------------------------------------------------
void vtkImageRFFT::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
// This extent of the components changes to real and imaginary values.
int vtkImageRFFT::IterativeRequestInformation(
  vtkInformation* vtkNotUsed(input), vtkInformation* output)
{
  vtkDataObject::SetPointDataActiveScalarInfo(output, VTK_DOUBLE, 2);
  return 1;
}

static void vtkImageRFFTInternalRequestUpdateExtent(
  int* inExt, const int* outExt, const int* wExt, int iteration)
{
  memcpy(inExt, outExt, 6 * sizeof(int));
  inExt[iteration * 2] = wExt[iteration * 2];
  inExt[iteration * 2 + 1] = wExt[iteration * 2 + 1];
}

//------------------------------------------------------------------------------
// This method tells the superclass that the whole input array is needed
// to compute any output region.
int vtkImageRFFT::IterativeRequestUpdateExtent(vtkInformation* input, vtkInformation* output)
{
  int* outExt = output->Get(vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT());
  int* wExt = input->Get(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT());
  int inExt[6];
  vtkImageRFFTInternalRequestUpdateExtent(inExt, outExt, wExt, this->Iteration);
  input->Set(vtkStreamingDemandDrivenPipeline::UPDATE_EXTENT(), inExt, 6);

  return 1;
}

//------------------------------------------------------------------------------
// This templated execute method handles any type input, but the output
// is always doubles.
template <class T>
void vtkImageRFFTExecute(vtkImageRFFT* self, vtkImageData* inData, int inExt[6], T* inPtr,
  vtkImageData* outData, int outExt[6], double* outPtr, int id)
{
  vtkImageComplex* inComplex;
  vtkImageComplex* outComplex;
  vtkImageComplex* pComplex;
  //
  int inMin0, inMax0;
  vtkIdType inInc0, inInc1, inInc2;
  T *inPtr0, *inPtr1, *inPtr2;
  //
  int outMin0, outMax0, outMin1, outMax1, outMin2, outMax2;
  vtkIdType outInc0, outInc1, outInc2;
  double *outPtr0, *outPtr1, *outPtr2;
  //
  int idx0, idx1, idx2, inSize0, numberOfComponents;
  unsigned long count = 0;
  unsigned long target;
  double startProgress;

  startProgress = self->GetIteration() / static_cast<double>(self->GetNumberOfIterations());

  // Reorder axes (The outs here are just placeholders)
  self->PermuteExtent(inExt, inMin0, inMax0, outMin1, outMax1, outMin2, outMax2);
  self->PermuteExtent(outExt, outMin0, outMax0, outMin1, outMax1, outMin2, outMax2);

  // Compute the increments into a local array as `GetIncrements()` introduces
  // a data race on `vtkImageData::Increments`.
  vtkIdType inIncrements[3];
  vtkIdType outIncrements[3];
  inData->GetIncrements(inIncrements);
  outData->GetIncrements(outIncrements);

  self->PermuteIncrements(inIncrements, inInc0, inInc1, inInc2);
  self->PermuteIncrements(outIncrements, outInc0, outInc1, outInc2);

  inSize0 = inMax0 - inMin0 + 1;

  // Input has to have real components at least.
  numberOfComponents = inData->GetNumberOfScalarComponents();
  if (numberOfComponents < 1)
  {
    vtkGenericWarningMacro("No real components");
    return;
  }

  // Allocate the arrays of complex numbers
  inComplex = new vtkImageComplex[inSize0];
  outComplex = new vtkImageComplex[inSize0];

  target = static_cast<unsigned long>(
    (outMax2 - outMin2 + 1) * (outMax1 - outMin1 + 1) * self->GetNumberOfIterations() / 50.0);
  target++;

  // loop over other axes
  inPtr2 = inPtr;
  outPtr2 = outPtr;
  for (idx2 = outMin2; idx2 <= outMax2; ++idx2)
  {
    inPtr1 = inPtr2;
    outPtr1 = outPtr2;
    for (idx1 = outMin1; !self->AbortExecute && idx1 <= outMax1; ++idx1)
    {
      if (!id)
      {
        if (!(count % target))
        {
          self->UpdateProgress(count / (50.0 * target) + startProgress);
        }
        count++;
      }
      // copy into complex numbers
      inPtr0 = inPtr1;
      pComplex = inComplex;
      for (idx0 = inMin0; idx0 <= inMax0; ++idx0)
      {
        pComplex->Real = static_cast<double>(*inPtr0);
        pComplex->Imag = 0.0;
        if (numberOfComponents > 1)
        { // yes we have an imaginary input
          pComplex->Imag = static_cast<double>(inPtr0[1]);
        }
        inPtr0 += inInc0;
        ++pComplex;
      }

      // Call the method that performs the RFFT
      self->ExecuteRfft(inComplex, outComplex, inSize0);

      // copy into output
      outPtr0 = outPtr1;
      pComplex = outComplex + (outMin0 - inMin0);
      for (idx0 = outMin0; idx0 <= outMax0; ++idx0)
      {
        *outPtr0 = pComplex->Real;
        outPtr0[1] = pComplex->Imag;
        outPtr0 += outInc0;
        ++pComplex;
      }
      inPtr1 += inInc1;
      outPtr1 += outInc1;
    }
    inPtr2 += inInc2;
    outPtr2 += outInc2;
  }

  delete[] inComplex;
  delete[] outComplex;
}

//------------------------------------------------------------------------------
// This method is passed input and output Datas, and executes the RFFT
// algorithm to fill the output from the input.
// Not threaded yet.
void vtkImageRFFT::ThreadedRequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* vtkNotUsed(outputVector),
  vtkImageData*** inDataVec, vtkImageData** outDataVec, int outExt[6], int threadId)
{
  vtkImageData* inData = inDataVec[0][0];
  vtkImageData* outData = outDataVec[0];
  void *inPtr, *outPtr;
  int inExt[6];

  int* wExt =
    inputVector[0]->GetInformationObject(0)->Get(vtkStreamingDemandDrivenPipeline::WHOLE_EXTENT());
  vtkImageRFFTInternalRequestUpdateExtent(inExt, outExt, wExt, this->Iteration);
  inPtr = inData->GetScalarPointerForExtent(inExt);
  outPtr = outData->GetScalarPointerForExtent(outExt);

  // this filter expects that the output be doubles.
  if (outData->GetScalarType() != VTK_DOUBLE)
  {
    vtkErrorMacro(<< "Execute: Output must be type double.");
    return;
  }

  // this filter expects input to have 1 or two components
  if (outData->GetNumberOfScalarComponents() != 1 && outData->GetNumberOfScalarComponents() != 2)
  {
    vtkErrorMacro(<< "Execute: Cannot handle more than 2 components");
    return;
  }

  // choose which templated function to call.
  switch (inData->GetScalarType())
  {
    vtkTemplateMacro(vtkImageRFFTExecute(this, inData, inExt, static_cast<VTK_TT*>(inPtr), outData,
      outExt, static_cast<double*>(outPtr), threadId));
    default:
      vtkErrorMacro(<< "Execute: Unknown ScalarType");
      return;
  }
}
VTK_ABI_NAMESPACE_END
