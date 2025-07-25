// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
// .SECTION Thanks
// Contribution by Thierry Carrard <br>
// CEA/DIF - Commissariat a l'Energie Atomique, Centre DAM Ile-De-France <br>
// BP12, F-91297 Arpajon, France. <br>

#include "vtkPolyDataSilhouette.h"

#include "vtkCamera.h"
#include "vtkCellArray.h"
#include "vtkCellArrayIterator.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkMath.h"
#include "vtkObjectFactory.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkPolygon.h"
#include "vtkProp3D.h"
#include "vtkTransform.h"

#include <map>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkPolyDataSilhouette);

vtkCxxSetObjectMacro(vtkPolyDataSilhouette, Camera, vtkCamera);

struct vtkOrderedEdge
{
  vtkOrderedEdge(vtkIdType a, vtkIdType b)
  {
    if (a <= b)
    {
      p1 = a;
      p2 = b;
    }
    else
    {
      p1 = b;
      p2 = a;
    }
  }
  bool operator<(const vtkOrderedEdge& oe) const
  {
    return (p1 < oe.p1) || ((p1 == oe.p1) && (p2 < oe.p2));
  }
  vtkIdType p1, p2;
};

struct vtkTwoNormals
{
  double leftNormal[3];  // normal of the left polygon
  double rightNormal[3]; // normal of the right polygon
  vtkTwoNormals()
  {
    leftNormal[0] = 0.0;
    leftNormal[1] = 0.0;
    leftNormal[2] = 0.0;
    rightNormal[0] = 0.0;
    rightNormal[1] = 0.0;
    rightNormal[2] = 0.0;
  }
};

class vtkPolyDataEdges
{
public:
  vtkTimeStamp mtime;
  double vec[3];
  std::map<vtkOrderedEdge, vtkTwoNormals> edges;
  bool* edgeFlag;
  vtkCellArray* lines;
  vtkPolyDataEdges()
    : edgeFlag(nullptr)
    , lines(nullptr)
  {
    vec[0] = vec[1] = vec[2] = 0.0;
  }
};

vtkPolyDataSilhouette::vtkPolyDataSilhouette()
{
  this->Camera = nullptr;
  this->Prop3D = nullptr;
  this->Direction = VTK_DIRECTION_CAMERA_ORIGIN;
  this->Vector[0] = this->Vector[1] = this->Vector[2] = 0.0;
  this->Origin[0] = this->Origin[1] = this->Origin[2] = 0.0;
  this->Transform = vtkTransform::New();
  this->EnableFeatureAngle = 1;
  this->FeatureAngle = 60;
  this->BorderEdges = 0;
  this->PieceInvariant = 1;
  this->PreComp = new vtkPolyDataEdges();
}

vtkPolyDataSilhouette::~vtkPolyDataSilhouette()
{
  this->Transform->Delete();

  if (this->Camera)
  {
    this->Camera->Delete();
  }

  delete[] this->PreComp->edgeFlag;
  if (this->PreComp->lines)
  {
    this->PreComp->lines->Delete();
  }
  delete this->PreComp;

  // Note: vtkProp3D is not deleted to avoid reference count cycle
}

// Don't reference count to avoid nasty cycle
void vtkPolyDataSilhouette::SetProp3D(vtkProp3D* prop3d)
{
  if (this->Prop3D != prop3d)
  {
    this->Prop3D = prop3d;
    this->Modified();
  }
}

vtkProp3D* vtkPolyDataSilhouette::GetProp3D()
{
  return this->Prop3D;
}

int vtkPolyDataSilhouette::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** inputVector, vtkInformationVector* outputVector)
{
  // get the info objects
  vtkInformation* inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation* outInfo = outputVector->GetInformationObject(0);

  // get the input and output
  vtkPolyData* input = vtkPolyData::SafeDownCast(inInfo->Get(vtkDataObject::DATA_OBJECT()));
  vtkPolyData* output = vtkPolyData::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));

  if (input == nullptr || output == nullptr)
  {
    vtkErrorMacro(<< "Need correct connections");
    return 0;
  }

  vtkDebugMacro(<< "RequestData\n");

  const double featureAngleCos = cos(vtkMath::RadiansFromDegrees(this->FeatureAngle));

  bool vectorMode = true;
  double vector[3];
  double origin[3];

  // Compute the sort vector
  switch (this->Direction)
  {
    case VTK_DIRECTION_SPECIFIED_VECTOR:
      vector[0] = this->Vector[0];
      vector[1] = this->Vector[1];
      vector[2] = this->Vector[2];
      break;

    case VTK_DIRECTION_SPECIFIED_ORIGIN:
      origin[0] = this->Origin[0];
      origin[1] = this->Origin[1];
      origin[2] = this->Origin[2];
      vectorMode = false;
      break;

    case VTK_DIRECTION_CAMERA_ORIGIN:
      vectorMode = false;
      [[fallthrough]];

    case VTK_DIRECTION_CAMERA_VECTOR:
      if (this->Camera == nullptr)
      {
        vtkErrorMacro(<< "Need a camera when direction is set to VTK_DIRECTION_CAMERA_*");
        return 0;
      }
      this->ComputeProjectionVector(vector, origin);
      break;
  }

  vtkPoints* inPoints = input->GetPoints();

  if (input->GetMTime() > this->PreComp->mtime.GetMTime())
  {
    vtkDebugMacro(<< "Compute edge-face connectivity and face normals\n");

    this->PreComp->mtime.Modified();
    this->PreComp->edges.clear();

    vtkCellArray* polyCells = input->GetPolys();
    auto polyIter = vtkSmartPointer<vtkCellArrayIterator>::Take(polyCells->NewIterator());

    for (polyIter->GoToFirstCell(); !polyIter->IsDoneWithTraversal(); polyIter->GoToNextCell())
    {
      vtkIdType np;
      const vtkIdType* polys;
      polyIter->GetCurrentCell(np, polys);

      double normal[3];
      vtkPolygon::ComputeNormal(
        inPoints, static_cast<int>(np), const_cast<vtkIdType*>(polys), normal);

      for (int j = 0; j < np; j++)
      {
        vtkIdType p1 = j, p2 = (j + 1) % np;
        vtkOrderedEdge oe(polys[p1], polys[p2]);
        vtkTwoNormals& tn = this->PreComp->edges[oe];
        if (polys[p1] < polys[p2])
        {
#ifdef DEBUG
          if (vtkMath::Dot(tn.leftNormal, tn.leftNormal) > 0)
          {
            vtkDebugMacro(<< "Warning: vtkPolyDataSilhouette: non-manifold mesh: edge-L ("
                          << polys[p1] << "," << polys[p2] << ") counted more than once\n");
          }
#endif
          tn.leftNormal[0] = normal[0];
          tn.leftNormal[1] = normal[1];
          tn.leftNormal[2] = normal[2];
        }
        else
        {
#ifdef DEBUG
          if (vtkMath::Dot(tn.rightNormal, tn.rightNormal) > 0)
          {
            vtkDebugMacro(<< "Warning: vtkPolyDataSilhouette: non-manifold mesh: edge-R ("
                          << polys[p1] << "," << polys[p2] << ") counted more than once\n");
          }
#endif
          tn.rightNormal[0] = normal[0];
          tn.rightNormal[1] = normal[1];
          tn.rightNormal[2] = normal[2];
        }
      }
    }

    delete[] this->PreComp->edgeFlag;
    this->PreComp->edgeFlag = new bool[this->PreComp->edges.size()];
  }

  bool vecChanged = false;
  for (int d = 0; d < 3; d++)
  {
    vecChanged = vecChanged || this->PreComp->vec[d] != vector[d];
  }

  if ((this->PreComp->mtime.GetMTime() > output->GetMTime()) ||
    (this->Camera && this->Camera->GetMTime() > output->GetMTime()) ||
    (this->Prop3D && this->Prop3D->GetMTime() > output->GetMTime()) || vecChanged)
  {
    vtkDebugMacro(<< "Extract edges\n");

    vtkIdType i = 0, silhouetteEdges = 0;

    for (std::map<vtkOrderedEdge, vtkTwoNormals>::iterator it = this->PreComp->edges.begin();
         it != this->PreComp->edges.end(); ++it)
    {
      if (this->CheckAbort())
      {
        break;
      }
      double d1, d2;

      // does this edge have two co-faces ?
      bool winged =
        vtkMath::Norm(it->second.leftNormal) > 0.5 && vtkMath::Norm(it->second.rightNormal) > 0.5;

      // cosine of feature angle, to be compared with scalar product of two co-faces normals
      double edgeAngleCos = vtkMath::Dot(it->second.leftNormal, it->second.rightNormal);

      if (vectorMode) // uniform direction
      {
        d1 = vtkMath::Dot(vector, it->second.leftNormal);
        d2 = vtkMath::Dot(vector, it->second.rightNormal);
      }
      else // origin to edge's center direction
      {
        double p1[3];
        double p2[3];
        double vec[3];
        inPoints->GetPoint(it->first.p1, p1);
        inPoints->GetPoint(it->first.p2, p2);
        vec[0] = origin[0] - ((p1[0] + p2[0]) * 0.5);
        vec[1] = origin[1] - ((p1[1] + p2[1]) * 0.5);
        vec[2] = origin[2] - ((p1[2] + p2[2]) * 0.5);
        d1 = vtkMath::Dot(vec, it->second.leftNormal);
        d2 = vtkMath::Dot(vec, it->second.rightNormal);
      }

      // shall we output this edge ?
      bool outputEdge = (winged && (d1 * d2) < 0) ||
        (this->EnableFeatureAngle && edgeAngleCos < featureAngleCos) ||
        (this->BorderEdges && !winged);

      if (outputEdge) // add this edge
      {
        this->PreComp->edgeFlag[i] = true;
        ++silhouetteEdges;
      }
      else // skip this edge
      {
        this->PreComp->edgeFlag[i] = false;
      }
      ++i;
    }

    // build output data set (lines)
    if (this->PreComp->lines != nullptr)
    {
      this->PreComp->lines->Delete();
    }
    this->PreComp->lines = vtkCellArray::New();
    this->PreComp->lines->AllocateEstimate(silhouetteEdges, 2);

    i = 0;
    silhouetteEdges = 0;
    for (std::map<vtkOrderedEdge, vtkTwoNormals>::iterator it = this->PreComp->edges.begin();
         it != this->PreComp->edges.end(); ++it)
    {
      if (this->PreComp->edgeFlag[i])
      {
        this->PreComp->lines->InsertNextCell({ it->first.p1, it->first.p2 });
      }
      ++i;
    }
  }

  output->Initialize();
  output->SetPoints(inPoints);
  output->SetLines(this->PreComp->lines);

  return 1;
}

void vtkPolyDataSilhouette::ComputeProjectionVector(double vector[3], double origin[3])
{
  double* focalPoint = this->Camera->GetFocalPoint();
  double* position = this->Camera->GetPosition();

  // If a camera is present, use it
  if (!this->Prop3D)
  {
    for (int i = 0; i < 3; i++)
    {
      vector[i] = focalPoint[i] - position[i];
      origin[i] = position[i];
    }
  }

  else // Otherwise, use Prop3D
  {
    double focalPt[4], pos[4];
    int i;

    this->Transform->SetMatrix(this->Prop3D->GetMatrix());
    this->Transform->Push();
    this->Transform->Inverse();

    for (i = 0; i < 4; i++)
    {
      focalPt[i] = focalPoint[i];
      pos[i] = position[i];
    }

    this->Transform->TransformPoint(focalPt, focalPt);
    this->Transform->TransformPoint(pos, pos);

    for (i = 0; i < 3; i++)
    {
      vector[i] = focalPt[i] - pos[i];
      origin[i] = pos[i];
    }
    this->Transform->Pop();
  }
}

vtkMTimeType vtkPolyDataSilhouette::GetMTime()
{
  vtkMTimeType mTime = this->Superclass::GetMTime();

  if (this->Direction != VTK_DIRECTION_SPECIFIED_VECTOR)
  {
    vtkMTimeType time;
    if (this->Camera != nullptr)
    {
      time = this->Camera->GetMTime();
      mTime = (time > mTime ? time : mTime);
    }

    if (this->Prop3D != nullptr)
    {
      time = this->Prop3D->GetMTime();
      mTime = (time > mTime ? time : mTime);
    }
  }

  return mTime;
}

void vtkPolyDataSilhouette::PrintSelf(ostream& os, vtkIndent indent)
{

  this->Superclass::PrintSelf(os, indent);

  if (this->Camera)
  {
    os << indent << "Camera:\n";
    this->Camera->PrintSelf(os, indent.GetNextIndent());
  }
  else
  {
    os << indent << "Camera: (none)\n";
  }

  if (this->Prop3D)
  {
    os << indent << "Prop3D:\n";
    this->Prop3D->PrintSelf(os, indent.GetNextIndent());
  }
  else
  {
    os << indent << "Prop3D: (none)\n";
  }

  os << indent << "Direction: ";
#define DIRECTION_CASE(name)                                                                       \
  case VTK_DIRECTION_##name:                                                                       \
    os << "VTK_DIRECTION_" << #name << "\n";                                                       \
    break
  switch (this->Direction)
  {
    DIRECTION_CASE(SPECIFIED_ORIGIN);
    DIRECTION_CASE(SPECIFIED_VECTOR);
    DIRECTION_CASE(CAMERA_ORIGIN);
    DIRECTION_CASE(CAMERA_VECTOR);
  }
#undef DIRECTION_CASE

  if (this->Direction == VTK_DIRECTION_SPECIFIED_VECTOR)
  {
    os << "Specified Vector: (" << this->Vector[0] << ", " << this->Vector[1] << ", "
       << this->Vector[2] << ")\n";
  }
  if (this->Direction == VTK_DIRECTION_SPECIFIED_ORIGIN)
  {
    os << "Specified Origin: (" << this->Origin[0] << ", " << this->Origin[1] << ", "
       << this->Origin[2] << ")\n";
  }

  os << indent << "PieceInvariant: " << this->PieceInvariant << "\n";
  os << indent << "FeatureAngle: " << this->FeatureAngle << "\n";
  os << indent << "EnableFeatureAngle: " << this->EnableFeatureAngle << "\n";
  os << indent << "BorderEdges: " << this->BorderEdges << "\n";
}
VTK_ABI_NAMESPACE_END
