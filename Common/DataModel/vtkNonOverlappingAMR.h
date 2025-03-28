// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkNonOverlappingAMR
 * @brief   A concrete instance of vtkUniformGridAMR to store uniform grids at different
 *  levels of resolution that do not overlap with each other.
 *
 * @sa
 * vtkUniformGridAMR
 */

#ifndef vtkNonOverlappingAMR_h
#define vtkNonOverlappingAMR_h

#include "vtkCommonDataModelModule.h" // For export macro
#include "vtkUniformGridAMR.h"

VTK_ABI_NAMESPACE_BEGIN
class VTKCOMMONDATAMODEL_EXPORT vtkNonOverlappingAMR : public vtkUniformGridAMR
{
public:
  static vtkNonOverlappingAMR* New();
  vtkTypeMacro(vtkNonOverlappingAMR, vtkUniformGridAMR);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Returns object type (see vtkType.h for definitions).
   */
  int GetDataObjectType() VTK_FUTURE_CONST override { return VTK_NON_OVERLAPPING_AMR; }

  /**
   * Retrieve an instance of this class from an information object.
   */
  static vtkNonOverlappingAMR* GetData(vtkInformation* info)
  {
    return vtkNonOverlappingAMR::SafeDownCast(Superclass::GetData(info));
  }
  static vtkNonOverlappingAMR* GetData(vtkInformationVector* v, int i = 0)
  {
    return vtkNonOverlappingAMR::SafeDownCast(Superclass::GetData(v, i));
  }

protected:
  vtkNonOverlappingAMR();
  ~vtkNonOverlappingAMR() override;

private:
  vtkNonOverlappingAMR(const vtkNonOverlappingAMR&) = delete;
  void operator=(const vtkNonOverlappingAMR&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif /* vtkNonOverlappingAMR_h */
