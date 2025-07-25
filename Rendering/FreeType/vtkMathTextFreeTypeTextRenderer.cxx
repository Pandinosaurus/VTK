// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkMathTextFreeTypeTextRenderer.h"

#include "vtkFreeTypeTools.h"
#include "vtkMathTextUtilities.h"
#include "vtkObjectFactory.h"
#include "vtkStdString.h"
#include "vtkTextProperty.h"

//------------------------------------------------------------------------------
VTK_ABI_NAMESPACE_BEGIN
vtkObjectFactoryNewMacro(vtkMathTextFreeTypeTextRenderer);

//------------------------------------------------------------------------------
void vtkMathTextFreeTypeTextRenderer::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  if (this->FreeTypeTools)
  {
    os << indent << "FreeTypeTools:" << endl;
    this->FreeTypeTools->PrintSelf(os, indent.GetNextIndent());
  }
  else
  {
    os << indent << "FreeTypeTools: (nullptr)" << endl;
  }

  if (this->MathTextUtilities)
  {
    os << indent << "MathTextUtilities:" << endl;
    this->MathTextUtilities->PrintSelf(os, indent.GetNextIndent());
  }
  else
  {
    os << indent << "MathTextUtilities: (nullptr)" << endl;
  }
}

//------------------------------------------------------------------------------
bool vtkMathTextFreeTypeTextRenderer::FreeTypeIsSupported()
{
  return this->FreeTypeTools != nullptr;
}

//------------------------------------------------------------------------------
bool vtkMathTextFreeTypeTextRenderer::MathTextIsSupported()
{
  return this->MathTextUtilities != nullptr && this->MathTextUtilities->IsAvailable();
}

//------------------------------------------------------------------------------
bool vtkMathTextFreeTypeTextRenderer::GetBoundingBoxInternal(
  vtkTextProperty* tprop, const vtkStdString& str, int bbox[4], int dpi, int backend)
{
  if (!bbox || !tprop)
  {
    vtkErrorMacro("No bounding box container and/or text property supplied!");
    return false;
  }

  memset(bbox, 0, 4 * sizeof(int));
  if (str.empty())
  {
    return true;
  }

  if (static_cast<Backend>(backend) == Default)
  {
    backend = this->DefaultBackend;
  }

  if (static_cast<Backend>(backend) == Detect)
  {
    backend = this->DetectBackend(str);
  }

  switch (static_cast<Backend>(backend))
  {
    case MathText:
      if (this->MathTextIsSupported())
      {
        if (this->MathTextUtilities->GetBoundingBox(tprop, str.c_str(), dpi, bbox))
        {
          return true;
        }
      }
      vtkDebugMacro("MathText unavailable. Falling back to FreeType.");
      [[fallthrough]];
    case FreeType:
    {
      vtkStdString cleanString(str);
      this->CleanUpFreeTypeEscapes(cleanString);
      // Interpret string as UTF-8, use the UTF-16 GetBoundingBox overload:
      return this->FreeTypeTools->GetBoundingBox(tprop, cleanString, dpi, bbox);
    }
    case Default:
    case UserBackend:
    default:
      vtkDebugMacro("Unrecognized backend requested: " << backend);
      break;
    case Detect:
      vtkDebugMacro("Unhandled 'Detect' backend requested!");
      break;
  }
  return false;
}

//------------------------------------------------------------------------------
bool vtkMathTextFreeTypeTextRenderer::GetMetricsInternal(vtkTextProperty* tprop,
  const vtkStdString& str, vtkTextRenderer::Metrics& metrics, int dpi, int backend)
{
  if (!tprop)
  {
    vtkErrorMacro("No text property supplied!");
    return false;
  }

  metrics = Metrics();
  if (str.empty())
  {
    return true;
  }

  if (static_cast<Backend>(backend) == Default)
  {
    backend = this->DefaultBackend;
  }

  if (static_cast<Backend>(backend) == Detect)
  {
    backend = this->DetectBackend(str);
  }

  switch (static_cast<Backend>(backend))
  {
    case MathText:
      if (this->MathTextIsSupported())
      {
        if (this->MathTextUtilities->GetMetrics(tprop, str.c_str(), dpi, metrics))
        {
          return true;
        }
      }
      vtkDebugMacro("MathText unavailable. Falling back to FreeType.");
      [[fallthrough]];
    case FreeType:
    {
      vtkStdString cleanString(str);
      this->CleanUpFreeTypeEscapes(cleanString);
      // Interpret string as UTF-8, use the UTF-16 GetMetrics overload:
      return this->FreeTypeTools->GetMetrics(tprop, cleanString, dpi, metrics);
    }
    case Default:
    case UserBackend:
    default:
      vtkDebugMacro("Unrecognized backend requested: " << backend);
      break;
    case Detect:
      vtkDebugMacro("Unhandled 'Detect' backend requested!");
      break;
  }
  return false;
}

//------------------------------------------------------------------------------
bool vtkMathTextFreeTypeTextRenderer::RenderStringInternal(vtkTextProperty* tprop,
  const vtkStdString& str, vtkImageData* data, int textDims[2], int dpi, int backend)
{
  if (!data || !tprop)
  {
    vtkErrorMacro("No image container and/or text property supplied!");
    return false;
  }

  if (static_cast<Backend>(backend) == Default)
  {
    backend = this->DefaultBackend;
  }

  if (static_cast<Backend>(backend) == Detect)
  {
    backend = this->DetectBackend(str);
  }

  switch (static_cast<Backend>(backend))
  {
    case MathText:
      if (this->MathTextIsSupported())
      {
        if (this->MathTextUtilities->RenderString(str.c_str(), data, tprop, dpi, textDims))
        {
          return true;
        }
      }
      vtkDebugMacro("MathText unavailable. Falling back to FreeType.");
      [[fallthrough]];
    case FreeType:
    {
      vtkStdString cleanString(str);
      this->CleanUpFreeTypeEscapes(cleanString);
      // Interpret string as UTF-8, use the UTF-16 RenderString overload:
      return this->FreeTypeTools->RenderString(tprop, cleanString, dpi, data, textDims);
    }
    case Default:
    case UserBackend:
    default:
      vtkDebugMacro("Unrecognized backend requested: " << backend);
      break;
    case Detect:
      vtkDebugMacro("Unhandled 'Detect' backend requested!");
      break;
  }
  return false;
}

//------------------------------------------------------------------------------
int vtkMathTextFreeTypeTextRenderer::GetConstrainedFontSizeInternal(const vtkStdString& str,
  vtkTextProperty* tprop, int targetWidth, int targetHeight, int dpi, int backend)
{
  if (!tprop)
  {
    vtkErrorMacro("No text property supplied!");
    return false;
  }

  if (static_cast<Backend>(backend) == Default)
  {
    backend = this->DefaultBackend;
  }

  if (static_cast<Backend>(backend) == Detect)
  {
    backend = this->DetectBackend(str);
  }

  switch (static_cast<Backend>(backend))
  {
    case MathText:
      if (this->MathTextIsSupported())
      {
        if (this->MathTextUtilities->GetConstrainedFontSize(
              str.c_str(), tprop, targetWidth, targetHeight, dpi) != -1)
        {
          return tprop->GetFontSize();
        }
      }
      vtkDebugMacro("MathText unavailable. Falling back to FreeType.");
      [[fallthrough]];
    case FreeType:
    {
      vtkStdString cleanString(str);
      this->CleanUpFreeTypeEscapes(cleanString);
      return this->FreeTypeTools->GetConstrainedFontSize(
        cleanString, tprop, dpi, targetWidth, targetHeight);
    }
    case Default:
    case UserBackend:
    default:
      vtkDebugMacro("Unrecognized backend requested: " << backend);
      break;
    case Detect:
      vtkDebugMacro("Unhandled 'Detect' backend requested!");
      break;
  }
  return false;
}

//------------------------------------------------------------------------------
bool vtkMathTextFreeTypeTextRenderer::StringToPathInternal(
  vtkTextProperty* tprop, const vtkStdString& str, vtkPath* path, int dpi, int backend)
{
  if (!path || !tprop)
  {
    vtkErrorMacro("No path container and/or text property supplied!");
    return false;
  }

  if (static_cast<Backend>(backend) == Default)
  {
    backend = this->DefaultBackend;
  }

  if (static_cast<Backend>(backend) == Detect)
  {
    backend = this->DetectBackend(str);
  }

  switch (static_cast<Backend>(backend))
  {
    case MathText:
      if (this->MathTextIsSupported())
      {
        if (this->MathTextUtilities->StringToPath(str.c_str(), path, tprop, dpi))
        {
          return true;
        }
      }
      vtkDebugMacro("MathText unavailable. Falling back to FreeType.");
      [[fallthrough]];
    case FreeType:
    {
      vtkStdString cleanString(str);
      this->CleanUpFreeTypeEscapes(cleanString);
      return this->FreeTypeTools->StringToPath(tprop, str, dpi, path);
    }
    case Default:
    case UserBackend:
    default:
      vtkDebugMacro("Unrecognized backend requested: " << backend);
      break;
    case Detect:
      vtkDebugMacro("Unhandled 'Detect' backend requested!");
      break;
  }
  return false;
}

//------------------------------------------------------------------------------
void vtkMathTextFreeTypeTextRenderer::SetScaleToPowerOfTwoInternal(bool scale)
{
  if (this->FreeTypeTools)
  {
    this->FreeTypeTools->SetScaleToPowerTwo(scale);
  }
  if (this->MathTextUtilities)
  {
    this->MathTextUtilities->SetScaleToPowerOfTwo(scale);
  }
}

//------------------------------------------------------------------------------
vtkMathTextFreeTypeTextRenderer::vtkMathTextFreeTypeTextRenderer()
{
  this->FreeTypeTools = vtkFreeTypeTools::GetInstance();
  this->MathTextUtilities = vtkMathTextUtilities::GetInstance();
}

//------------------------------------------------------------------------------
vtkMathTextFreeTypeTextRenderer::~vtkMathTextFreeTypeTextRenderer() = default;
VTK_ABI_NAMESPACE_END
