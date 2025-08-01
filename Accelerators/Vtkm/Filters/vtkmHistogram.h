// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-FileCopyrightText: Copyright (c) Kitware, Inc.
// SPDX-FileCopyrightText: Copyright 2012 Sandia Corporation.
// SPDX-License-Identifier: LicenseRef-BSD-3-Clause-Sandia-USGov
/**
 * @class   vtkmHistogram
 * @brief   generate a histogram out of a scalar data
 *
 * vtkmHistogram is a filter that generates a histogram out of a scalar data.
 * The histogram consists of a certain number of bins specified by the user, and
 * the user can fetch the range and bin delta after completion.
 *
 */

#ifndef vtkmHistogram_h
#define vtkmHistogram_h

#include "vtkAcceleratorsVTKmFiltersModule.h" //required for correct export
#include "vtkTableAlgorithm.h"
#include "vtkmlib/vtkmInitializer.h" // Need for initializing viskores

VTK_ABI_NAMESPACE_BEGIN
class vtkDoubleArray;

class VTKACCELERATORSVTKMFILTERS_EXPORT vtkmHistogram : public vtkTableAlgorithm
{
public:
  vtkTypeMacro(vtkmHistogram, vtkTableAlgorithm);
  void PrintSelf(ostream& os, vtkIndent indent) override;
  static vtkmHistogram* New();

  ///@{
  /**
   * Specify number of bins.  Default is 10.
   */
  vtkSetMacro(NumberOfBins, vtkIdType);
  vtkGetMacro(NumberOfBins, vtkIdType);
  ///@}

  ///@{
  /**
   * Specify the range to use to generate the histogram. They are only used when
   * UseCustomBinRanges is set to true.
   */
  vtkSetVector2Macro(CustomBinRange, double);
  vtkGetVector2Macro(CustomBinRange, double);
  ///@}

  ///@{
  /**
   * When set to true, CustomBinRanges will  be used instead of using the full
   * range for the selected array. By default, set to false.
   */
  vtkSetMacro(UseCustomBinRanges, bool);
  vtkGetMacro(UseCustomBinRanges, bool);
  vtkBooleanMacro(UseCustomBinRanges, bool);
  ///@}

  ///@{
  /**
   * Get/Set if first and last bins must be centered around the min and max
   * data. This is only used when UseCustomBinRanges is set to false.
   * Default is false.
   */
  vtkSetMacro(CenterBinsAroundMinAndMax, bool);
  vtkGetMacro(CenterBinsAroundMinAndMax, bool);
  vtkBooleanMacro(CenterBinsAroundMinAndMax, bool);
  ///@}

  ///@{
  /**
   * Return the range used to generate the histogram.
   */
  vtkGetVectorMacro(ComputedRange, double, 2);
  ///@}

  ///@{
  /**
   * Return the bin delta of the computed field.
   */
  vtkGetMacro(BinDelta, double);
  ///@}

protected:
  vtkmHistogram();
  ~vtkmHistogram() override;

  int RequestData(vtkInformation*, vtkInformationVector**, vtkInformationVector*) override;
  int FillInputPortInformation(int port, vtkInformation* info) override;

private:
  vtkmHistogram(const vtkmHistogram&) = delete;
  void operator=(const vtkmHistogram&) = delete;

  void FillBinExtents(vtkDoubleArray* binExtents);

  vtkIdType NumberOfBins;
  double BinDelta;
  double CustomBinRange[2];
  bool UseCustomBinRanges;
  bool CenterBinsAroundMinAndMax;
  double ComputedRange[2];
  vtkmInitializer Initializer;
};

VTK_ABI_NAMESPACE_END
#endif // vtkmHistogram_h
