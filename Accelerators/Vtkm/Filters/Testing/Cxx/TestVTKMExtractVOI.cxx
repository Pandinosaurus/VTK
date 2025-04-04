// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkmExtractVOI.h"

#include "vtkActor.h"
#include "vtkDataSetSurfaceFilter.h"
#include "vtkNew.h"
#include "vtkPolyDataMapper.h"
#include "vtkRTAnalyticSource.h"
#include "vtkRegressionTestImage.h"
#include "vtkRenderWindow.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkRenderer.h"
#include "vtkSphereSource.h"
#include "vtkTriangleFilter.h"

#include "vtkImageData.h"

int TestVTKMExtractVOI(int argc, char* argv[])
{
  vtkNew<vtkSphereSource> sphere;
  sphere->SetRadius(2.0);

  vtkNew<vtkPolyDataMapper> sphereMapper;
  sphereMapper->SetInputConnection(sphere->GetOutputPort());

  vtkNew<vtkActor> sphereActor;
  sphereActor->SetMapper(sphereMapper);

  vtkNew<vtkRTAnalyticSource> rt;
  rt->SetWholeExtent(-50, 50, -50, 50, 0, 0);

  vtkNew<vtkmExtractVOI> voi;
  voi->ForceVTKmOn();
  voi->SetInputConnection(rt->GetOutputPort());
  voi->SetVOI(-11, 39, 5, 45, 0, 0);
  voi->SetSampleRate(5, 5, 1);

  // Get rid of ambiguous triangulation issues.
  vtkNew<vtkDataSetSurfaceFilter> surf;
  surf->SetInputConnection(voi->GetOutputPort());

  vtkNew<vtkTriangleFilter> tris;
  tris->SetInputConnection(surf->GetOutputPort());

  vtkNew<vtkPolyDataMapper> mapper;
  mapper->SetInputConnection(tris->GetOutputPort());
  mapper->SetScalarRange(130, 280);

  vtkNew<vtkActor> actor;
  actor->SetMapper(mapper);

  vtkNew<vtkRenderer> renderer;
  renderer->AddActor(actor);
  renderer->AddActor(sphereActor);
  renderer->ResetCamera();

  vtkNew<vtkRenderWindow> renWin;
  renWin->AddRenderer(renderer);

  vtkNew<vtkRenderWindowInteractor> iren;
  iren->SetRenderWindow(renWin);
  iren->Initialize();

  renWin->Render();
  int retVal = vtkRegressionTestImage(renWin);
  if (retVal == vtkRegressionTester::DO_INTERACTOR)
  {
    iren->Start();
  }

  return !retVal;
}
