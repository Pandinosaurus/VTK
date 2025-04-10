// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-FileCopyrightText: Copyright 2003 Sandia Corporation
// SPDX-License-Identifier: LicenseRef-BSD-3-Clause-Sandia-USGov

#include "vtkOpenGLProjectedTetrahedraMapper.h"

#include "vtkCamera.h"
#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkCellIterator.h"
#include "vtkFloatArray.h"
#include "vtkIdList.h"
#include "vtkIdTypeArray.h"
#include "vtkInformation.h"
#include "vtkMath.h"
#include "vtkMatrix3x3.h"
#include "vtkMatrix4x4.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkOpenGLActor.h"
#include "vtkOpenGLCamera.h"
#include "vtkOpenGLError.h"
#include "vtkOpenGLFramebufferObject.h"
#include "vtkOpenGLIndexBufferObject.h"
#include "vtkOpenGLRenderUtilities.h"
#include "vtkOpenGLRenderWindow.h"
#include "vtkOpenGLShaderCache.h"
#include "vtkOpenGLState.h"
#include "vtkOpenGLVertexArrayObject.h"
#include "vtkOpenGLVertexBufferObject.h"
#include "vtkPointData.h"
#include "vtkRenderer.h"
#include "vtkShaderProgram.h"
#include "vtkSmartPointer.h"
#include "vtkTextureObject.h"
#include "vtkTimerLog.h"
#include "vtkUnsignedCharArray.h"
#include "vtkUnstructuredGrid.h"
#include "vtkVisibilitySort.h"
#include "vtkVolume.h"
#include "vtkVolumeProperty.h"

#include <algorithm>
#include <cmath>
#include <string>

// bring in shader code
#include "vtkglProjectedTetrahedraFS.h"
#include "vtkglProjectedTetrahedraVS.h"

VTK_ABI_NAMESPACE_BEGIN
namespace
{
void annotate(const std::string& message)
{
  vtkOpenGLRenderUtilities::MarkDebugEvent(message);
}

class scoped_annotate
{
  std::string Message;

public:
  scoped_annotate(const std::string& message)
    : Message(message)
  {
    annotate("start " + message);
  }
  ~scoped_annotate() { annotate("end " + this->Message); }
};
}

static int tet_edges[6][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 }, { 0, 3 }, { 1, 3 }, { 2, 3 } };

const int SqrtTableSize = 2048;

//------------------------------------------------------------------------------
class vtkOpenGLProjectedTetrahedraMapper::vtkInternals
{
public:
  bool IntermixedGeometryWarningIssued = false;
};

//------------------------------------------------------------------------------
vtkStandardNewMacro(vtkOpenGLProjectedTetrahedraMapper);

//------------------------------------------------------------------------------
vtkOpenGLProjectedTetrahedraMapper::vtkOpenGLProjectedTetrahedraMapper()
{
  this->Internals = new vtkInternals();
  this->TransformedPoints = vtkFloatArray::New();
  this->Colors = vtkUnsignedCharArray::New();
  this->LastProperty = nullptr;
  this->MaxCellSize = 0;
  this->GaveError = 0;
  this->SqrtTable = new float[SqrtTableSize];
  this->SqrtTableBias = 0.0;
  this->Initialized = false;
  this->CurrentFBOWidth = -1;
  this->CurrentFBOHeight = -1;
  this->FloatingPointFrameBufferResourcesAllocated = false;
  this->Framebuffer = vtkOpenGLFramebufferObject::New();
  this->UseFloatingPointFrameBuffer = true;
  this->CanDoFloatingPointFrameBuffer = false;
  this->HasHardwareSupport = false;
  this->VBO = vtkOpenGLVertexBufferObject::New();
}

//------------------------------------------------------------------------------
vtkOpenGLProjectedTetrahedraMapper::~vtkOpenGLProjectedTetrahedraMapper()
{
  this->ReleaseGraphicsResources(nullptr);
  this->TransformedPoints->Delete();
  this->Colors->Delete();
  delete[] this->SqrtTable;
  this->VBO->Delete();
  this->Framebuffer->Delete();
  delete this->Internals;
  this->Internals = nullptr;
}

//------------------------------------------------------------------------------
void vtkOpenGLProjectedTetrahedraMapper::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "VisibilitySort: " << this->VisibilitySort << endl;
  os << indent
     << "UseFloatingPointFrameBuffer: " << (this->UseFloatingPointFrameBuffer ? "True" : "False")
     << endl;
}

//------------------------------------------------------------------------------
bool vtkOpenGLProjectedTetrahedraMapper::IsSupported(vtkRenderWindow* rwin)
{
  vtkOpenGLRenderWindow* context = vtkOpenGLRenderWindow::SafeDownCast(rwin);
  if (!context)
  {
    vtkErrorMacro(<< "Support for " << rwin->GetClassName() << " not implemented");
    return false;
  }

  // use render to FBO when it's supported
  this->CanDoFloatingPointFrameBuffer = false;
  if (this->UseFloatingPointFrameBuffer)
  {
    this->CanDoFloatingPointFrameBuffer = true;
  }

  return true;
}

//------------------------------------------------------------------------------
void vtkOpenGLProjectedTetrahedraMapper::Initialize(vtkRenderer* renderer)
{
  if (this->Initialized)
  {
    return;
  }

  this->Initialized = true;

  vtkOpenGLRenderWindow* renwin = vtkOpenGLRenderWindow::SafeDownCast(renderer->GetRenderWindow());
  this->HasHardwareSupport = renwin != nullptr && this->IsSupported(renwin);
  if (!this->HasHardwareSupport)
  {
    // this is an error since there's no fallback.
    vtkErrorMacro("The required extensions are not supported.");
  }
}

//------------------------------------------------------------------------------
bool vtkOpenGLProjectedTetrahedraMapper::AllocateFOResources(vtkRenderer* r)
{
  vtkOpenGLClearErrorMacro();
  scoped_annotate annotator("PTM::AllocateFOResources");

  const int* size = r->GetSize();

  if (this->UseFloatingPointFrameBuffer && this->CanDoFloatingPointFrameBuffer &&
    (!this->FloatingPointFrameBufferResourcesAllocated || (size[0] != this->CurrentFBOWidth) ||
      (size[0] != this->CurrentFBOHeight)))
  {
    vtkOpenGLRenderWindow* rw = static_cast<vtkOpenGLRenderWindow*>(r->GetRenderWindow());

    if (!this->FloatingPointFrameBufferResourcesAllocated)
    {
      // determine if we have MSAA
      GLint winSampleBuffers = 0;
      glGetIntegerv(GL_SAMPLE_BUFFERS, &winSampleBuffers);
      GLint winSamples = 0;
      if (winSampleBuffers)
      {
        glGetIntegerv(GL_SAMPLES, &winSamples);
      }

      int dsize = rw->GetDepthBufferSize();
      if (dsize == 0)
      {
        dsize = 24;
      }

      vtkOpenGLFramebufferObject* fo = this->Framebuffer;
      fo->SetContext(rw);
      rw->GetState()->PushFramebufferBindings();

      const char* desc;

      // if we failed to get a framebuffer and we wanted
      // multisamples, then try again without multisamples
      if (!fo->PopulateFramebuffer(size[0], size[1],
            true,         // use textures
            1, VTK_FLOAT, // 1 color buffer of float
            true, dsize,  // yes depth buffer
            winSamples)   // possibly multisampled
        && winSamples > 0)
      {
        fo->PopulateFramebuffer(size[0], size[1],
          true,         // use textures
          1, VTK_FLOAT, // 1 color buffer of float
          true, dsize,  // yes depth buffer
          0);           // no multisamples
      }

      this->FloatingPointFrameBufferResourcesAllocated = true;

      if (!vtkOpenGLFramebufferObject::GetFrameBufferStatus(
            vtkOpenGLFramebufferObject::GetDrawMode(), desc))
      {
        vtkWarningMacro("Missing FBO support. The algorithm may produce visual artifacts.");
        this->CanDoFloatingPointFrameBuffer = false;
        rw->GetState()->PopFramebufferBindings();
        return false;
      }
      rw->GetState()->PopFramebufferBindings();
      this->CanDoFloatingPointFrameBuffer = true;
    }
    else
    {
      // need resize
      vtkOpenGLFramebufferObject* fo = this->Framebuffer;
      rw->GetState()->PushFramebufferBindings();
      fo->Bind();
      fo->Resize(size[0], size[1]);
      this->Framebuffer->UnBind();
      rw->GetState()->PopFramebufferBindings();
    }
    this->CurrentFBOWidth = size[0];
    this->CurrentFBOHeight = size[1];
  }
  return true;
}

//------------------------------------------------------------------------------
void vtkOpenGLProjectedTetrahedraMapper::ReleaseGraphicsResources(vtkWindow* win)
{
  this->Initialized = false;

  if (this->FloatingPointFrameBufferResourcesAllocated)
  {
    this->FloatingPointFrameBufferResourcesAllocated = false;
    this->Framebuffer->ReleaseGraphicsResources(win);
  }

  this->VBO->ReleaseGraphicsResources();
  this->Tris.ReleaseGraphicsResources(win);

  this->Superclass::ReleaseGraphicsResources(win);
}

//------------------------------------------------------------------------------
void vtkOpenGLProjectedTetrahedraMapper::Render(vtkRenderer* renderer, vtkVolume* volume)
{
  vtkOpenGLClearErrorMacro();
  scoped_annotate annotator("PTM::Render");

  // Disable FP-FBO support on Apple with ATI. See paraview/paraview#17303
#ifdef __APPLE__
  if (this->UseFloatingPointFrameBuffer)
  {
    std::string glVendor = (const char*)glGetString(GL_VENDOR);
    if (glVendor.find("ATI") != std::string::npos)
    {
      vtkWarningMacro("Disabling floating point framebuffer: Unsupported "
                      "hardware. Volume rendering will continue, though"
                      "artifacts may be present.");
      this->UseFloatingPointFrameBufferOff();
    }
  }
#endif

  // load required extensions
  this->Initialize(renderer);

  if (!this->HasHardwareSupport)
  {
    return;
  }

  // make sure our shader program is loaded and ready to go
  vtkOpenGLRenderWindow* renWin = vtkOpenGLRenderWindow::SafeDownCast(renderer->GetRenderWindow());

  if (renWin == nullptr)
  {
    vtkErrorMacro("Invalid vtkOpenGLRenderWindow");
  }

  vtkInformation* volumeKeys = volume->GetPropertyKeys();
  if (volumeKeys && volumeKeys->Has(vtkOpenGLActor::GLDepthMaskOverride()))
  {
    if (!this->Internals->IntermixedGeometryWarningIssued)
    {
      vtkWarningMacro(
        "Intermixing translucent polygonal data with unstructured grid volumes is not supported!"
        "\nEither set opacity to 1.0 for polydata in the view or resample the unstructured "
        "grid to image data and use the ray cast mapper.");
      this->Internals->IntermixedGeometryWarningIssued = true;
    }
  }

  vtkUnstructuredGridBase* input = this->GetInput();
  vtkVolumeProperty* property = volume->GetProperty();

  // Check to see if input changed.
  if ((this->InputAnalyzedTime < this->MTime) || (this->InputAnalyzedTime < input->GetMTime()))
  {
    this->GaveError = 0;
    float max_cell_size2 = 0;

    if (input->GetNumberOfCells() == 0)
    {
      // Apparently, the input has no cells.  Just do nothing.
      return;
    }

    vtkSmartPointer<vtkCellIterator> cellIter =
      vtkSmartPointer<vtkCellIterator>::Take(input->NewCellIterator());
    for (cellIter->InitTraversal(); !cellIter->IsDoneWithTraversal(); cellIter->GoToNextCell())
    {
      vtkIdType npts = cellIter->GetNumberOfPoints();
      if (npts != 4)
      {
        if (!this->GaveError)
        {
          vtkErrorMacro("Encountered non-tetrahedra cell!");
          this->GaveError = 1;
        }
        continue;
      }
      vtkIdType* pts = cellIter->GetPointIds()->GetPointer(0);
      for (int j = 0; j < 6; j++)
      {
        double p1[3], p2[3];
        input->GetPoint(pts[tet_edges[j][0]], p1);
        input->GetPoint(pts[tet_edges[j][1]], p2);
        float size2 = (float)vtkMath::Distance2BetweenPoints(p1, p2);
        if (size2 > max_cell_size2)
        {
          max_cell_size2 = size2;
        }
      }
    }

    this->MaxCellSize = (float)sqrt(max_cell_size2);

    // Build a sqrt lookup table for measuring distances.  During perspective
    // modes we have to take a lot of square roots, and a table is much faster
    // than calling the sqrt function.
    this->SqrtTableBias = (SqrtTableSize - 1) / max_cell_size2;
    for (int i = 0; i < SqrtTableSize; i++)
    {
      this->SqrtTable[i] = (float)sqrt(i / this->SqrtTableBias);
    }

    this->InputAnalyzedTime.Modified();
  }

  if (renderer->GetRenderWindow()->CheckAbortStatus() || this->GaveError)
  {
    vtkOpenGLCheckErrorMacro("failed during Render");
    return;
  }

  if (renderer->GetRenderWindow()->CheckAbortStatus())
  {
    vtkOpenGLCheckErrorMacro("failed during Render");
    return;
  }

  // Check to see if we need to remap colors.
  if ((this->ColorsMappedTime < this->MTime) || (this->ColorsMappedTime < input->GetMTime()) ||
    (this->LastProperty != property) || (this->ColorsMappedTime < property->GetMTime()))
  {
    vtkDataArray* scalars = vtkOpenGLProjectedTetrahedraMapper::GetScalars(input, this->ScalarMode,
      this->ArrayAccessMode, this->ArrayId, this->ArrayName, this->UsingCellColors);
    if (!scalars)
    {
      vtkErrorMacro(<< "Can't use projected tetrahedra without scalars!");
      vtkOpenGLCheckErrorMacro("failed during Render");
      return;
    }

    vtkProjectedTetrahedraMapper::MapScalarsToColors(this->Colors, property, scalars);

    this->ColorsMappedTime.Modified();
    this->LastProperty = property;
  }
  if (renderer->GetRenderWindow()->CheckAbortStatus())
  {
    vtkOpenGLCheckErrorMacro("failed during Render");
    return;
  }

  this->Timer->StartTimer();

  this->ProjectTetrahedra(renderer, volume, renWin);

  this->Timer->StopTimer();
  this->TimeToDraw = this->Timer->GetElapsedTime();
  vtkOpenGLCheckErrorMacro("failed after Render");
}

//------------------------------------------------------------------------------

float vtkOpenGLProjectedTetrahedraMapper::GetCorrectedDepth(float x, float y, float z1, float z2,
  const float inverse_projection_mat[16], int use_linear_depth_correction,
  float linear_depth_correction)
{
  if (use_linear_depth_correction)
  {
    float depth = linear_depth_correction * (z1 - z2);
    if (depth < 0)
      depth = -depth;
    return depth;
  }
  else
  {
    float eye1[3], eye2[3], invw;

    // This code does the same as the commented code above, but also collects
    // common arithmetic between the two matrix x vector operations.  An
    // optimizing compiler may or may not pick up on that.
    float common[4];

    common[0] =
      (inverse_projection_mat[0] * x + inverse_projection_mat[4] * y + inverse_projection_mat[12]);
    common[1] =
      (inverse_projection_mat[1] * x + inverse_projection_mat[5] * y + inverse_projection_mat[13]);
    common[2] = (inverse_projection_mat[2] * x + inverse_projection_mat[6] * y +
      inverse_projection_mat[10] * z1 + inverse_projection_mat[14]);
    common[3] =
      (inverse_projection_mat[3] * x + inverse_projection_mat[7] * y + inverse_projection_mat[15]);

    invw = 1 / (common[3] + inverse_projection_mat[11] * z1);
    eye1[0] = invw * (common[0] + inverse_projection_mat[8] * z1);
    eye1[1] = invw * (common[1] + inverse_projection_mat[9] * z1);
    eye1[2] = invw * (common[2] + inverse_projection_mat[10] * z1);

    invw = 1 / (common[3] + inverse_projection_mat[11] * z2);
    eye2[0] = invw * (common[0] + inverse_projection_mat[8] * z2);
    eye2[1] = invw * (common[1] + inverse_projection_mat[9] * z2);
    eye2[2] = invw * (common[2] + inverse_projection_mat[10] * z2);

    float dist2 = vtkMath::Distance2BetweenPoints(eye1, eye2);
    return this->SqrtTable[(int)(dist2 * this->SqrtTableBias)];
  }
}

//------------------------------------------------------------------------------
void vtkOpenGLProjectedTetrahedraMapper::ProjectTetrahedra(
  vtkRenderer* renderer, vtkVolume* volume, vtkOpenGLRenderWindow* window)
{
  vtkOpenGLClearErrorMacro();
  scoped_annotate annotator("PTM::ProjectTetrahedra");

  // after mucking about with FBO bindings be sure
  // we're saving the default fbo attributes/blend function
  this->AllocateFOResources(renderer);

  vtkOpenGLFramebufferObject* fo = nullptr;
  vtkOpenGLRenderWindow* renderWindow =
    static_cast<vtkOpenGLRenderWindow*>(renderer->GetRenderWindow());
  vtkOpenGLState* ostate = renderWindow->GetState();

  // Copy existing Depth/Color buffers to FO
  if (this->UseFloatingPointFrameBuffer && this->CanDoFloatingPointFrameBuffer)
  {
    scoped_annotate annotator2("PTM::UseFloatingPointFrameBuffer");
    fo = this->Framebuffer;

    // bind draw+read to set it up
    ostate->PushFramebufferBindings();
    fo->Bind(vtkOpenGLFramebufferObject::GetDrawMode());
    fo->ActivateDrawBuffer(0);

    if (!fo->CheckFrameBufferStatus(vtkOpenGLFramebufferObject::GetDrawMode()))
    {
      vtkErrorMacro("FO is incomplete ");
    }

    auto srcDepthTexture =
      renderWindow->GetRenderFramebuffer()->GetDepthAttachmentAsTextureObject();
    auto dstDepthTexture = fo->GetDepthAttachmentAsTextureObject();
    const auto srcDepthFormat = srcDepthTexture->GetFormat(0, 0, false);
    const auto dstDepthFormat = dstDepthTexture->GetFormat(0, 0, false);
    // We need to treat depth buffer blitting specially because depth buffer formats may not
    // be compatible between the FBO used in this class and the renderwindow's FBO.
    if (srcDepthFormat == dstDepthFormat)
    {
      // compatible, blit color and depth attachments.
      ostate->vtkglBlitFramebuffer(0, 0, this->CurrentFBOWidth, this->CurrentFBOHeight, 0, 0,
        this->CurrentFBOWidth, this->CurrentFBOHeight, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT,
        GL_NEAREST);
    }
    else
    {
      // incompatible, blit only color attachment
      ostate->vtkglBlitFramebuffer(0, 0, this->CurrentFBOWidth, this->CurrentFBOHeight, 0, 0,
        this->CurrentFBOWidth, this->CurrentFBOHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
      // depth values are sampled from srcDepthTexture into our framebuffer's depth attachment.
      renderWindow->TextureDepthBlit(srcDepthTexture);
    }

    vtkOpenGLCheckErrorMacro("failed at glBlitFramebuffer");
  }

  // has something changed that would require us to recreate the shader?
  if (!this->Tris.Program)
  {
    // build the shader source code
    std::string VSSource = vtkglProjectedTetrahedraVS;
    std::string FSSource = vtkglProjectedTetrahedraFS;
    std::string GSSource;

    // compile and bind it if needed
    vtkShaderProgram* newShader = window->GetShaderCache()->ReadyShaderProgram(
      VSSource.c_str(), FSSource.c_str(), GSSource.c_str());

    // if the shader changed reinitialize the VAO
    if (newShader != this->Tris.Program)
    {
      this->Tris.Program = newShader;
      this->Tris.VAO->ShaderProgramChanged(); // reset the VAO as the shader has changed
    }

    this->Tris.ShaderSourceTime.Modified();
  }
  else
  {
    window->GetShaderCache()->ReadyShaderProgram(this->Tris.Program);
  }

  // TODO:
  // There are some caching optimizations that could be used
  // here to skip various expensive operations (eg sorting
  // cells could be skipped if input data and MVP matrices
  // haven't changed).

  vtkUnstructuredGridBase* input = this->GetInput();
  this->VisibilitySort->SetInput(input);
  this->VisibilitySort->SetDirectionToBackToFront();
  volume->GetModelToWorldMatrix(this->tmpMat);
  this->VisibilitySort->SetModelTransform(this->tmpMat);
  this->VisibilitySort->SetCamera(renderer->GetActiveCamera());
  this->VisibilitySort->SetMaxCellsReturned(1000);

  this->VisibilitySort->InitTraversal();

  if (renderer->GetRenderWindow()->CheckAbortStatus())
  {
    if (fo)
    {
      ostate->PopFramebufferBindings();
    }
    return;
  }

  vtkMatrix4x4* wcdc;
  vtkMatrix4x4* wcvc;
  vtkMatrix3x3* norms;
  vtkMatrix4x4* vcdc;
  vtkOpenGLCamera* cam = (vtkOpenGLCamera*)(renderer->GetActiveCamera());
  cam->GetKeyMatrices(renderer, wcvc, norms, vcdc, wcdc);
  float projection_mat[16];
  for (int i = 0; i < 4; ++i)
  {
    for (int j = 0; j < 4; ++j)
    {
      projection_mat[i * 4 + j] = vcdc->GetElement(i, j);
    }
  }

  float modelview_mat[16];
  if (!volume->GetIsIdentity())
  {
    volume->GetModelToWorldMatrix(tmpMat);
    tmpMat2->DeepCopy(wcvc);
    tmpMat2->Transpose();
    vtkMatrix4x4::Multiply4x4(tmpMat2, tmpMat, tmpMat);
    tmpMat->Transpose();
    for (int i = 0; i < 4; ++i)
    {
      for (int j = 0; j < 4; ++j)
      {
        modelview_mat[i * 4 + j] = tmpMat->GetElement(i, j);
      }
    }
  }
  else
  {
    for (int i = 0; i < 4; ++i)
    {
      for (int j = 0; j < 4; ++j)
      {
        modelview_mat[i * 4 + j] = wcvc->GetElement(i, j);
      }
    }
  }

  // Get the inverse projection matrix so that we can convert distances in
  // clipping space to distances in world or eye space.
  float inverse_projection_mat[16];
  float linear_depth_correction = 1;
  int use_linear_depth_correction;
  double tmp_mat[16];

  // VTK's matrix functions use doubles.
  std::copy(projection_mat, projection_mat + 16, tmp_mat);
  // VTK and OpenGL store their matrices differently.  Correct.
  vtkMatrix4x4::Transpose(tmp_mat, tmp_mat);
  // Take the inverse.
  vtkMatrix4x4::Invert(tmp_mat, tmp_mat);
  // Restore back to OpenGL form.
  vtkMatrix4x4::Transpose(tmp_mat, tmp_mat);
  // Copy back to float for faster computation.
  std::copy(tmp_mat, tmp_mat + 16, inverse_projection_mat);

  // Check to see if we can just do a linear depth correction from clipping
  // space to eye space.
  use_linear_depth_correction = ((projection_mat[3] == 0.0) && (projection_mat[7] == 0.0) &&
    (projection_mat[11] == 0.0) && (projection_mat[15] == 1.0));
  if (use_linear_depth_correction)
  {
    float pos1[3], *pos2;

    pos1[0] = inverse_projection_mat[8] + inverse_projection_mat[12];
    pos1[1] = inverse_projection_mat[9] + inverse_projection_mat[13];
    pos1[2] = inverse_projection_mat[10] + inverse_projection_mat[14];

    pos2 = inverse_projection_mat + 12;

    linear_depth_correction = sqrt(vtkMath::Distance2BetweenPoints(pos1, pos2));
  }
  // Transform all the points.
  vtkProjectedTetrahedraMapper::TransformPoints(
    input->GetPoints(), projection_mat, modelview_mat, this->TransformedPoints);
  float* points = this->TransformedPoints->GetPointer(0);

  if (renderer->GetRenderWindow()->CheckAbortStatus())
  {
    if (fo)
    {
      ostate->PopFramebufferBindings();
    }
    return;
  }

  ostate->vtkglDepthMask(GL_FALSE);
  ostate->vtkglEnable(GL_DEPTH_TEST);

  ostate->vtkglDisable(GL_CULL_FACE);
  vtkOpenGLState::ScopedglBlendFuncSeparate bfsaver(ostate);

  ostate->vtkglBlendFuncSeparate(
    GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

  float unit_distance = volume->GetProperty()->GetScalarOpacityUnitDistance();

  // build the VBO and IBOs, we do these in chunks as based on
  // the settings of the VisibilitySort tclass
  this->VBO->SetStride(6 * sizeof(float));

  // Establish vertex arrays.
  // tets have 4 points, 5th point here is used
  // to insert a point in case of intersections
  float tet_points[5 * 3] = { 0.0f };
  unsigned char tet_colors[5 * 3] = { 0 };
  float tet_texcoords[5 * 2] = { 0.0f };

  unsigned char* colors = this->Colors->GetPointer(0);
  vtkIdType totalnumcells = input->GetNumberOfCells();
  vtkIdType numcellsrendered = 0;
  vtkNew<vtkIdList> cellPointIds;

  std::vector<float> packedVBO;
  packedVBO.reserve(6 * 5 * this->VisibilitySort->GetMaxCellsReturned());

  std::vector<unsigned int> indexArray;
  indexArray.reserve(3 * 4 * this->VisibilitySort->GetMaxCellsReturned());

  double progressNext = 0.0;

  // Let's do it!
  for (vtkIdTypeArray* sorted_cell_ids = this->VisibilitySort->GetNextCells();
       sorted_cell_ids != nullptr; sorted_cell_ids = this->VisibilitySort->GetNextCells())
  {
    const double progress = static_cast<double>(numcellsrendered) / totalnumcells;
    if (progress >= progressNext)
    {
      this->GLSafeUpdateProgress(progress, window);
      progressNext += 0.1; // we report progress in 10% increments to avoid
                           // over-reporting .
    }

    if (renderer->GetRenderWindow()->CheckAbortStatus())
    {
      break;
    }
    vtkIdType* cell_ids = sorted_cell_ids->GetPointer(0);
    vtkIdType num_cell_ids = sorted_cell_ids->GetNumberOfTuples();

    packedVBO.resize(6 * 5 * num_cell_ids);
    std::vector<float>::iterator it = packedVBO.begin();
    int numPts = 0;
    indexArray.resize(0);

    for (vtkIdType i = 0; i < num_cell_ids; i++)
    {
      vtkIdType cell = cell_ids[i];
      input->GetCellPoints(cell, cellPointIds);
      int j;

      // Get the data for the tetrahedra.
      for (j = 0; j < 4; j++)
      {
        // Assuming we only have tetrahedra, each entry in cells has 5
        // components.
        const float* p = points + 3 * cellPointIds->GetId(j);
        tet_points[j * 3 + 0] = p[0];
        tet_points[j * 3 + 1] = p[1];
        tet_points[j * 3 + 2] = p[2];

        const unsigned char* c;
        if (this->UsingCellColors)
        {
          c = colors + 4 * cell;
        }
        else
        {
          c = colors + 4 * cellPointIds->GetId(j);
        }

        tet_colors[j * 3 + 0] = c[0];
        tet_colors[j * 3 + 1] = c[1];
        tet_colors[j * 3 + 2] = c[2];

        tet_texcoords[j * 2 + 0] = static_cast<float>(c[3]) / 255.0f;
        tet_texcoords[j * 2 + 1] = 0;
      }

      // Do not render this cell if it is outside of the cutting planes.  For
      // most planes, cut if all points are outside.  For the near plane, cut if
      // any points are outside because things can go very wrong if one of the
      // points is behind the view.
      if (((tet_points[0 * 3 + 0] > 1.0f) && (tet_points[1 * 3 + 0] > 1.0f) &&
            (tet_points[2 * 3 + 0] > 1.0f) && (tet_points[3 * 3 + 0] > 1.0f)) ||
        ((tet_points[0 * 3 + 0] < -1.0f) && (tet_points[1 * 3 + 0] < -1.0f) &&
          (tet_points[2 * 3 + 0] < -1.0f) && (tet_points[3 * 3 + 0] < -1.0f)) ||
        ((tet_points[0 * 3 + 1] > 1.0f) && (tet_points[1 * 3 + 1] > 1.0f) &&
          (tet_points[2 * 3 + 1] > 1.0f) && (tet_points[3 * 3 + 1] > 1.0f)) ||
        ((tet_points[0 * 3 + 1] < -1.0f) && (tet_points[1 * 3 + 1] < -1.0f) &&
          (tet_points[2 * 3 + 1] < -1.0f) && (tet_points[3 * 3 + 1] < -1.0f)) ||
        ((tet_points[0 * 3 + 2] > 1.0f) && (tet_points[1 * 3 + 2] > 1.0f) &&
          (tet_points[2 * 3 + 2] > 1.0f) && (tet_points[3 * 3 + 2] > 1.0f)) ||
        ((tet_points[0 * 3 + 2] < -1.0f) || (tet_points[1 * 3 + 2] < -1.0f) ||
          (tet_points[2 * 3 + 2] < -1.0f) || (tet_points[3 * 3 + 2] < -1.0f)))
      {
        continue;
      }

      // The classic PT algorithm uses face normals to determine the
      // projection class and then do calculations individually.  However,
      // Wylie 2002 shows how to use the intersection of two segments to
      // calculate the depth of the thick part for any case.  Here, we use
      // face normals to determine which segments to use.  One segment
      // should be between two faces that are either both front facing or
      // back facing.  Obviously, we only need to test three faces to find
      // two such faces.  We test the three faces connected to point 0.
      vtkIdType segment1[2];
      vtkIdType segment2[2];

      float v1[2], v2[2], v3[3];
      v1[0] = tet_points[1 * 3 + 0] - tet_points[0 * 3 + 0];
      v1[1] = tet_points[1 * 3 + 1] - tet_points[0 * 3 + 1];
      v2[0] = tet_points[2 * 3 + 0] - tet_points[0 * 3 + 0];
      v2[1] = tet_points[2 * 3 + 1] - tet_points[0 * 3 + 1];
      v3[0] = tet_points[3 * 3 + 0] - tet_points[0 * 3 + 0];
      v3[1] = tet_points[3 * 3 + 1] - tet_points[0 * 3 + 1];

      float face_dir1 = v3[0] * v2[1] - v3[1] * v2[0];
      float face_dir2 = v1[0] * v3[1] - v1[1] * v3[0];
      float face_dir3 = v2[0] * v1[1] - v2[1] * v1[0];

      if ((face_dir1 * face_dir2 >= 0) &&
        ((face_dir1 != 0)       // Handle a special case where 2 faces
          || (face_dir2 != 0))) // are perpendicular to the view plane.
      {
        segment1[0] = 0;
        segment1[1] = 3;
        segment2[0] = 1;
        segment2[1] = 2;
      }
      else if (face_dir1 * face_dir3 >= 0)
      {
        segment1[0] = 0;
        segment1[1] = 2;
        segment2[0] = 1;
        segment2[1] = 3;
      }
      else // Unless the tet is degenerate, face_dir2*face_dir3 >= 0
      {
        segment1[0] = 0;
        segment1[1] = 1;
        segment2[0] = 2;
        segment2[1] = 3;
      }

#define VEC3SUB(Z, X, Y)                                                                           \
  do                                                                                               \
  {                                                                                                \
    (Z)[0] = (X)[0] - (Y)[0];                                                                      \
    (Z)[1] = (X)[1] - (Y)[1];                                                                      \
    (Z)[2] = (X)[2] - (Y)[2];                                                                      \
  } while (false)
#define P1 (tet_points + 3 * segment1[0])
#define P2 (tet_points + 3 * segment1[1])
#define P3 (tet_points + 3 * segment2[0])
#define P4 (tet_points + 3 * segment2[1])
#define C1 (tet_colors + 3 * segment1[0])
#define C2 (tet_colors + 3 * segment1[1])
#define C3 (tet_colors + 3 * segment2[0])
#define C4 (tet_colors + 3 * segment2[1])
#define T1 (tet_texcoords + 2 * segment1[0])
#define T2 (tet_texcoords + 2 * segment1[1])
#define T3 (tet_texcoords + 2 * segment2[0])
#define T4 (tet_texcoords + 2 * segment2[1])
      // Find the intersection of the projection of the two segments in the
      // XY plane.  This algorithm is based on that given in Graphics Gems
      // III, pg. 199-202.
      float A[3], B[3], C[3];
      // We can define the two lines parametrically as:
      //        P1 + alpha(A)
      //        P3 + beta(B)
      // where A = P2 - P1
      // and   B = P4 - P3.
      // alpha and beta are in the range [0,1] within the line segment.
      VEC3SUB(A, P2, P1);
      VEC3SUB(B, P4, P3);
      // The lines intersect when the values of the two parametric equations
      // are equal.  Setting them equal and moving everything to one side:
      //        0 = C + beta(B) - alpha(A)
      // where C = P3 - P1.
      VEC3SUB(C, P3, P1);
      // When we project the lines to the xy plane (which we do by throwing
      // away the z value), we have two equations and two unknowns.  The
      // following are the solutions for alpha and beta.
      float denominator = (A[0] * B[1] - A[1] * B[0]);
      if (denominator == 0)
        continue; // Must be degenerated tetrahedra.
      float alpha = (B[1] * C[0] - B[0] * C[1]) / denominator;
      float beta = (A[1] * C[0] - A[0] * C[1]) / denominator;

      if ((alpha >= 0) && (alpha <= 1))
      {
        // The two segments intersect.  This corresponds to class 2 in
        // Shirley and Tuchman (or one of the degenerate cases).

        // Make new point at intersection.
        tet_points[3 * 4 + 0] = P1[0] + alpha * A[0];
        tet_points[3 * 4 + 1] = P1[1] + alpha * A[1];
        tet_points[3 * 4 + 2] = P1[2] + alpha * A[2];

        // Find depth at intersection.
        float depth = this->GetCorrectedDepth(tet_points[3 * 4 + 0], tet_points[3 * 4 + 1],
          tet_points[3 * 4 + 2], P3[2] + beta * B[2], inverse_projection_mat,
          use_linear_depth_correction, linear_depth_correction);

        // Find color at intersection.
        tet_colors[3 * 4 + 0] = static_cast<unsigned char>(
          0.5f * (C1[0] + alpha * (C2[0] - C1[0]) + C3[0] + beta * (C4[0] - C3[0])));

        tet_colors[3 * 4 + 1] = static_cast<unsigned char>(
          0.5f * (C1[1] + alpha * (C2[1] - C1[1]) + C3[1] + beta * (C4[1] - C3[1])));

        tet_colors[3 * 4 + 2] = static_cast<unsigned char>(
          0.5f * (C1[2] + alpha * (C2[2] - C1[2]) + C3[2] + beta * (C4[2] - C3[2])));

        //         tet_colors[3*0 + 0] = 255;
        //         tet_colors[3*0 + 1] = 0;
        //         tet_colors[3*0 + 2] = 0;
        //         tet_colors[3*1 + 0] = 255;
        //         tet_colors[3*1 + 1] = 0;
        //         tet_colors[3*1 + 2] = 0;
        //         tet_colors[3*2 + 0] = 255;
        //         tet_colors[3*2 + 1] = 0;
        //         tet_colors[3*2 + 2] = 0;
        //         tet_colors[3*3 + 0] = 255;
        //         tet_colors[3*3 + 1] = 0;
        //         tet_colors[3*3 + 2] = 0;
        //         tet_colors[3*4 + 0] = 255;
        //         tet_colors[3*4 + 1] = 0;
        //         tet_colors[3*4 + 2] = 0;

        // Find the opacity at intersection.
        tet_texcoords[2 * 4 + 0] =
          0.5f * (T1[0] + alpha * (T2[0] - T1[0]) + T3[0] + alpha * (T4[0] - T3[0]));

        // Record the depth at the intersection.
        tet_texcoords[2 * 4 + 1] = depth / unit_distance;

        // Establish the order in which the points should be rendered.
        unsigned char indices[6];
        indices[0] = 4;
        indices[1] = segment1[0];
        indices[2] = segment2[0];
        indices[3] = segment1[1];
        indices[4] = segment2[1];
        indices[5] = segment1[0];
        // add the cells to the IBO
        for (int cellIdx = 0; cellIdx < 4; cellIdx++)
        {
          indexArray.push_back(indices[0] + numPts);
          indexArray.push_back(indices[cellIdx + 1] + numPts);
          indexArray.push_back(indices[cellIdx + 2] + numPts);
        }
      }
      else
      {
        // The two segments do not intersect.  This corresponds to class 1
        // in Shirley and Tuchman.
        if (alpha <= 0)
        {
          // Flip segment1 so that alpha is >= 1.  P1 and P2 are also
          // flipped as are C1-C2 and T1-T2.  Note that this will
          // invalidate A.  B and beta are unaffected.
          std::swap(segment1[0], segment1[1]);
          alpha = 1 - alpha;
        }
        // From here on, we can assume P2 is the "thick" point.

        // Find the depth under the thick point.  Use the alpha and beta
        // from intersection to determine location of face under thick
        // point.
        float edgez = P3[2] + beta * B[2];
        float pointz = P1[2];
        float facez = (edgez + (alpha - 1) * pointz) / alpha;
        float depth = GetCorrectedDepth(P2[0], P2[1], P2[2], facez, inverse_projection_mat,
          use_linear_depth_correction, linear_depth_correction);

        // Fix color at thick point.  Average color with color of opposite
        // face.
        for (j = 0; j < 3; j++)
        {
          float edgec = C3[j] + beta * (C4[j] - C3[j]);
          float pointc = C1[j];
          float facec = (edgec + (alpha - 1) * pointc) / alpha;
          C2[j] = (unsigned char)(0.5f * (facec + C2[j]));
        }

        //         tet_colors[3*segment1[0] + 0] = 0;
        //         tet_colors[3*segment1[0] + 1] = 255;
        //         tet_colors[3*segment1[0] + 2] = 0;
        //         tet_colors[3*segment1[1] + 0] = 0;
        //         tet_colors[3*segment1[1] + 1] = 255;
        //         tet_colors[3*segment1[1] + 2] = 0;
        //         tet_colors[3*segment2[0] + 0] = 0;
        //         tet_colors[3*segment2[0] + 1] = 255;
        //         tet_colors[3*segment2[0] + 2] = 0;
        //         tet_colors[3*segment2[1] + 0] = 0;
        //         tet_colors[3*segment2[1] + 1] = 255;
        //         tet_colors[3*segment2[1] + 2] = 0;

        // Fix opacity at thick point.  Average opacity with opacity of
        // opposite face.
        float edgea = T3[0] + beta * (T4[0] - T3[0]);
        float pointa = T1[0];
        float facea = (edgea + (alpha - 1) * pointa) / alpha;
        T2[0] = 0.5f * (facea + T2[0]);

        // Record thickness at thick point.
        T2[1] = depth / unit_distance;

        // Establish the order in which the points should be rendered.
        unsigned char indices[5];
        indices[0] = segment1[1];
        indices[1] = segment1[0];
        indices[2] = segment2[0];
        indices[3] = segment2[1];
        indices[4] = segment1[0];

        // add the cells to the IBO
        for (int cellIdx = 0; cellIdx < 3; cellIdx++)
        {
          indexArray.push_back(indices[0] + numPts);
          indexArray.push_back(indices[cellIdx + 1] + numPts);
          indexArray.push_back(indices[cellIdx + 2] + numPts);
        }
      }

      // add the points to the VBO
      union
      {
        unsigned char c[4];
        float f;
      } v = { { 0, 0, 0, 255 } };
      for (int ptIdx = 0; ptIdx < 5; ptIdx++)
      {
        *(it++) = tet_points[ptIdx * 3];
        *(it++) = tet_points[ptIdx * 3 + 1];
        *(it++) = tet_points[ptIdx * 3 + 2];
        v.c[0] = tet_colors[ptIdx * 3];
        v.c[1] = tet_colors[ptIdx * 3 + 1];
        v.c[2] = tet_colors[ptIdx * 3 + 2];
        *(it++) = v.f;
        *(it++) = tet_texcoords[ptIdx * 2];     // attenuation
        *(it++) = tet_texcoords[ptIdx * 2 + 1]; // depth
      }
      numPts += 5;
    }

    this->VBO->Upload(packedVBO, vtkOpenGLBufferObject::ArrayBuffer);
    this->VBO->Bind();

    this->Tris.VAO->Bind();
    if (this->Tris.IBO->IndexCount &&
      (this->Tris.ShaderSourceTime > this->Tris.AttributeUpdateTime))
    {
      if (!this->Tris.VAO->AddAttributeArray(this->Tris.Program, this->VBO, "vertexDC", 0,
            this->VBO->GetStride(), VTK_FLOAT, 3, false))
      {
        vtkErrorMacro(<< "Error setting 'vertexDC' in shader VAO.");
      }
      if (!this->Tris.VAO->AddAttributeArray(this->Tris.Program, this->VBO, "scalarColor",
            3 * sizeof(float), this->VBO->GetStride(), VTK_UNSIGNED_CHAR, 3, true))
      {
        vtkErrorMacro(<< "Error setting 'scalarColor' in shader VAO.");
      }
      if (!this->Tris.VAO->AddAttributeArray(this->Tris.Program, this->VBO, "attenuationArray",
            4 * sizeof(float), this->VBO->GetStride(), VTK_FLOAT, 1, false))
      {
        vtkErrorMacro(<< "Error setting attenuation in shader VAO.");
      }
      if (!this->Tris.VAO->AddAttributeArray(this->Tris.Program, this->VBO, "depthArray",
            5 * sizeof(float), this->VBO->GetStride(), VTK_FLOAT, 1, false))
      {
        vtkErrorMacro(<< "Error setting depth in shader VAO.");
      }
      this->Tris.AttributeUpdateTime.Modified();
    }

    this->Tris.IBO->Upload(indexArray, vtkOpenGLBufferObject::ElementArrayBuffer);
    this->Tris.IBO->IndexCount = indexArray.size();
    this->Tris.IBO->Bind();
    // Avoid underflow in numPts-1 calculation
    if (numPts > 0)
    {
      glDrawRangeElements(GL_TRIANGLES, 0, static_cast<GLuint>(numPts - 1),
        static_cast<GLsizei>(this->Tris.IBO->IndexCount), GL_UNSIGNED_INT, nullptr);
    }
    this->Tris.IBO->Release();
    this->Tris.VAO->Release();
    this->VBO->Release();
    numcellsrendered += num_cell_ids;
  }

  if (fo)
  {
    // copy from our fbo to the default one
    fo->Bind(vtkOpenGLFramebufferObject::GetReadMode());

    // draw to default fbo
    ostate->PopDrawFramebufferBinding();

    // Depth buffer has not changed so only copy color
    ostate->vtkglBlitFramebuffer(0, 0, this->CurrentFBOWidth, this->CurrentFBOHeight, 0, 0,
      this->CurrentFBOWidth, this->CurrentFBOHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);

    vtkOpenGLCheckErrorMacro("failed at glBlitFramebuffer");

    // restore default fbo for both read+draw
    ostate->PopReadFramebufferBinding();
  }

  // Restore the blend function.
  vtkOpenGLCheckErrorMacro("failed at glPopAttrib");

  ostate->vtkglDepthMask(GL_TRUE);

  vtkOpenGLCheckErrorMacro("failed after ProjectTetrahedra");
  this->GLSafeUpdateProgress(1.0, window);
}

//------------------------------------------------------------------------------
void vtkOpenGLProjectedTetrahedraMapper::GLSafeUpdateProgress(double, vtkOpenGLRenderWindow*)
{
  // Turns out firing progress event during rendering is not only opens up the
  // potential corrupting buffers, but also slows the mapper down considerably!
  // turning off progress events entirely. just not worth the hassle at this
  // point.
#if 0
  scoped_annotate annotator("GLSafeUpdateProgress");
  window->GetState()->PushFramebufferBindings();
  // since UpdateProgress may causes GL context changes, we save and restore
  // state.
  this->UpdateProgress(value);
  window->MakeCurrent();
  window->GetState()->PopFramebufferBindings();
  vtkOpenGLCheckErrorMacro("failed after GLSafeUpdateProgress");
#endif
}
VTK_ABI_NAMESPACE_END
