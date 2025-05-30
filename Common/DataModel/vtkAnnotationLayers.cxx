// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-FileCopyrightText: Copyright 2008 Sandia Corporation
// SPDX-License-Identifier: LicenseRef-BSD-3-Clause-Sandia-USGov

#include "vtkAnnotationLayers.h"

#include "vtkAnnotation.h"
#include "vtkIdTypeArray.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkSelection.h"
#include "vtkSelectionNode.h"
#include "vtkSmartPointer.h"

#include <algorithm>
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkAnnotationLayers);
vtkCxxSetObjectMacro(vtkAnnotationLayers, CurrentAnnotation, vtkAnnotation);

class vtkAnnotationLayers::Internals
{
public:
  std::vector<vtkSmartPointer<vtkAnnotation>> Annotations;
};

vtkAnnotationLayers::vtkAnnotationLayers()
  : Implementation(new Internals())
{
  this->CurrentAnnotation = vtkAnnotation::New();

  // Start with an empty index selection
  vtkSmartPointer<vtkSelection> sel = vtkSmartPointer<vtkSelection>::New();
  vtkSmartPointer<vtkSelectionNode> node = vtkSmartPointer<vtkSelectionNode>::New();
  node->SetContentType(vtkSelectionNode::INDICES);
  vtkSmartPointer<vtkIdTypeArray> ids = vtkSmartPointer<vtkIdTypeArray>::New();
  node->SetSelectionList(ids);
  sel->AddNode(node);
  this->CurrentAnnotation->SetSelection(sel);
}

vtkAnnotationLayers::~vtkAnnotationLayers()
{
  delete this->Implementation;
  if (this->CurrentAnnotation)
  {
    this->CurrentAnnotation->Delete();
  }
}

void vtkAnnotationLayers::SetCurrentSelection(vtkSelection* sel)
{
  if (this->CurrentAnnotation)
  {
    this->CurrentAnnotation->SetSelection(sel);
    this->Modified();
  }
}

vtkSelection* vtkAnnotationLayers::GetCurrentSelection()
{
  if (this->CurrentAnnotation)
  {
    return this->CurrentAnnotation->GetSelection();
  }
  return nullptr;
}

unsigned int vtkAnnotationLayers::GetNumberOfAnnotations()
{
  return static_cast<unsigned int>(this->Implementation->Annotations.size());
}

vtkAnnotation* vtkAnnotationLayers::GetAnnotation(unsigned int idx)
{
  if (idx >= this->Implementation->Annotations.size())
  {
    return nullptr;
  }
  return this->Implementation->Annotations[idx];
}

void vtkAnnotationLayers::AddAnnotation(vtkAnnotation* annotation)
{
  this->Implementation->Annotations.emplace_back(annotation);
  this->Modified();
}

void vtkAnnotationLayers::RemoveAnnotation(vtkAnnotation* annotation)
{
  this->Implementation->Annotations.erase(std::remove(this->Implementation->Annotations.begin(),
                                            this->Implementation->Annotations.end(), annotation),
    this->Implementation->Annotations.end());
  this->Modified();
}

void vtkAnnotationLayers::Initialize()
{
  this->Implementation->Annotations.clear();
  this->Modified();
}

void vtkAnnotationLayers::ShallowCopy(vtkDataObject* other)
{
  this->Superclass::ShallowCopy(other);
  vtkAnnotationLayers* obj = vtkAnnotationLayers::SafeDownCast(other);
  if (!obj)
  {
    return;
  }
  this->Implementation->Annotations.clear();
  for (unsigned int a = 0; a < obj->GetNumberOfAnnotations(); ++a)
  {
    vtkAnnotation* ann = obj->GetAnnotation(a);
    this->AddAnnotation(ann);
  }
  this->SetCurrentAnnotation(obj->GetCurrentAnnotation());
}

void vtkAnnotationLayers::DeepCopy(vtkDataObject* other)
{
  this->Superclass::DeepCopy(other);
  vtkAnnotationLayers* obj = vtkAnnotationLayers::SafeDownCast(other);
  if (!obj)
  {
    return;
  }
  this->Implementation->Annotations.clear();
  for (unsigned int a = 0; a < obj->GetNumberOfAnnotations(); ++a)
  {
    vtkSmartPointer<vtkAnnotation> ann = vtkSmartPointer<vtkAnnotation>::New();
    ann->DeepCopy(obj->GetAnnotation(a));
    this->AddAnnotation(ann);
  }
}

vtkMTimeType vtkAnnotationLayers::GetMTime()
{
  vtkMTimeType mtime = this->Superclass::GetMTime();
  for (unsigned int a = 0; a < this->GetNumberOfAnnotations(); ++a)
  {
    vtkAnnotation* ann = this->GetAnnotation(a);
    if (ann)
    {
      vtkMTimeType atime = ann->GetMTime();
      if (atime > mtime)
      {
        mtime = atime;
      }
    }
  }
  vtkAnnotation* s = this->GetCurrentAnnotation();
  if (s)
  {
    vtkMTimeType stime = s->GetMTime();
    if (stime > mtime)
    {
      mtime = stime;
    }
  }
  return mtime;
}

void vtkAnnotationLayers::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  vtkIndent next = indent.GetNextIndent();
  for (unsigned int a = 0; a < this->GetNumberOfAnnotations(); ++a)
  {
    os << next << "Annotation " << a << ":";
    vtkAnnotation* ann = this->GetAnnotation(a);
    if (ann)
    {
      os << "\n";
      ann->PrintSelf(os, next.GetNextIndent());
    }
    else
    {
      os << "(none)\n";
    }
  }
  os << indent << "CurrentAnnotation: ";
  if (this->CurrentAnnotation)
  {
    os << "\n";
    this->CurrentAnnotation->PrintSelf(os, indent.GetNextIndent());
  }
  else
  {
    os << "(none)\n";
  }
}

vtkAnnotationLayers* vtkAnnotationLayers::GetData(vtkInformation* info)
{
  return info ? vtkAnnotationLayers::SafeDownCast(info->Get(DATA_OBJECT())) : nullptr;
}

vtkAnnotationLayers* vtkAnnotationLayers::GetData(vtkInformationVector* v, int i)
{
  return vtkAnnotationLayers::GetData(v->GetInformationObject(i));
}
VTK_ABI_NAMESPACE_END
