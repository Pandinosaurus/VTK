// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
//
// This test is unlikely to fail if FXAA isn't working, but can be used to
// quickly check the same scene with/without FXAA enabled.
//

#include "vtkActor.h"
#include "vtkCamera.h"
#include "vtkCameraPass.h"
#include "vtkConeSource.h"
#include "vtkCylinderSource.h"
#include "vtkDefaultPass.h"
#include "vtkDiskSource.h"
#include "vtkLightsPass.h"
#include "vtkLineSource.h"
#include "vtkNew.h"
#include "vtkOpenGLFXAAPass.h"
#include "vtkOpenGLRenderer.h"
#include "vtkPolyDataMapper.h"
#include "vtkProperty.h"
#include "vtkRegressionTestImage.h"
#include "vtkRenderPassCollection.h"
#include "vtkRenderWindow.h"
#include "vtkRenderWindowInteractor.h"
#include "vtkSequencePass.h"
#include "vtkSphereSource.h"
#include "vtkTextActor.h"
#include "vtkTextProperty.h"

namespace
{

void BuildRenderer(vtkRenderer* renderer, int widthBias)
{
  const size_t NUM_LINES = 10;

  vtkNew<vtkLineSource> lines[NUM_LINES];
  vtkNew<vtkPolyDataMapper> mappers[NUM_LINES];
  vtkNew<vtkActor> actors[NUM_LINES];
  for (size_t i = 0; i < NUM_LINES; ++i)
  {
    double c = static_cast<double>(2 * i) / static_cast<double>(NUM_LINES - 1) - 1.;
    lines[i]->SetPoint1(-1, c, 0.);
    lines[i]->SetPoint2(1, -c, 0.);

    mappers[i]->SetInputConnection(lines[i]->GetOutputPort());

    actors[i]->SetMapper(mappers[i]);
    actors[i]->GetProperty()->SetColor(0., 1., 0.);
    actors[i]->GetProperty()->SetRepresentationToWireframe();
    actors[i]->GetProperty()->SetLineWidth(((i + widthBias) % 2) ? 1 : 3);
    renderer->AddActor(actors[i]);
  }

  vtkNew<vtkSphereSource> sphere;
  sphere->SetCenter(0., 0.6, 0.);
  sphere->SetThetaResolution(80);
  sphere->SetPhiResolution(80);
  sphere->SetRadius(0.4);
  vtkNew<vtkPolyDataMapper> sphereMapper;
  sphereMapper->SetInputConnection(sphere->GetOutputPort());
  vtkNew<vtkActor> sphereActor;
  sphereActor->SetMapper(sphereMapper);
  sphereActor->GetProperty()->SetColor(0.9, 0.4, 0.2);
  sphereActor->GetProperty()->SetAmbient(.6);
  sphereActor->GetProperty()->SetDiffuse(.4);
  renderer->AddActor(sphereActor);

  vtkNew<vtkConeSource> cone;
  cone->SetCenter(0., 0.5, -0.5);
  cone->SetResolution(160);
  cone->SetRadius(.9);
  cone->SetHeight(0.9);
  cone->SetDirection(0., -1., 0.);
  vtkNew<vtkPolyDataMapper> coneMapper;
  coneMapper->SetInputConnection(cone->GetOutputPort());
  vtkNew<vtkActor> coneActor;
  coneActor->SetMapper(coneMapper);
  coneActor->GetProperty()->SetColor(0.9, .6, 0.8);
  coneActor->GetProperty()->SetAmbient(.6);
  coneActor->GetProperty()->SetDiffuse(.4);
  renderer->AddActor(coneActor);

  vtkNew<vtkDiskSource> disk;
  disk->SetCircumferentialResolution(80);
  disk->SetInnerRadius(0);
  disk->SetOuterRadius(0.5);
  vtkNew<vtkPolyDataMapper> diskMapper;
  diskMapper->SetInputConnection(disk->GetOutputPort());
  vtkNew<vtkActor> diskActor;
  diskActor->SetPosition(0., -0.5, -0.5);
  diskActor->SetMapper(diskMapper);
  diskActor->GetProperty()->SetColor(.3, .1, .4);
  diskActor->GetProperty()->SetAmbient(.6);
  diskActor->GetProperty()->SetDiffuse(.4);
  renderer->AddActor(diskActor);

  vtkNew<vtkCylinderSource> cyl;
  cyl->SetCenter(0., -.5, 0.);
  cyl->SetHeight(.6);
  cyl->SetRadius(.2);
  cyl->SetResolution(80);
  vtkNew<vtkPolyDataMapper> cylMapper;
  cylMapper->SetInputConnection(cyl->GetOutputPort());
  vtkNew<vtkActor> cylActor;
  cylActor->SetOrigin(cyl->GetCenter());
  cylActor->RotateWXYZ(35, -0.2, 0., 1.);
  cylActor->SetMapper(cylMapper);
  cylActor->GetProperty()->SetColor(0.3, .9, .4);
  cylActor->GetProperty()->SetAmbient(.6);
  cylActor->GetProperty()->SetDiffuse(.4);
  renderer->AddActor(cylActor);

  renderer->SetBackground(0., 0., 0.);
  renderer->GetActiveCamera()->ParallelProjectionOn();
  renderer->ResetCamera();
  renderer->ResetCameraClippingRange();
  renderer->GetActiveCamera()->SetParallelScale(0.9);
}

} // end anon namespace

int TestFXAAPass(int argc, char* argv[])
{
  vtkNew<vtkRenderWindowInteractor> iren;
  vtkNew<vtkRenderWindow> renWin;
  renWin->SetMultiSamples(0);
  iren->SetRenderWindow(renWin);

  vtkNew<vtkRenderer> renderer;
  vtkNew<vtkRenderer> rendererFXAA;

  // custom passes
  vtkNew<vtkCameraPass> cameraP;
  vtkNew<vtkSequencePass> seq;
  vtkNew<vtkDefaultPass> defaultP;
  vtkNew<vtkLightsPass> lights;
  vtkNew<vtkOpenGLFXAAPass> fxaa;
  fxaa->SetFXAAOptions(rendererFXAA->GetFXAAOptions());

  vtkNew<vtkRenderPassCollection> passes;
  passes->AddItem(lights);
  passes->AddItem(defaultP);
  seq->SetPasses(passes);
  cameraP->SetDelegatePass(seq);

  fxaa->SetDelegatePass(cameraP);

  vtkOpenGLRenderer::SafeDownCast(rendererFXAA)->SetPass(fxaa);

  vtkNew<vtkTextActor> label;
  label->SetInput("No FXAA");
  label->GetTextProperty()->SetFontSize(20);
  label->GetTextProperty()->SetJustificationToCentered();
  label->GetTextProperty()->SetVerticalJustificationToBottom();
  label->SetPosition(85, 10);
  renderer->AddViewProp(label);

  vtkNew<vtkTextActor> labelFXAA;
  labelFXAA->SetInput("FXAA");
  labelFXAA->GetTextProperty()->SetFontSize(20);
  labelFXAA->GetTextProperty()->SetJustificationToCentered();
  labelFXAA->GetTextProperty()->SetVerticalJustificationToBottom();
  labelFXAA->SetPosition(85, 10);
  rendererFXAA->AddViewProp(labelFXAA);

  renderer->SetViewport(0., 0., .5, 1.);
  BuildRenderer(renderer, 0);
  renWin->AddRenderer(renderer);

  rendererFXAA->SetViewport(.5, 0., 1., 1.);
  BuildRenderer(rendererFXAA, 1);
  renWin->AddRenderer(rendererFXAA);

  renWin->SetSize(1000, 500);
  renWin->Render();

  int retVal = vtkRegressionTestImage(renWin);
  if (retVal == vtkRegressionTester::DO_INTERACTOR)
  {
    iren->Start();
  }
  return !retVal;
}
