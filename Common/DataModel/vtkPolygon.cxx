// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkPolygon.h"

#include "vtkArrayDispatch.h"
#include "vtkArrayRange.h"
#include "vtkBoundingBox.h"
#include "vtkBox.h"
#include "vtkCellArray.h"
#include "vtkDataSet.h"
#include "vtkDoubleArray.h"
#include "vtkIncrementalPointLocator.h"
#include "vtkLine.h"
#include "vtkLogger.h"
#include "vtkMath.h"
#include "vtkMathUtilities.h"
#include "vtkMergePoints.h"
#include "vtkNew.h"
#include "vtkObjectFactory.h"
#include "vtkPlane.h"
#include "vtkPoints.h"
#include "vtkPriorityQueue.h"
#include "vtkQuad.h"
#include "vtkSmartPointer.h"
#include "vtkTriangle.h"
#include "vtkVector.h"

#include <limits> // For DBL_MAX
#include <vector>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkPolygon);

constexpr double VTK_POLYGON_TOL = 1.e-08; // Absolute tolerance for testing near polygon boundary.
constexpr double VTK_DEFAULT_PLANARITY_TOLERANCE = 0.1; // dZ / max(dX, dY). See ComputeCentroid.

//------------------------------------------------------------------------------
// Instantiate polygon.
vtkPolygon::vtkPolygon()
{
  this->Tris = vtkIdList::New();
  this->Tris->Allocate(VTK_CELL_SIZE);
  this->Triangle = vtkTriangle::New();
  this->Quad = vtkQuad::New();
  this->TriScalars = vtkDoubleArray::New();
  this->TriScalars->Allocate(3);
  this->Line = vtkLine::New();
  this->Tolerance = 1.0e-06;
  this->Tol = 0.0; // Internal tolerance derived from this->Tolerance
  this->SuccessfulTriangulation = 0;
  this->UseMVCInterpolation = false;
}

//------------------------------------------------------------------------------
vtkPolygon::~vtkPolygon()
{
  this->Tris->Delete();
  this->Triangle->Delete();
  this->Quad->Delete();
  this->TriScalars->Delete();
  this->Line->Delete();
}

//------------------------------------------------------------------------------
// Compute the internal tolerance Tol from Tolerance and other geometric
// information.
void vtkPolygon::ComputeTolerance()
{
  const double* bounds = this->GetBounds();

  double d = sqrt((bounds[1] - bounds[0]) * (bounds[1] - bounds[0]) +
    (bounds[3] - bounds[2]) * (bounds[3] - bounds[2]) +
    (bounds[5] - bounds[4]) * (bounds[5] - bounds[4]));

  this->Tol = this->Tolerance * d;
}

//------------------------------------------------------------------------------
double vtkPolygon::ComputeArea()
{
  double normal[3]; // not used, but required for the
                    // following ComputeArea call
  return vtkPolygon::ComputeArea(
    this->GetPoints(), this->GetNumberOfPoints(), this->GetPointIds()->GetPointer(0), normal);
}

//------------------------------------------------------------------------------
bool vtkPolygon::IsConvex()
{
  return vtkPolygon::IsConvex(
    this->GetPoints(), this->GetNumberOfPoints(), this->GetPointIds()->GetPointer(0));
}

constexpr int VTK_POLYGON_FAILURE = -1;
constexpr int VTK_POLYGON_OUTSIDE = 0;
constexpr int VTK_POLYGON_INSIDE = 1;
/* constexpr int VTK_POLYGON_INTERSECTION = 2; */
/* constexpr int VTK_POLYGON_ON_LINE = 3; */

namespace
{
//------------------------------------------------------------------------------
template <bool PointIdRedirection>
vtkIdType GetPointId(const vtkIdType* pts, vtkIdType id);

//------------------------------------------------------------------------------
template <>
vtkIdType GetPointId<true>(const vtkIdType* pts, vtkIdType id)
{
  return pts[id];
}

//------------------------------------------------------------------------------
template <>
vtkIdType GetPointId<false>(const vtkIdType*, vtkIdType id)
{
  return id;
}

//==============================================================================
template <bool PointIdRedirection>
struct NormalWorker
{
  /**
   * To compute the normal, given an arbitrary point C on the plane spanned by the polygon,
   * we accumulate for each segment P_i, P_j (with j = i + 1) the vector
   * (P_i - C) x (P_j - C) where x is the cross product.
   * We set C = P_0 so we can skip the 2 segments where this point exists.
   * If C was set to 0, there could be numerical imprecision on small polygon far from the origin.
   * Setting it to P_0 avoids such caveat while allowing us to save 2 cross products in the
   * computation.
   */
  template <class ArrayT>
  void operator()(ArrayT* p, int numPts, const vtkIdType* pts, double* n)
  {
    auto points = vtk::DataArrayTupleRange<3>(p);
    using PointType = typename decltype(points)::ConstTupleReferenceType;
    double v1[3], v2[3];
    vtkIdType pointId;
    vtkIdType commonPointId = -1;
    for (pointId = 0; pointId < (numPts - 2); ++pointId)
    {
      PointType p0 = points[::GetPointId<PointIdRedirection>(pts, pointId)];
      vtkMath::Subtract(points[::GetPointId<PointIdRedirection>(pts, pointId + 1)], p0, v1);
      if (vtkMath::SquaredNorm(v1) > 0)
      {
        commonPointId = pointId;
        pointId += 2; // consume the two points we just used to obtain a non-zero v1
        break;
      }
    }

    if (pointId >= numPts || commonPointId < 0)
    {
      // Either all the points in the loop were coincident or we used
      // all the points to obtain v1 and have nothing left for v2.
      n[0] = 0;
      n[1] = 0;
      n[2] = 0;
      return;
    }

    PointType p0 = points[::GetPointId<PointIdRedirection>(pts, commonPointId)];
    for (; pointId < numPts; ++pointId)
    {
      vtkMath::Subtract(points[::GetPointId<PointIdRedirection>(pts, pointId)], p0, v2);
      vtkMath::Cross(v1, v2, v1);
      vtkMath::Add(n, v1, n);
      std::swap(v1, v2);
    }
  }
};

//------------------------------------------------------------------------------
template <bool PointIdRedirection>
void ComputeNormal(vtkPoints* p, int numPts, const vtkIdType* pts, double* n)
{
  using Dispatcher = vtkArrayDispatch::DispatchByValueType<vtkArrayDispatch::Reals>;
  ::NormalWorker<PointIdRedirection> worker;
  if (!Dispatcher::Execute(p->GetData(), worker, numPts, pts, n))
  {
    worker(p->GetData(), numPts, pts, n);
  }
}
} // anonymous namespace

//------------------------------------------------------------------------------
//
// In many of the functions that follow, the Points and PointIds members
// of the Cell are assumed initialized.  This is usually done indirectly
// through the GetCell(id) method in the DataSet objects.
//

// Compute the polygon normal from a points list, and a list of point ids
// that index into the points list. Parameter pts can be nullptr, indicating that
// the polygon indexing is {0, 1, ..., numPts-1}. This version will handle
// non-convex polygons.
vtkCellStatus vtkPolygon::ComputeNormal(vtkPoints* p, int numPts, const vtkIdType* pts, double* n)
{
  //
  // Check for special triangle case. Saves extra work.
  //
  n[0] = n[1] = n[2] = 0.0;
  if (numPts < 3)
  {
    return vtkCellStatus::WrongNumberOfPoints;
  }

  if (pts)
  {
    ::ComputeNormal<true>(p, numPts, pts, n);
  }
  else
  {
    ::ComputeNormal<false>(p, numPts, pts, n);
  }

  return (vtkMath::Normalize(n) == 0) ? vtkCellStatus::DegenerateFaces : vtkCellStatus::Valid;
}

//------------------------------------------------------------------------------
// Compute the polygon normal from a points list, and a list of point ids
// that index into the points list. This version will handle non-convex
// polygons.
vtkCellStatus vtkPolygon::ComputeNormal(vtkIdTypeArray* ids, vtkPoints* p, double n[3])
{
  return vtkPolygon::ComputeNormal(p, ids->GetNumberOfTuples(), ids->GetPointer(0), n);
}

//------------------------------------------------------------------------------
// Compute the polygon normal from a list of doubleing points. This version
// will handle non-convex polygons.
vtkCellStatus vtkPolygon::ComputeNormal(vtkPoints* p, double* n)
{
  return vtkPolygon::ComputeNormal(p, p->GetNumberOfPoints(), nullptr, n);
}

//------------------------------------------------------------------------------
// Compute the polygon normal from an array of points. This version assumes
// that the polygon is convex, and looks for the first valid normal.
vtkCellStatus vtkPolygon::ComputeNormal(int numPts, double* pts, double n[3])
{
  int i;
  double *v1, *v2, *v3;
  double length;
  double ax, ay, az;
  double bx, by, bz;

  if (numPts < 3)
  {
    return vtkCellStatus::WrongNumberOfPoints;
  }

  //  Because some polygon vertices are colinear, need to make sure
  //  first non-zero normal is found.
  //
  v1 = pts;
  v2 = pts + 3;
  v3 = pts + 6;

  for (i = 0; i < numPts - 2; i++)
  {
    ax = v2[0] - v1[0];
    ay = v2[1] - v1[1];
    az = v2[2] - v1[2];
    bx = v3[0] - v1[0];
    by = v3[1] - v1[1];
    bz = v3[2] - v1[2];

    n[0] = (ay * bz - az * by);
    n[1] = (az * bx - ax * bz);
    n[2] = (ax * by - ay * bx);

    length = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (length != 0.0)
    {
      n[0] /= length;
      n[1] /= length;
      n[2] /= length;
      return vtkCellStatus::Valid;
    }
    else
    {
      v1 = v2;
      v2 = v3;
      v3 += 3;
    }
  } // over all points
  return vtkCellStatus::DegenerateFaces;
}

//------------------------------------------------------------------------------
// Determine whether or not a polygon is convex from a points list and a list
// of point ids that index into the points list. Parameter pts can be nullptr,
// indicating that the polygon indexing is {0, 1, ..., numPts-1}.
bool vtkPolygon::IsConvex(vtkPoints* p, int numPts, const vtkIdType* pts)
{
  int i;
  double v[3][3], *v0 = v[0], *v1 = v[1], *v2 = v[2], *tmp, a[3], aMag, b[3], bMag;
  double n[3] = { 0., 0., 0. }, ni[3] = { 0., 0., 0. };
  bool nComputed = false;

  if (numPts < 3)
  {
    return false;
  }

  if (numPts == 3)
  {
    return true;
  }

  if (pts)
  {
    p->GetPoint(pts[0], v1);
    p->GetPoint(pts[1], v2);
  }
  else
  {
    p->GetPoint(0, v1);
    p->GetPoint(1, v2);
  }

  for (i = 0; i <= numPts; i++)
  {
    tmp = v0;
    v0 = v1;
    v1 = v2;
    v2 = tmp;

    if (pts)
    {
      p->GetPoint(pts[(i + 2) % numPts], v2);
    }
    else
    {
      p->GetPoint((i + 2) % numPts, v2);
    }

    // order is important!!! to maintain consistency with polygon vertex order
    a[0] = v2[0] - v1[0];
    a[1] = v2[1] - v1[1];
    a[2] = v2[2] - v1[2];
    b[0] = v0[0] - v1[0];
    b[1] = v0[1] - v1[1];
    b[2] = v0[2] - v1[2];

    if (!nComputed)
    {
      aMag = vtkMath::Norm(a);
      bMag = vtkMath::Norm(b);
      if (aMag > VTK_DBL_EPSILON && bMag > VTK_DBL_EPSILON)
      {
        vtkMath::Cross(a, b, n);
        nComputed = vtkMath::Norm(n) > VTK_DBL_EPSILON * (aMag < bMag ? bMag : aMag);
      }
      continue;
    }

    vtkMath::Cross(a, b, ni);
    if (vtkMath::Norm(ni) > VTK_DBL_EPSILON && vtkMath::Dot(n, ni) < 0.)
    {
      return false;
    }
  }

  return true;
}

//------------------------------------------------------------------------------
bool vtkPolygon::IsConvex(vtkIdTypeArray* ids, vtkPoints* p)
{
  return vtkPolygon::IsConvex(p, ids->GetNumberOfTuples(), ids->GetPointer(0));
}

//------------------------------------------------------------------------------
bool vtkPolygon::IsConvex(vtkPoints* p)
{
  return vtkPolygon::IsConvex(p, p->GetNumberOfPoints(), nullptr);
}

//------------------------------------------------------------------------------
int vtkPolygon::EvaluatePosition(const double x[3], double closestPoint[3], int& subId,
  double pcoords[3], double& minDist2, double weights[])
{
  int i;
  double p0[3], p10[3], l10, p20[3], l20, n[3], cp[3];
  double ray[3], bounds[6];

  subId = 0;
  this->ParameterizePolygon(p0, p10, l10, p20, l20, n);
  this->InterpolateFunctions(x, weights);
  vtkPlane::ProjectPoint(x, p0, n, cp);

  for (i = 0; i < 3; i++)
  {
    ray[i] = cp[i] - p0[i];
  }
  pcoords[0] = vtkMath::Dot(ray, p10) / (l10 * l10);
  pcoords[1] = vtkMath::Dot(ray, p20) / (l20 * l20);
  pcoords[2] = 0.0;

  // Make sure that the bounding box has non-zero volume, so that all
  // bounding box sides have non-zero thickness. This prevents tolerancing
  // issues when the polygon lies exactly on a coordinate plane.
  vtkBoundingBox bbox(this->GetBounds());
  bbox.InflateSlice(VTK_POLYGON_TOL);
  bbox.GetBounds(bounds);

  if (pcoords[0] >= 0.0 && pcoords[0] <= 1.0 && pcoords[1] >= 0.0 && pcoords[1] <= 1.0 &&
    (vtkPolygon::PointInPolygon(cp, this->Points->GetNumberOfPoints(),
       static_cast<vtkDoubleArray*>(this->Points->GetData())->GetPointer(0), bounds,
       n) == VTK_POLYGON_INSIDE))
  {
    if (closestPoint)
    {
      closestPoint[0] = cp[0];
      closestPoint[1] = cp[1];
      closestPoint[2] = cp[2];
      minDist2 = vtkMath::Distance2BetweenPoints(x, closestPoint);
    }
    return 1;
  }

  // If here, point is outside of polygon, so need to find distance to boundary
  //
  else
  {
    double t, dist2;
    int numPts;
    double closest[3];
    const double *pt1, *pt2;

    if (closestPoint)
    {
      numPts = this->Points->GetNumberOfPoints();
      // Efficient point access
      const auto pointsArray = vtkDoubleArray::FastDownCast(this->Points->GetData());
      if (!pointsArray)
      {
        vtkErrorMacro(<< "Points should be double type");
        return 0;
      }
      const double* pts = pointsArray->GetPointer(0);
      for (minDist2 = VTK_DOUBLE_MAX, i = 0; i < numPts; i++)
      {
        pt1 = pts + 3 * i;
        pt2 = pts + 3 * ((i + 1) % numPts);
        dist2 = vtkLine::DistanceToLine(x, pt1, pt2, t, closest);
        if (dist2 < minDist2)
        {
          closestPoint[0] = closest[0];
          closestPoint[1] = closest[1];
          closestPoint[2] = closest[2];
          minDist2 = dist2;
        }
      }
    }
    return 0;
  }
}

//------------------------------------------------------------------------------
void vtkPolygon::EvaluateLocation(
  int& vtkNotUsed(subId), const double pcoords[3], double x[3], double* weights)
{
  int i;
  double p0[3], p10[3], l10, p20[3], l20, n[3];

  this->ParameterizePolygon(p0, p10, l10, p20, l20, n);
  for (i = 0; i < 3; i++)
  {
    x[i] = p0[i] + pcoords[0] * p10[i] + pcoords[1] * p20[i];
  }

  this->InterpolateFunctions(x, weights);
}

//------------------------------------------------------------------------------
// Compute interpolation weights using 1/r**2 normalized sum or mean value
// coordinate.
void vtkPolygon::InterpolateFunctions(const double x[3], double* weights)
{
  // Compute interpolation weights using mean value coordinate.
  if (this->UseMVCInterpolation)
  {
    this->InterpolateFunctionsUsingMVC(x, weights);
    return;
  }

  // Compute interpolation weights using 1/r**2 normalized sum.
  int i;
  int numPts = this->Points->GetNumberOfPoints();
  double sum, pt[3];

  for (sum = 0.0, i = 0; i < numPts; i++)
  {
    this->Points->GetPoint(i, pt);
    weights[i] = vtkMath::Distance2BetweenPoints(x, pt);
    if (weights[i] == 0.0) // exact hit
    {
      for (int j = 0; j < numPts; j++)
      {
        weights[j] = 0.0;
      }
      weights[i] = 1.0;
      return;
    }
    else
    {
      weights[i] = 1.0 / weights[i];
      sum += weights[i];
    }
  }

  for (i = 0; i < numPts; i++)
  {
    weights[i] /= sum;
  }
}

//------------------------------------------------------------------------------
// Compute interpolation weights using mean value coordinate.
void vtkPolygon::InterpolateFunctionsUsingMVC(const double x[3], double* weights)
{
  int numPts = this->Points->GetNumberOfPoints();

  // Begin by initializing weights.
  for (int i = 0; i < numPts; i++)
  {
    weights[i] = 0.0;
  }

  // create local array for storing point-to-vertex vectors and distances
  std::vector<double> dist(numPts);
  std::vector<double> uVec(3 * numPts);
  static const double eps = 0.00000001;
  for (int i = 0; i < numPts; i++)
  {
    double pt[3];
    this->Points->GetPoint(i, pt);

    // point-to-vertex vector
    uVec[3 * i] = pt[0] - x[0];
    uVec[3 * i + 1] = pt[1] - x[1];
    uVec[3 * i + 2] = pt[2] - x[2];

    // distance
    dist[i] = vtkMath::Norm(uVec.data() + 3 * i);

    // handle special case when the point is really close to a vertex
    if (dist[i] < eps)
    {
      weights[i] = 1.0;
      return;
    }

    uVec[3 * i] /= dist[i];
    uVec[3 * i + 1] /= dist[i];
    uVec[3 * i + 2] /= dist[i];
  }

  // Now loop over all vertices to compute weight
  // w_i = ( tan(theta_i/2) + tan(theta_(i+1)/2) ) / dist_i
  // To do consider the simplification of
  // tan(alpha/2) = (1-cos(alpha))/sin(alpha)
  //              = (d0*d1 - cross(u0, u1))/(2*dot(u0,u1))
  std::vector<double> tanHalfTheta(numPts);
  for (int i = 0; i < numPts; i++)
  {
    int i1 = i + 1;
    if (i1 == numPts)
    {
      i1 = 0;
    }

    double* u0 = uVec.data() + 3 * i;
    double* u1 = uVec.data() + 3 * i1;

    double l = sqrt(vtkMath::Distance2BetweenPoints(u0, u1));
    double theta = 2.0 * asin(l / 2.0);

    // special case where x lies on an edge
    if (vtkMath::Pi() - theta < 0.001)
    {
      weights[i] = dist[i1] / (dist[i] + dist[i1]);
      weights[i1] = 1 - weights[i];
      return;
    }

    tanHalfTheta[i] = tan(theta / 2.0);
  }

  // Normal case
  for (int i = 0; i < numPts; i++)
  {
    int i1 = i - 1;
    if (i1 == -1)
    {
      i1 = numPts - 1;
    }

    weights[i] = (tanHalfTheta[i] + tanHalfTheta[i1]) / dist[i];
  }

  // normalize weight
  double sum = 0.0;
  for (int i = 0; i < numPts; i++)
  {
    sum += weights[i];
  }

  if (fabs(sum) < eps)
  {
    return;
  }

  for (int i = 0; i < numPts; i++)
  {
    weights[i] /= sum;
  }
}

//------------------------------------------------------------------------------
// Create a local s-t coordinate system for a polygon. The point p0 is
// the origin of the local system, p10 is s-axis vector, and p20 is the
// t-axis vector. (These are expressed in the modelling coordinate system and
// are vectors of dimension [3].) The values l20 and l20 are the lengths of
// the vectors p10 and p20, and n is the polygon normal.
int vtkPolygon::ParameterizePolygon(
  double* p0, double* p10, double& l10, double* p20, double& l20, double* n)
{
  int i, j;
  double s, t, p[3], p1[3], p2[3], sbounds[2], tbounds[2];
  int numPts = this->Points->GetNumberOfPoints();
  double x1[3], x2[3];

  if (numPts < 3)
  {
    return 0;
  }

  //  This is a two pass process: first create a p' coordinate system
  //  that is then adjusted to ensure that the polygon points are all in
  //  the range 0<=s,t<=1.  The p' system is defined by the polygon normal,
  //  first vertex and the first edge.
  //
  this->ComputeNormal(this->Points, n);
  this->Points->GetPoint(0, x1);
  this->Points->GetPoint(1, x2);
  for (i = 0; i < 3; i++)
  {
    p0[i] = x1[i];
    p10[i] = x2[i] - x1[i];
  }
  vtkMath::Cross(n, p10, p20);

  // Determine lengths of edges
  //
  if ((l10 = vtkMath::Dot(p10, p10)) == 0.0 || (l20 = vtkMath::Dot(p20, p20)) == 0.0)
  {
    return 0;
  }

  //  Now evaluate all polygon points to determine min/max parametric
  //  coordinate values.
  //
  // first vertex has (s,t) = (0,0)
  sbounds[0] = 0.0;
  sbounds[1] = 0.0;
  tbounds[0] = 0.0;
  tbounds[1] = 0.0;

  for (i = 1; i < numPts; i++)
  {
    this->Points->GetPoint(i, x1);
    for (j = 0; j < 3; j++)
    {
      p[j] = x1[j] - p0[j];
    }
    s = (p[0] * p10[0] + p[1] * p10[1] + p[2] * p10[2]) / l10;
    t = (p[0] * p20[0] + p[1] * p20[1] + p[2] * p20[2]) / l20;
    sbounds[0] = (s < sbounds[0] ? s : sbounds[0]);
    sbounds[1] = (s > sbounds[1] ? s : sbounds[1]);
    tbounds[0] = (t < tbounds[0] ? t : tbounds[0]);
    tbounds[1] = (t > tbounds[1] ? t : tbounds[1]);
  }

  //  Re-evaluate coordinate system
  //
  for (i = 0; i < 3; i++)
  {
    p1[i] = p0[i] + sbounds[1] * p10[i] + tbounds[0] * p20[i];
    p2[i] = p0[i] + sbounds[0] * p10[i] + tbounds[1] * p20[i];
    p0[i] = p0[i] + sbounds[0] * p10[i] + tbounds[0] * p20[i];
    p10[i] = p1[i] - p0[i];
    p20[i] = p2[i] - p0[i];
  }
  l10 = vtkMath::Norm(p10);
  l20 = vtkMath::Norm(p20);

  return 1;
}

// Support the PointInPolygon algorithm. Determine on which side a point is
// positioned wrt an oriented edge.
namespace
{

// Given the line (p0,p1), determine if a point x is located to the left
// of, on, or to the right of a line (with the function returning >0, ==0, or
// <0 respectively).  The points are assumed 3D points, but projected into
// one of x-y-z planes; hence the indices axis0 and axis1 specify which plane
// the computation is to be performed on.
inline double PointLocation(int axis0, int axis1, double* p0, double* p1, double* x)
{
  return (((p1[axis0] - p0[axis0]) * (x[axis1] - p0[axis1])) -
    ((x[axis0] - p0[axis0]) * (p1[axis1] - p0[axis1])));
}
}

//------------------------------------------------------------------------------
// Determine whether a point is inside a polygon. The function uses a winding
// number calculation generalized to the 3D plane one which the polygon
// resides. Returns 0 if point is not in the polygon; 1 if it is inside.  Can
// also return -1 to indicate a degenerate polygon. This implementation is
// inspired by Dan Sunday's algorithm found in the book Practical Geometry
// Algorithms.
int vtkPolygon::PointInPolygon(double x[3], int numPts, double* pts, double bounds[6], double* n)
{
  // Do a quick bounds check to throw out trivial cases.
  // winding plane.
  if (x[0] < bounds[0] || x[0] > bounds[1] || x[1] < bounds[2] || x[1] > bounds[3] ||
    x[2] < bounds[4] || x[2] > bounds[5])
  {
    return VTK_POLYGON_OUTSIDE;
  }

  //  Check that the normal is non-zero.
  if (vtkMath::Norm(n) <= FLT_EPSILON)
  {
    return VTK_POLYGON_FAILURE;
  }

  // Assess whether the point lies on the boundary of the polygon. Points on
  // the boundary are considered inside the polygon. Need to define a small
  // tolerance relative to the bounding box diagonal length of the polygon.
  double tol2 = VTK_POLYGON_TOL *
    ((bounds[1] - bounds[0]) * (bounds[1] - bounds[0]) +
      (bounds[3] - bounds[2]) * (bounds[3] - bounds[2]) +
      (bounds[5] - bounds[4]) * (bounds[5] - bounds[4]));
  tol2 *= tol2;
  tol2 = (tol2 == 0.0 ? FLT_EPSILON : tol2);

  for (int i = 0; i < numPts; i++)
  {
    // Check coincidence to polygon vertices
    double* p0 = pts + 3 * i;
    if (vtkMath::Distance2BetweenPoints(x, p0) <= tol2)
    {
      return VTK_POLYGON_INSIDE;
    }

    // Check coincidence to polygon edges
    double* p1 = pts + 3 * ((i + 1) % numPts);
    double t;
    double d2 = vtkLine::DistanceToLine(x, p0, p1, t);
    if (d2 <= tol2 && 0.0 < t && t < 1.0)
    {
      return VTK_POLYGON_INSIDE;
    }
  }

  // If here, begin computation of the winding number. This method works for
  // points/polygons arbitrarily oriented in 3D space.  Hence a projection
  // onto one of the x-y-z coordinate planes using the maximum normal
  // component. The computation will be performed in the (axis0,axis1) plane.
  int axis0, axis1;
  if (fabs(n[0]) > fabs(n[1]))
  {
    if (fabs(n[0]) > fabs(n[2]))
    {
      axis0 = 1;
      axis1 = 2;
    }
    else
    {
      axis0 = 0;
      axis1 = 1;
    }
  }
  else
  {
    if (fabs(n[1]) > fabs(n[2]))
    {
      axis0 = 0;
      axis1 = 2;
    }
    else
    {
      axis0 = 0;
      axis1 = 1;
    }
  }

  // Compute the winding number wn. If after processing all polygon edges
  // wn==0, then the point is outside.  Otherwise, the point is inside the
  // polygon. Process all polygon edges determining if there are ascending or
  // descending crossings of the line axis1=constant.
  int wn = 0;
  for (int i = 0; i < numPts; i++)
  {
    double* p0 = pts + 3 * i;
    double* p1 = pts + 3 * ((i + 1) % numPts);

    if (p0[axis1] <= x[axis1])
    {
      if (p1[axis1] > x[axis1]) // if an upward crossing
      {
        if (PointLocation(axis0, axis1, p0, p1, x) > 0) // if x left of edge
        {
          ++wn; // a valid up intersect, increment the winding number
        }
      }
    }
    else
    {
      if (p1[axis1] <= x[axis1]) // if a downward crossing
      {
        if (PointLocation(axis0, axis1, p0, p1, x) < 0) // if x right of edge
        {
          --wn; // a valid down intersect, decrement the winding number
        }
      }
    }
  } // Over all polygon edges

  // A winding number==0 is outside the polygon
  return ((wn == 0 ? VTK_POLYGON_OUTSIDE : VTK_POLYGON_INSIDE));
}

//------------------------------------------------------------------------------
// Split into non-degenerate polygons prior to triangulation
//
int vtkPolygon::NonDegenerateTriangulate(vtkIdList* outTris)
{
  double pt[3], bounds[6];
  vtkIdType ptId, numPts;

  // ComputeBounds does not give the correct bounds
  // So we do it manually
  bounds[0] = VTK_DOUBLE_MAX;
  bounds[1] = -VTK_DOUBLE_MAX;
  bounds[2] = VTK_DOUBLE_MAX;
  bounds[3] = -VTK_DOUBLE_MAX;
  bounds[4] = VTK_DOUBLE_MAX;
  bounds[5] = -VTK_DOUBLE_MAX;

  numPts = this->GetNumberOfPoints();

  for (int i = 0; i < numPts; i++)
  {
    this->Points->GetPoint(i, pt);

    if (pt[0] < bounds[0])
    {
      bounds[0] = pt[0];
    }
    if (pt[1] < bounds[2])
    {
      bounds[2] = pt[1];
    }
    if (pt[2] < bounds[4])
    {
      bounds[4] = pt[2];
    }
    if (pt[0] > bounds[1])
    {
      bounds[1] = pt[0];
    }
    if (pt[1] > bounds[3])
    {
      bounds[3] = pt[1];
    }
    if (pt[2] > bounds[5])
    {
      bounds[5] = pt[2];
    }
  }

  outTris->Reset();
  outTris->Allocate(3 * (2 * numPts - 4));

  vtkPoints* newPts = vtkPoints::New();
  newPts->Allocate(numPts);

  vtkMergePoints* mergePoints = vtkMergePoints::New();
  mergePoints->InitPointInsertion(newPts, bounds);
  mergePoints->SetDivisions(10, 10, 10);

  vtkIdTypeArray* matchingIds = vtkIdTypeArray::New();
  matchingIds->SetNumberOfTuples(numPts);

  int numDuplicatePts = 0;

  for (int i = 0; i < numPts; i++)
  {
    this->Points->GetPoint(i, pt);
    if (mergePoints->InsertUniquePoint(pt, ptId))
    {
      matchingIds->SetValue(i, ptId + numDuplicatePts);
    }
    else
    {
      matchingIds->SetValue(i, ptId + numDuplicatePts);
      numDuplicatePts++;
    }
  }

  mergePoints->Delete();
  newPts->Delete();

  int numPtsRemoved = 0;
  vtkIdType tri[3];

  while (numPtsRemoved < numPts)
  {
    vtkIdType start = 0;
    vtkIdType end = numPts - 1;

    for (; start < numPts; start++)
    {
      if (matchingIds->GetValue(start) >= 0)
      {
        break;
      }
    }

    if (start >= end)
    {
      vtkErrorMacro("ERROR: start >= end");
      break;
    }

    for (int i = start; i < numPts; i++)
    {
      if (matchingIds->GetValue(i) < 0)
      {
        continue;
      }

      if (matchingIds->GetValue(i) != i)
      {
        start = (matchingIds->GetValue(i) + 1) % numPts;
        end = i;

        while (matchingIds->GetValue(start) < 0)
        {
          start++;
        }

        break;
      }
    }

    vtkPolygon* polygon = vtkPolygon::New();
    polygon->Points->SetDataTypeToDouble();

    int numPolygonPts = start < end ? end - start + 1 : end - start + numPts + 1;

    for (int i = 0; i < numPolygonPts; i++)
    {
      ptId = (start + i) % numPts;

      if (matchingIds->GetValue(ptId) >= 0)
      {
        numPtsRemoved++;
        matchingIds->SetValue(ptId, -1);

        polygon->PointIds->InsertNextId(ptId);
        polygon->Points->InsertNextPoint(this->Points->GetPoint(ptId));
      }
    }

    vtkIdList* outTriangles = vtkIdList::New();
    outTriangles->Allocate(3 * (2 * polygon->GetNumberOfPoints() - 4));

    polygon->TriangulateLocalIds(0, outTriangles);

    int outNumTris = outTriangles->GetNumberOfIds();

    for (int i = 0; i < outNumTris; i += 3)
    {
      tri[0] = outTriangles->GetId(i);
      tri[1] = outTriangles->GetId(i + 1);
      tri[2] = outTriangles->GetId(i + 2);

      tri[0] = polygon->PointIds->GetId(tri[0]);
      tri[1] = polygon->PointIds->GetId(tri[1]);
      tri[2] = polygon->PointIds->GetId(tri[2]);

      outTris->InsertNextId(tri[0]);
      outTris->InsertNextId(tri[1]);
      outTris->InsertNextId(tri[2]);
    }

    polygon->Delete();
    outTriangles->Delete();
  }

  matchingIds->Delete();
  return 1;
}

//------------------------------------------------------------------------------
// Triangulate polygon and enforce that the ratio of the smallest triangle area
// to the polygon area is greater than a user-defined tolerance.
int vtkPolygon::BoundedTriangulate(vtkIdList* outTris, double tolerance)
{
  int i, j, k, success = 0, numPts = this->PointIds->GetNumberOfIds();
  double totalArea, area, areaMin;
  double p[3][3];

  for (i = 0; i < numPts; i++)
  {
    success = this->UnbiasedEarCutTriangulation(i, outTris);

    if (!success)
    {
      continue;
    }
    areaMin = DBL_MAX;
    totalArea = 0.;
    for (j = 0; j < numPts - 2; j++)
    {
      for (k = 0; k < 3; k++)
      {
        this->Points->GetPoint(outTris->GetId(3 * j + k), p[k]);
      }
      area = vtkTriangle::TriangleArea(p[0], p[1], p[2]);
      totalArea += area;
      areaMin = std::min(area, areaMin);
    }

    if ((totalArea != 0.) && areaMin / totalArea < tolerance)
    {
      success = 0;
    }
    else
    {
      break;
    }
  }
  return success;
}

// Special triangulation helper class. At some point, we may want to split this
// outside of vtkPolygon. It could be generalized for different polygon
// triangulation methods.
namespace
{ // anonymous
//------------------------------------------------------------------------------
// Special structures for building loops. This is a double-linked list.
typedef struct _vtkPolyVertex
{
  int id;
  double x[3];
  double measure;
  _vtkPolyVertex* next;
  _vtkPolyVertex* previous;
} vtkLocalPolyVertex;

class vtkPolyVertexList
{ // structure to support triangulation
public:
  vtkPolyVertexList(vtkIdList* ptIds, vtkPoints* pts, double tol2, int measure);
  ~vtkPolyVertexList();

  int ComputeNormal();
  double ComputeMeasure(vtkLocalPolyVertex* vtx);
  void RemoveVertex(vtkLocalPolyVertex* vtx, vtkIdList* ids, vtkPriorityQueue* queue = nullptr);
  void RemoveVertex(int i, vtkIdList* ids, vtkPriorityQueue* queue = nullptr);
  int CanRemoveVertex(vtkLocalPolyVertex* vtx);
  int CanRemoveVertex(int id);

  double Tol;
  double Tol2;
  int Measure;

  int NumberOfVerts;
  vtkLocalPolyVertex* Array;
  vtkLocalPolyVertex* Head;
  double Normal[3];
};

//------------------------------------------------------------------------------
// tolerance is squared
vtkPolyVertexList::vtkPolyVertexList(vtkIdList* ptIds, vtkPoints* pts, double tol2, int measure)
{
  this->Tol2 = tol2;
  this->Tol = (tol2 > 0.0 ? sqrt(tol2) : 0.0);
  this->Measure = measure;

  int numVerts = ptIds->GetNumberOfIds();
  this->NumberOfVerts = numVerts;
  this->Array = new vtkLocalPolyVertex[numVerts];
  int i;

  // Load the data into the array.
  for (i = 0; i < numVerts; i++)
  {
    this->Array[i].id = i;
    pts->GetPoint(i, this->Array[i].x);
    this->Array[i].next = (i == (numVerts - 1) ? this->Array : this->Array + i + 1);
    this->Array[i].previous = (i == 0 ? this->Array + numVerts - 1 : this->Array + i - 1);
  }

  // Make sure that there are no coincident vertices.
  // Beware of multiple coincident vertices.
  vtkLocalPolyVertex *vtx, *next;
  this->Head = this->Array;

  for (vtx = this->Head, i = 0; i < numVerts; i++)
  {
    next = vtx->next;
    if (vtkMath::Distance2BetweenPoints(vtx->x, next->x) < tol2)
    {
      next->next->previous = vtx;
      vtx->next = next->next;
      if (next == this->Head)
      {
        this->Head = vtx;
      }
      this->NumberOfVerts--;
    }
    else // can move forward
    {
      vtx = next;
    }
  }
}

//------------------------------------------------------------------------------
vtkPolyVertexList::~vtkPolyVertexList()
{
  delete[] this->Array;
}

//------------------------------------------------------------------------------
// Remove the vertex from the polygon (forming a triangle with
// its previous and next neighbors, and reinsert the neighbors
// into the priority queue).
void vtkPolyVertexList::RemoveVertex(
  vtkLocalPolyVertex* vtx, vtkIdList* tris, vtkPriorityQueue* queue)
{
  // Create triangle
  tris->InsertNextId(vtx->id);
  tris->InsertNextId(vtx->next->id);
  tris->InsertNextId(vtx->previous->id);

  // remove vertex; special case if single triangle left
  if (--this->NumberOfVerts < 3)
  {
    return;
  }
  if (vtx == this->Head)
  {
    this->Head = vtx->next;
  }
  vtx->previous->next = vtx->next;
  vtx->next->previous = vtx->previous;

  // recompute measure, reinsert into queue
  // note that id may have been previously deleted (with Pop()) if we
  // are dealing with a concave polygon and vertex couldn't be split.
  if (queue)
  {
    queue->DeleteId(vtx->previous->id);
    queue->DeleteId(vtx->next->id);
    if (this->ComputeMeasure(vtx->previous) > 0.0)
    {
      queue->Insert(vtx->previous->measure, vtx->previous->id);
    }
    if (this->ComputeMeasure(vtx->next) > 0.0)
    {
      queue->Insert(vtx->next->measure, vtx->next->id);
    }
  }
}

//------------------------------------------------------------------------------
// Remove the vertex from the polygon (forming a triangle with
// its previous and next neighbors, and reinsert the neighbors
// into the priority queue.
void vtkPolyVertexList::RemoveVertex(int i, vtkIdList* tris, vtkPriorityQueue* queue)
{
  this->RemoveVertex(this->Array + i, tris, queue);
}

//------------------------------------------------------------------------------
int vtkPolyVertexList::ComputeNormal()
{
  vtkLocalPolyVertex* vtx = this->Head;
  double v1[3], v2[3], n[3], *anchor = vtx->x;

  this->Normal[0] = this->Normal[1] = this->Normal[2] = 0.0;
  for (vtx = vtx->next; vtx->next != this->Head; vtx = vtx->next)
  {
    v1[0] = vtx->x[0] - anchor[0];
    v1[1] = vtx->x[1] - anchor[1];
    v1[2] = vtx->x[2] - anchor[2];
    v2[0] = vtx->next->x[0] - anchor[0];
    v2[1] = vtx->next->x[1] - anchor[1];
    v2[2] = vtx->next->x[2] - anchor[2];
    vtkMath::Cross(v1, v2, n);
    this->Normal[0] += n[0];
    this->Normal[1] += n[1];
    this->Normal[2] += n[2];
  }
  if (vtkMath::Normalize(this->Normal) == 0.0)
  {
    return 0;
  }
  else
  {
    return 1;
  }
}

//------------------------------------------------------------------------------
// Different measures are supported. Historically, the measure was the ratio
// of triangle perimeter^2 to area (PERIMETER2_TO_AREA_RATIO).  The other
// select for "best quality" triangles (BEST_QUALITY), and the largest dot
// product (DOT_PRODUCT - a measure of apex angle).  The measure is used in a
// priority queue to select the next vertex to remove, hence smaller,
// positive numbers are selected first. Note that concave vertices, or zero
// area vertices, return a negative measure.
double vtkPolyVertexList::ComputeMeasure(vtkLocalPolyVertex* vtx)
{
  double v1[3], v2[3], v3[3], v4[3], area, perimeter;

  for (int i = 0; i < 3; i++)
  {
    v1[i] = vtx->x[i] - vtx->previous->x[i];
    v2[i] = vtx->next->x[i] - vtx->x[i];
    v3[i] = vtx->previous->x[i] - vtx->next->x[i];
  }
  vtkMath::Cross(v1, v2, v4); //|v4| is twice the area
  if ((area = vtkMath::Dot(v4, this->Normal)) < 0.0)
  {
    return (vtx->measure = -1.0); // concave or bad triangle
  }
  else if (area == 0.0)
  {
    return (vtx->measure = -VTK_DOUBLE_MAX); // concave or bad triangle
  }

  // If here, the vertex is convex and the area of the triangle is positive.
  // Compute the specified measure.
  if (this->Measure == vtkPolygon::PERIMETER2_TO_AREA_RATIO)
  {
    // This measure sucks as triangles become "needle-like" but works fine
    // when the triangle is more flattened.
    perimeter = vtkMath::Norm(v1) + vtkMath::Norm(v2) + vtkMath::Norm(v3);
    return (vtx->measure = perimeter * perimeter / area);
  }
  else if (this->Measure == vtkPolygon::DOT_PRODUCT)
  {
    vtkMath::Normalize(v1);
    vtkMath::Normalize(v2);
    return (vtx->measure = (1.0 + vtkMath::Dot(v1, v2)));
  }
  else if (this->Measure == vtkPolygon::BEST_QUALITY)
  {
    // Best quality: ratio of maximum edge length to height.
    // This is a greedy triangulation algorithm, so it may
    // not produce the mesh with the best total quality. However,
    // in greedy fashion it will select the next triangle with the
    // best quality. It is an expensive operation.
    double l1 = vtkMath::Norm(v1);
    double l2 = vtkMath::Norm(v2);
    double l3 = vtkMath::Norm(v3);
    int longestEdge = (l1 > l2 ? (l1 > l3 ? 1 : 3) : (l2 > l3 ? 2 : 3));
    double shortest, longest;
    if (longestEdge == 1)
    {
      longest = l1;
      shortest = vtkLine::DistanceToLine(vtx->next->x, vtx->x, vtx->previous->x);
    }
    else if (longestEdge == 2)
    {
      longest = l2;
      shortest = vtkLine::DistanceToLine(vtx->previous->x, vtx->x, vtx->next->x);
    }
    else
    {
      longest = l3;
      shortest = vtkLine::DistanceToLine(vtx->x, vtx->previous->x, vtx->next->x);
    }

    // sqrt(3)/2 = 0.866025404 comes from equilateral triangle
    return (vtx->measure = (0.866025404 - (shortest / longest)));
  }
  else
  {
    vtkLog(WARNING, "Measure not supported");
    return -1.0;
  }
}

//------------------------------------------------------------------------------
// returns != 0 if vertex can be removed. Uses half-space
// comparison to determine whether ear-cut is valid, and may
// resort to line-plane intersections to resolve possible
// intersections with ear-cut.
int vtkPolyVertexList::CanRemoveVertex(vtkLocalPolyVertex* currentVtx)
{
  double tolerance = this->Tol;
  int i, sign, currentSign;
  double v[3], sN[3], *sPt, val, s, t;
  vtkLocalPolyVertex *previous, *next, *vtx;

  // Check for simple case
  if (this->NumberOfVerts <= 3)
  {
    return 1;
  }

  // Compute split plane, the point to be cut off
  // is always on the positive side of the plane.
  previous = currentVtx->previous;
  next = currentVtx->next;

  sPt = previous->x; // point on plane
  for (i = 0; i < 3; i++)
  {
    v[i] = next->x[i] - previous->x[i]; // vector passing through point
  }

  vtkMath::Cross(v, this->Normal, sN);
  if ((vtkMath::Normalize(sN)) == 0.0)
  {
    return 0; // bad split, indeterminant
  }

  // Traverse the other points to see if a) they are all on the
  // other side of the plane; and if not b) whether they intersect
  // the split line.
  int oneNegative = 0;
  val = vtkPlane::Evaluate(sN, sPt, next->next->x);
  currentSign = (val > tolerance ? 1 : (val < -tolerance ? -1 : 0));
  oneNegative = (currentSign < 0 ? 1 : 0); // very important

  // Intersections are only computed when the split half-space is crossed
  for (vtx = next->next->next; vtx != previous; vtx = vtx->next)
  {
    val = vtkPlane::Evaluate(sN, sPt, vtx->x);
    sign = (val > tolerance ? 1 : (val < -tolerance ? -1 : 0));
    if (sign != currentSign)
    {
      if (!oneNegative)
      {
        oneNegative = (sign < 0 ? 1 : 0); // very important
      }
      if (vtkLine::Intersection(
            sPt, next->x, vtx->x, vtx->previous->x, s, t, tolerance, vtkLine::AbsoluteFuzzy) != 0)
      {
        return 0;
      }
      else
      {
        currentSign = sign;
      }
    } // if crossing occurs
  }   // for the rest of the loop

  if (!oneNegative)
  {
    return 0; // entire loop is on this side of plane
  }
  else
  {
    return 1;
  }
}

//------------------------------------------------------------------------------
// returns != 0 if vertex can be removed. Uses half-space
// comparison to determine whether ear-cut is valid, and may
// resort to line-plane intersections to resolve possible
// intersections with ear-cut.
int vtkPolyVertexList::CanRemoveVertex(int id)
{
  return this->CanRemoveVertex(this->Array + id);
}

//------------------------------------------------------------------------------
// Handles some trivial triangulation cases. Returns 0 if cannot triangulate
// the current polygon. 3 and 4 points are handled with special care for concave quad.
int SimpleTriangulation(vtkIdList* ptIds, vtkPoints* pts, double tol2, vtkIdList* tris)
{
  int number_of_verts = ptIds->GetNumberOfIds();
  // Just output the single triangle
  if (number_of_verts == 3)
  {
    double x0[3], x1[3], x2[3];
    bool valid = true;
    pts->GetPoint(0, x0);
    pts->GetPoint(1, x1);
    pts->GetPoint(2, x2);
    if (vtkMath::Distance2BetweenPoints(x0, x1) < tol2 ||
      vtkMath::Distance2BetweenPoints(x1, x2) < tol2 ||
      vtkMath::Distance2BetweenPoints(x0, x2) < tol2)
    {
      valid = false;
    }
    if (valid)
    {
      tris->SetNumberOfIds(3);
      std::iota(tris->begin(), tris->end(), 0);
      return 1;
    }
  }

  // Four points are split into two triangles. Watch out for the
  // concave case (i.e., quad looks like a arrowhead).
  else if (number_of_verts == 4)
  {
    // There are only two ear cutting possibility.
    // This boolean
    bool use_d1 = true;
    bool concave = false;
    // Temporary storage of the four points
    double x0[3], x1[3], x2[3], x3[3];
    // Quad possible diagonals with d1 and d2
    double d1[3], d2[3];
    // complementary vector to analyse fan
    double v1[3], v3[3];
    // face normal
    double normal[3];
    // local tri normal
    double n1[3], n2[3];
    double area;

    pts->GetPoint(0, x0);
    pts->GetPoint(1, x1);
    pts->GetPoint(2, x2);
    pts->GetPoint(3, x3);
    // Build diagonals for ear cutting
    d1[0] = x2[0] - x0[0];
    d1[1] = x2[1] - x0[1];
    d1[2] = x2[2] - x0[2];
    //
    d2[0] = x3[0] - x1[0];
    d2[1] = x3[1] - x1[1];
    d2[2] = x3[2] - x1[2];

    double d1_n2 = vtkMath::SquaredNorm(d1);
    double d2_n2 = vtkMath::SquaredNorm(d2);
    if (d1_n2 < d2_n2)
    {
      use_d1 = true;
      // check diagonal validity
      if (d1_n2 < tol2)
      {
        return 0;
      }
      // prepare vector for fan building
      v1[0] = x1[0] - x0[0];
      v1[1] = x1[1] - x0[1];
      v1[2] = x1[2] - x0[2];
      v3[0] = x3[0] - x0[0];
      v3[1] = x3[1] - x0[1];
      v3[2] = x3[2] - x0[2];
    }
    else
    {
      use_d1 = false;
      // check diagonal validity
      if (d2_n2 < tol2)
      {
        return 0;
      }
      // prepare vector for fan building
      v1[0] = x2[0] - x1[0];
      v1[1] = x2[1] - x1[1];
      v1[2] = x2[2] - x1[2];
      v3[0] = x0[0] - x1[0];
      v3[1] = x0[1] - x1[1];
      v3[2] = x0[2] - x1[2];
    }
    // Check points validity
    if (vtkMath::SquaredNorm(v1) < tol2)
    {
      return 0;
    }
    if (vtkMath::SquaredNorm(v3) < tol2)
    {
      return 0;
    }
    // build polygon normal to get coherent result with earcut algo
    if (use_d1)
    {
      vtkMath::Cross(v1, d1, n1);
      vtkMath::Cross(d1, v3, n2);
    }
    else
    {
      vtkMath::Cross(v1, d2, n1);
      vtkMath::Cross(d2, v3, n2);
    }
    // Indirect check points validity
    if (vtkMath::SquaredNorm(n1) < tol2)
    {
      return 0;
    }
    if (vtkMath::SquaredNorm(n2) < tol2)
    {
      return 0;
    }
    // Now finalize the normal building
    normal[0] = n1[0] + n2[0];
    normal[1] = n1[1] + n2[1];
    normal[2] = n1[2] + n2[2];
    if (vtkMath::Normalize(normal) == 0.0)
    {
      return 0;
    }

    // check for concave or invalid case
    if ((area = vtkMath::Dot(n1, normal)) < 0.0)
    {
      concave = true;
    }
    else if (area == 0.0)
    {
      return 0;
    }
    else
    {
      if ((area = vtkMath::Dot(n2, normal)) < 0.0)
      {
        concave = true;
      }
      else if (area == 0.0)
      {
        return 0;
      }
    }
    // Best possible case has concavity
    // Try the opposite case
    if (concave)
    {
      use_d1 = use_d1 != concave; // switch use_d1 if concave is true
      // Check concavity of opposite triangulation
      // Two cases:
      // - arrowhead is OK
      // - self intersecting like quad is KO
      if (use_d1)
      {
        v1[0] = x1[0] - x0[0];
        v1[1] = x1[1] - x0[1];
        v1[2] = x1[2] - x0[2];
        v3[0] = x3[0] - x0[0];
        v3[1] = x3[1] - x0[1];
        v3[2] = x3[2] - x0[2];
        vtkMath::Cross(v1, d2, n1);
        vtkMath::Cross(d2, v3, n2);
      }
      else
      {
        v1[0] = x2[0] - x1[0];
        v1[1] = x2[1] - x1[1];
        v1[2] = x2[2] - x1[2];
        v3[0] = x0[0] - x1[0];
        v3[1] = x0[1] - x1[1];
        v3[2] = x0[2] - x1[2];
        vtkMath::Cross(v1, d1, n1);
        vtkMath::Cross(d1, v3, n2);
      }
      // Check points validity
      if (vtkMath::SquaredNorm(v1) < tol2)
      {
        return 0;
      }
      if (vtkMath::SquaredNorm(v3) < tol2)
      {
        return 0;
      }
      // check for invalid case
      if (vtkMath::Dot(n1, normal) <= 0.0)
      {
        return 0;
      }
      if (vtkMath::Dot(n2, normal) <= 0.0)
      {
        return 0;
      }
    }

    // Finalize the tris
    tris->SetNumberOfIds(6);
    if (use_d1)
    {
      constexpr std::array<vtkIdType, 6> localPtIds{ 0, 1, 2, 0, 2, 3 };
      std::copy(localPtIds.begin(), localPtIds.end(), tris->begin());
    }
    else
    {
      constexpr std::array<vtkIdType, 6> localPtIds{ 0, 1, 3, 1, 2, 3 };
      std::copy(localPtIds.begin(), localPtIds.end(), tris->begin());
    }
    return 1;
  } // if simple cases

  return 0;
}
} // anonymous namespace

//------------------------------------------------------------------------------
// Triangulation method based on ear-cutting. Triangles, or ears, are
// repeatedly cut off from the polygon based on a measure of the
// vertex. Vertices must be convex, but different measures will produce
// different triangulations. While the algorithm works in 3D (the points
// don't have to be projected into 2D), it is assumed the polygon is planar -
// if not, poor results may occur.
int vtkPolygon::EarCutTriangulation(vtkIdList* outTris, int measure)
{
  // Initialize the list of output triangles
  outTris->Reset();

  // Make sure there are at least 3 vertices
  if (this->PointIds->GetNumberOfIds() < 3)
  {
    return (this->SuccessfulTriangulation = 0);
  }

  // Compute the tolerance local to this polygon
  this->ComputeTolerance();

  // Check for trivial triangulation cases
  if (::SimpleTriangulation(this->PointIds, this->Points, this->Tol * this->Tol, outTris))
  {
    return (this->SuccessfulTriangulation = 1);
  }

  // Establish a more convenient structure for the triangulation process
  vtkPolyVertexList poly(this->PointIds, this->Points, this->Tol * this->Tol, measure);
  vtkLocalPolyVertex* vtx;
  int i, id;

  // The polygon normal is needed during triangulation
  //
  if (!poly.ComputeNormal())
  {
    return (this->SuccessfulTriangulation = 0);
  }

  // Now compute the angles between edges incident to each
  // vertex. Place the structure into a priority queue (those
  // vertices with smallest measure are to be removed first).
  //
  vtkPriorityQueue* VertexQueue = vtkPriorityQueue::New();
  VertexQueue->Allocate(poly.NumberOfVerts);
  for (i = 0, vtx = poly.Head; i < poly.NumberOfVerts; i++, vtx = vtx->next)
  {
    // concave (negative measure) vertices are not eligible for removal
    if (poly.ComputeMeasure(vtx) > 0.0)
    {
      VertexQueue->Insert(vtx->measure, vtx->id);
    }
  }

  // For each vertex in the priority queue, and as long as there
  // are three or more vertices, remove the vertex (if possible)
  // and create a new triangle. NOTE: at one time this code checked the
  // number of verts in the removal queue, and if it was equal to the number
  // of remaining vertices, it assumed a convex polygon and indiscrimately
  // removed vertices. This tends to produce bad results as some triangles
  // were nearly flat etc. so the code was removed.
  //
  int numInQueue;
  while (poly.NumberOfVerts > 2 && (numInQueue = VertexQueue->GetNumberOfItems()) > 0)
  {
    id = VertexQueue->Pop(); // removes it, even if can't be split
    if (poly.CanRemoveVertex(id))
    {
      poly.RemoveVertex(id, outTris, VertexQueue);
    }
  } // while

  // Clean up
  VertexQueue->Delete();

  if (poly.NumberOfVerts > 2) // couldn't triangulate
  {
    return (this->SuccessfulTriangulation = 0);
  }
  return (this->SuccessfulTriangulation = 1);
}

//------------------------------------------------------------------------------
// Copies the results of triangulation into provided id list
int vtkPolygon::EarCutTriangulation(int measure)
{
  return this->EarCutTriangulation(this->Tris, measure);
}

//------------------------------------------------------------------------------
// Triangulation method based on ear-cutting. Triangles, or ears, are cut off
// from the polygon. This implementation does not bias the selection of ears;
// it sequentially progresses through each vertex starting at a user-defined
// seed value.
int vtkPolygon::UnbiasedEarCutTriangulation(int seed, vtkIdList* outTris, int measure)
{
  // Compute the tolerance local to this polygon
  this->ComputeTolerance();

  // Establish a more convenient structure for triangulation
  vtkPolyVertexList poly(this->PointIds, this->Points, this->Tol * this->Tol, measure);

  // First compute the polygon normal the correct way
  //
  outTris->Reset();
  if (!poly.ComputeNormal())
  {
    return (this->SuccessfulTriangulation = 0);
  }

  seed = abs(seed) % poly.NumberOfVerts;
  vtkLocalPolyVertex* vtx = poly.Array + seed;

  int marker = -1;

  while (poly.NumberOfVerts > 2)
  {
    if (poly.CanRemoveVertex(vtx))
    {
      poly.RemoveVertex(vtx, outTris);
    }
    vtx = vtx->next;

    if (vtx == poly.Head)
    {
      if (poly.NumberOfVerts == marker)
      {
        break;
      }
      marker = poly.NumberOfVerts;
    }
  }

  if (poly.NumberOfVerts > 2) // couldn't triangulate
  {
    return (this->SuccessfulTriangulation = 0);
  }
  return (this->SuccessfulTriangulation = 1);
}

//------------------------------------------------------------------------------
// Copies the results of triangulation into provided id list
int vtkPolygon::UnbiasedEarCutTriangulation(int seed, int measure)
{
  return this->UnbiasedEarCutTriangulation(seed, this->Tris, measure);
}

//------------------------------------------------------------------------------
int vtkPolygon::CellBoundary(int vtkNotUsed(subId), const double pcoords[3], vtkIdList* pts)
{
  int i, numPts = this->PointIds->GetNumberOfIds();
  double x[3];
  int closestPoint = 0, previousPoint, nextPoint;
  double largestWeight = 0.0;
  double p0[3], p10[3], l10, p20[3], l20, n[3];

  pts->Reset();
  std::vector<double> weights(numPts);

  // determine global coordinates given parametric coordinates
  this->ParameterizePolygon(p0, p10, l10, p20, l20, n);
  for (i = 0; i < 3; i++)
  {
    x[i] = p0[i] + pcoords[0] * p10[i] + pcoords[1] * p20[i];
  }

  // find edge with largest and next largest weight values. This will be
  // the closest edge.
  this->InterpolateFunctions(x, weights.data());
  for (i = 0; i < numPts; i++)
  {
    if (weights[i] > largestWeight)
    {
      closestPoint = i;
      largestWeight = weights[i];
    }
  }

  pts->InsertId(0, this->PointIds->GetId(closestPoint));

  previousPoint = closestPoint - 1;
  nextPoint = closestPoint + 1;
  if (previousPoint < 0)
  {
    previousPoint = numPts - 1;
  }
  if (nextPoint >= numPts)
  {
    nextPoint = 0;
  }

  if (weights[previousPoint] > weights[nextPoint])
  {
    pts->InsertId(1, this->PointIds->GetId(previousPoint));
  }
  else
  {
    pts->InsertId(1, this->PointIds->GetId(nextPoint));
  }

  // determine whether point is inside of polygon
  if (pcoords[0] >= 0.0 && pcoords[0] <= 1.0 && pcoords[1] >= 0.0 && pcoords[1] <= 1.0 &&
    (this->PointInPolygon(x, this->Points->GetNumberOfPoints(),
       static_cast<vtkDoubleArray*>(this->Points->GetData())->GetPointer(0), this->GetBounds(),
       n) == VTK_POLYGON_INSIDE))
  {
    return 1;
  }
  else
  {
    return 0;
  }
}

//------------------------------------------------------------------------------
void vtkPolygon::Contour(double value, vtkDataArray* cellScalars,
  vtkIncrementalPointLocator* locator, vtkCellArray* verts, vtkCellArray* lines,
  vtkCellArray* polys, vtkPointData* inPd, vtkPointData* outPd, vtkCellData* inCd, vtkIdType cellId,
  vtkCellData* outCd)
{
  int i, success;
  int p1, p2, p3;

  this->TriScalars->SetNumberOfTuples(3);

  this->SuccessfulTriangulation = 1;
  success = this->EarCutTriangulation(this->Tris);

  if (!success) // Just skip for now.
  {
  }
  else // Contour triangle
  {
    for (i = 0; i < this->Tris->GetNumberOfIds(); i += 3)
    {
      p1 = this->Tris->GetId(i);
      p2 = this->Tris->GetId(i + 1);
      p3 = this->Tris->GetId(i + 2);

      this->Triangle->Points->SetPoint(0, this->Points->GetPoint(p1));
      this->Triangle->Points->SetPoint(1, this->Points->GetPoint(p2));
      this->Triangle->Points->SetPoint(2, this->Points->GetPoint(p3));

      if (outPd)
      {
        this->Triangle->PointIds->SetId(0, this->PointIds->GetId(p1));
        this->Triangle->PointIds->SetId(1, this->PointIds->GetId(p2));
        this->Triangle->PointIds->SetId(2, this->PointIds->GetId(p3));
      }

      this->TriScalars->SetTuple(0, cellScalars->GetTuple(p1));
      this->TriScalars->SetTuple(1, cellScalars->GetTuple(p2));
      this->TriScalars->SetTuple(2, cellScalars->GetTuple(p3));

      this->Triangle->Contour(
        value, this->TriScalars, locator, verts, lines, polys, inPd, outPd, inCd, cellId, outCd);
    }
  }
}

//------------------------------------------------------------------------------
vtkCell* vtkPolygon::GetEdge(int edgeId)
{
  int numPts = this->Points->GetNumberOfPoints();

  // load point id's
  this->Line->PointIds->SetId(0, this->PointIds->GetId(edgeId));
  this->Line->PointIds->SetId(1, this->PointIds->GetId((edgeId + 1) % numPts));

  // load coordinates
  this->Line->Points->SetPoint(0, this->Points->GetPoint(edgeId));
  this->Line->Points->SetPoint(1, this->Points->GetPoint((edgeId + 1) % numPts));

  return this->Line;
}

//------------------------------------------------------------------------------
//
// Intersect this plane with finite line defined by p1 & p2 with tolerance tol.
//
int vtkPolygon::IntersectWithLine(const double p1[3], const double p2[3], double tol, double& t,
  double x[3], double pcoords[3], int& subId)
{
  double pt1[3], n[3];
  double tol2 = tol * tol;
  double closestPoint[3];
  double dist2;
  int npts = this->GetNumberOfPoints();

  subId = 0;
  pcoords[0] = pcoords[1] = pcoords[2] = 0.0;

  // Define a plane to intersect with
  //
  this->Points->GetPoint(1, pt1);
  this->ComputeNormal(this->Points, n);

  // Intersect plane of the polygon with line
  //
  if (!vtkPlane::IntersectWithLine(p1, p2, n, pt1, t, x))
  {
    return 0;
  }

  // Evaluate position
  //
  std::vector<double> weights(npts);
  if (this->EvaluatePosition(x, closestPoint, subId, pcoords, dist2, weights.data()) >= 0)
  {
    if (dist2 <= tol2)
    {
      return 1;
    }
  }
  return 0;
}

//------------------------------------------------------------------------------
int vtkPolygon::TriangulateLocalIds(int vtkNotUsed(index), vtkIdList* ptIds)
{
  this->SuccessfulTriangulation = 1;
  int success = this->EarCutTriangulation(ptIds);
  if (!success) // Indicate possible failure
  {
    vtkDebugMacro(<< "Possible triangulation failure");
  }
  return this->SuccessfulTriangulation;
}

//------------------------------------------------------------------------------
// Samples at three points to compute derivatives in local r-s coordinate
// system and projects vectors into 3D model coordinate system.
// Note that the results are usually inaccurate because
// this method actually returns the derivative of the interpolation
// function which is obtained using 1/r**2 normalized sum.
#define VTK_SAMPLE_DISTANCE 0.01
void vtkPolygon::Derivatives(
  int vtkNotUsed(subId), const double pcoords[3], const double* values, int dim, double* derivs)
{
  int i, j, k, idx;

  if (this->Points->GetNumberOfPoints() == 4)
  {
    for (i = 0; i < 4; i++)
    {
      this->Quad->Points->SetPoint(i, this->Points->GetPoint(i));
    }
    this->Quad->Derivatives(0, pcoords, values, dim, derivs);
    return;
  }
  else if (this->Points->GetNumberOfPoints() == 3)
  {
    for (i = 0; i < 3; i++)
    {
      this->Triangle->Points->SetPoint(i, this->Points->GetPoint(i));
    }
    this->Triangle->Derivatives(0, pcoords, values, dim, derivs);
    return;
  }

  double p0[3], p10[3], l10, p20[3], l20, n[3];
  double x[3][3], l1, l2, v1[3], v2[3];

  // setup parametric system and check for degeneracy
  if (this->ParameterizePolygon(p0, p10, l10, p20, l20, n) == 0)
  {
    for (j = 0; j < dim; j++)
    {
      for (i = 0; i < 3; i++)
      {
        derivs[j * dim + i] = 0.0;
      }
    }
    return;
  }

  int numVerts = this->PointIds->GetNumberOfIds();
  std::vector<double> weights(numVerts);
  std::vector<double> sample(dim * 3);

  // compute positions of three sample points
  for (i = 0; i < 3; i++)
  {
    x[0][i] = p0[i] + pcoords[0] * p10[i] + pcoords[1] * p20[i];
    x[1][i] = p0[i] + (pcoords[0] + VTK_SAMPLE_DISTANCE) * p10[i] + pcoords[1] * p20[i];
    x[2][i] = p0[i] + pcoords[0] * p10[i] + (pcoords[1] + VTK_SAMPLE_DISTANCE) * p20[i];
  }

  // for each sample point, sample data values
  for (idx = 0, k = 0; k < 3; k++) // loop over three sample points
  {
    this->InterpolateFunctions(x[k], weights.data());
    for (j = 0; j < dim; j++, idx++) // over number of derivates requested
    {
      sample[idx] = 0.0;
      for (i = 0; i < numVerts; i++)
      {
        sample[idx] += weights[i] * values[j + i * dim];
      }
    }
  }

  // compute differences along the two axes
  for (i = 0; i < 3; i++)
  {
    v1[i] = x[1][i] - x[0][i];
    v2[i] = x[2][i] - x[0][i];
  }
  l1 = vtkMath::Normalize(v1);
  l2 = vtkMath::Normalize(v2);

  // compute derivatives along x-y-z axes
  double ddx, ddy;
  for (j = 0; j < dim; j++)
  {
    ddx = (sample[dim + j] - sample[j]) / l1;
    ddy = (sample[2 * dim + j] - sample[j]) / l2;

    // project onto global x-y-z axes
    derivs[3 * j] = ddx * v1[0] + ddy * v2[0];
    derivs[3 * j + 1] = ddx * v1[1] + ddy * v2[1];
    derivs[3 * j + 2] = ddx * v1[2] + ddy * v2[2];
  }
}

//------------------------------------------------------------------------------
void vtkPolygon::Clip(double value, vtkDataArray* cellScalars, vtkIncrementalPointLocator* locator,
  vtkCellArray* tris, vtkPointData* inPD, vtkPointData* outPD, vtkCellData* inCD, vtkIdType cellId,
  vtkCellData* outCD, int insideOut)
{
  int i, success;
  int p1, p2, p3;

  this->TriScalars->SetNumberOfTuples(3);

  this->SuccessfulTriangulation = 1;
  success = this->EarCutTriangulation(this->Tris);

  if (success) // clip triangles
  {
    for (i = 0; i < this->Tris->GetNumberOfIds(); i += 3)
    {
      p1 = this->Tris->GetId(i);
      p2 = this->Tris->GetId(i + 1);
      p3 = this->Tris->GetId(i + 2);

      this->Triangle->Points->SetPoint(0, this->Points->GetPoint(p1));
      this->Triangle->Points->SetPoint(1, this->Points->GetPoint(p2));
      this->Triangle->Points->SetPoint(2, this->Points->GetPoint(p3));

      this->Triangle->PointIds->SetId(0, this->PointIds->GetId(p1));
      this->Triangle->PointIds->SetId(1, this->PointIds->GetId(p2));
      this->Triangle->PointIds->SetId(2, this->PointIds->GetId(p3));

      this->TriScalars->SetTuple(0, cellScalars->GetTuple(p1));
      this->TriScalars->SetTuple(1, cellScalars->GetTuple(p2));
      this->TriScalars->SetTuple(2, cellScalars->GetTuple(p3));

      this->Triangle->Clip(
        value, this->TriScalars, locator, tris, inPD, outPD, inCD, cellId, outCD, insideOut);
    }
  }
}

//------------------------------------------------------------------------------
// Method intersects two polygons. You must supply the number of points and
// point coordinates (npts, *pts) and the bounding box (bounds) of the two
// polygons. Also supply a tolerance squared for controlling
// error. The method returns 1 if there is an intersection, and 0 if
// not. A single point of intersection x[3] is also returned if there
// is an intersection.
int vtkPolygon::IntersectPolygonWithPolygon(int npts, double* pts, double bounds[6], int npts2,
  double* pts2, double bounds2[6], double tol2, double x[3])
{
  double n[3], coords[3];
  int i, j;
  double *p1, *p2, ray[3];
  double t;

  //  Intersect each edge of first polygon against second
  //
  vtkPolygon::ComputeNormal(npts2, pts2, n);

  for (i = 0; i < npts; i++)
  {
    p1 = pts + 3 * i;
    p2 = pts + 3 * ((i + 1) % npts);

    for (j = 0; j < 3; j++)
    {
      ray[j] = p2[j] - p1[j];
    }
    if (!vtkBox::IntersectBox(bounds2, p1, ray, coords, t))
    {
      continue;
    }

    if ((vtkPlane::IntersectWithLine(p1, p2, n, pts2, t, x)) == 1)
    {
      if ((npts2 == 3 && vtkTriangle::PointInTriangle(x, pts2, pts2 + 3, pts2 + 6, tol2)) ||
        (npts2 > 3 && vtkPolygon::PointInPolygon(x, npts2, pts2, bounds2, n) == VTK_POLYGON_INSIDE))
      {
        return 1;
      }
    }
    else
    {
      return 0;
    }
  }

  //  Intersect each edge of second polygon against first
  //
  vtkPolygon::ComputeNormal(npts, pts, n);

  for (i = 0; i < npts2; i++)
  {
    p1 = pts2 + 3 * i;
    p2 = pts2 + 3 * ((i + 1) % npts2);

    for (j = 0; j < 3; j++)
    {
      ray[j] = p2[j] - p1[j];
    }

    if (!vtkBox::IntersectBox(bounds, p1, ray, coords, t))
    {
      continue;
    }

    if ((vtkPlane::IntersectWithLine(p1, p2, n, pts, t, x)) == 1)
    {
      if ((npts == 3 && vtkTriangle::PointInTriangle(x, pts, pts + 3, pts + 6, tol2)) ||
        (npts > 3 && vtkPolygon::PointInPolygon(x, npts, pts, bounds, n) == VTK_POLYGON_INSIDE))
      {
        return 1;
      }
    }
    else
    {
      return 0;
    }
  }

  return 0;
}

//------------------------------------------------------------------------------
// Compute the area of the polygon (oriented in 3D space). It uses an
// efficient approach where the area is computed in 2D and then projected into
// 3D space.
double vtkPolygon::ComputeArea(vtkPoints* p, vtkIdType numPts, const vtkIdType* pts, double n[3])
{
  if (numPts < 3)
  {
    return 0.0;
  }
  else
  {
    double area = 0.0;
    double nx, ny, nz;
    int coord, i;

    vtkPolygon::ComputeNormal(p, numPts, pts, n);

    // Select the projection direction
    nx = (n[0] > 0.0 ? n[0] : -n[0]); // abs x-coord
    ny = (n[1] > 0.0 ? n[1] : -n[1]); // abs y-coord
    nz = (n[2] > 0.0 ? n[2] : -n[2]); // abs z-coord

    coord = (nx > ny ? (nx > nz ? 0 : 2) : (ny > nz ? 1 : 2));

    // compute area of the 2D projection
    double x0[3], x1[3], x2[3], *v0, *v1, *v2;
    v0 = x0;
    v1 = x1;
    v2 = x2;

    for (i = 0; i < numPts; i++)
    {
      if (pts)
      {
        p->GetPoint(pts[i], v0);
        p->GetPoint(pts[(i + 1) % numPts], v1);
        p->GetPoint(pts[(i + 2) % numPts], v2);
      }
      else
      {
        p->GetPoint(i, v0);
        p->GetPoint((i + 1) % numPts, v1);
        p->GetPoint((i + 2) % numPts, v2);
      }
      switch (coord)
      {
        case 0:
          area += v1[1] * (v2[2] - v0[2]);
          continue;
        case 1:
          area += v1[0] * (v2[2] - v0[2]);
          continue;
        case 2:
          area += v1[0] * (v2[1] - v0[1]);
          continue;
      }
    }

    // scale to get area before projection
    switch (coord)
    {
      case 0:
        area /= (2.0 * nx);
        break;
      case 1:
        area /= (2.0 * ny);
        break;
      case 2:
        area /= (2.0 * nz);
    }
    return fabs(area);
  } // general polygon
}

//------------------------------------------------------------------------------
void vtkPolygon::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Tolerance: " << this->Tolerance << "\n";
  os << indent << "SuccessfulTriangulation: " << this->SuccessfulTriangulation << "\n";
  os << indent << "UseMVCInterpolation: " << this->UseMVCInterpolation << "\n";
  os << indent << "Tris:\n";
  this->Tris->PrintSelf(os, indent.GetNextIndent());
  os << indent << "Triangle:\n";
  this->Triangle->PrintSelf(os, indent.GetNextIndent());
  os << indent << "Quad:\n";
  this->Quad->PrintSelf(os, indent.GetNextIndent());
  os << indent << "TriScalars:\n";
  this->TriScalars->PrintSelf(os, indent.GetNextIndent());
  os << indent << "Line:\n";
  this->Line->PrintSelf(os, indent.GetNextIndent());
}

//------------------------------------------------------------------------------
// Compute the polygon centroid from a points list, the number of points, and an
// array of point ids that index into the points list. Returns false if the
// computation is invalid.
vtkCellStatus vtkPolygon::ComputeCentroid(
  vtkPoints* p, int numPts, const vtkIdType* ids, double c[3], double tolerance)
{
  if (numPts < 2)
  {
    return vtkCellStatus::WrongNumberOfPoints;
  }

  vtkVector3d normal;
  auto status = vtkPolygon::ComputeNormal(p, numPts, ids, normal.GetData());
  if (!status)
  {
    return status;
  }

  // Set xx to be the average coordinate. This is not necessarily the centroid
  // but will generally produce accurate triangle areas used to compute the centroid.
  vtkVector3d xx(0, 0, 0);
  vtkVector3d pp;
  vtkVector3d qq;
  double wt = 1. / numPts;
  for (int ii = 0; ii < numPts; ++ii)
  {
    p->GetPoint(ids[ii], pp.GetData());
    xx += wt * pp;
  }
  // Note that pp now contains the final point in the polygon.
  // If we start again with the first point, we can track pairs
  // of points along edges.

  // Now compute the centroid of each triangle formed by xx and
  // the endpoints of one edge in the polygon. Weight the
  // centroid by the triangle's signed area (negative if the polygon
  // winds clockwise) and sum them together.
  //
  // This is equivalent to computing (Integral(x_i dA) / Integral(dA))
  // for each coordinate (x_0, x_1, x_2) using the "geometric decomposition"
  // method.
  double totalArea = 0.;
  double area;
  vtkVector3d accum(0, 0, 0);
  vtkVector3d ctr;
  double outOfPlane = 0;
  double inPlane2 = 0;
  for (int ii = 0; ii < numPts; ++ii, pp = qq)
  {
    p->GetPoint(ids[ii], qq.GetData());
    // The centroid of xx-pp-qq is 2/3 of the way from xx to the midpoint of qq-pp
    auto pq = (pp + qq) * 0.5;
    ctr = (1. / 3. * xx) + (2. / 3. * pq);
    auto dqx = qq - xx;
    area = ((pp - xx).Cross(dqx)).Dot(normal) / 2;
    accum += area * ctr;
    totalArea += area;
    // Compute the in-plane and out-of-plane distance from xx to qq.
    // Note that because xx is the average coordinate, oop and
    // ip2 will both be half-distances; their ratio will be correct
    // for comparison to tolerance.
    double oop = std::abs(dqx.Dot(normal)); // out-of-plane distance
    if (oop > outOfPlane)
    {
      outOfPlane = oop;
    }
    double ip2 = (dqx - oop * normal).SquaredNorm();
    if (ip2 > inPlane2)
    {
      inPlane2 = ip2;
    }
  }
  // Fail if the polygon is too far from planarity and the tolerance is "active":
  if (tolerance > 0. && outOfPlane / std::sqrt(inPlane2) > tolerance)
  {
    return vtkCellStatus::NonPlanarFaces;
  }
  // Divide the accumulated product of weighted centroids by the total area
  // of all the triangles. This produces the final centroid.
  accum = (1. / totalArea) * accum;
  c[0] = accum[0];
  c[1] = accum[1];
  c[2] = accum[2];
  return vtkCellStatus::Valid;
}

bool vtkPolygon::ComputeCentroid(vtkPoints* p, int numPts, const vtkIdType* pts, double centroid[3])
{
  return !!vtkPolygon::ComputeCentroid(p, numPts, pts, centroid, VTK_DEFAULT_PLANARITY_TOLERANCE);
}

//------------------------------------------------------------------------------
// Compute the polygon centroid from a points list and a list of point ids
// that index into the points list. Returns false if the computation is invalid.
bool vtkPolygon::ComputeCentroid(vtkIdTypeArray* ids, vtkPoints* p, double c[3])
{
  return !!vtkPolygon::ComputeCentroid(
    p, ids->GetNumberOfTuples(), ids->GetPointer(0), c, VTK_DEFAULT_PLANARITY_TOLERANCE);
}

//------------------------------------------------------------------------------
double vtkPolygon::DistanceToPolygon(
  double x[3], int numPts, double* pts, double bounds[6], double closest[3])
{
  // First check to see if the point is inside the polygon
  // do a quick bounds check
  if (x[0] >= bounds[0] && x[0] <= bounds[1] && x[1] >= bounds[2] && x[1] <= bounds[3] &&
    x[2] >= bounds[4] && x[2] <= bounds[5])
  {
    double n[3];
    vtkPolygon::ComputeNormal(numPts, pts, n);
    if (vtkPolygon::PointInPolygon(x, numPts, pts, bounds, n))
    {
      closest[0] = x[0];
      closest[1] = x[1];
      closest[2] = x[2];
      return 0.0;
    }
  }

  // Not inside, compute the distance of the point to the edges.
  double minDist2 = VTK_FLOAT_MAX;
  double *p0, *p1, dist2, t, c[3];
  for (int i = 0; i < numPts; i++)
  {
    p0 = pts + 3 * i;
    p1 = pts + 3 * ((i + 1) % numPts);
    dist2 = vtkLine::DistanceToLine(x, p0, p1, t, c);
    if (dist2 < minDist2)
    {
      minDist2 = dist2;
      closest[0] = c[0];
      closest[1] = c[1];
      closest[2] = c[2];
    }
  }

  return sqrt(minDist2);
}

//------------------------------------------------------------------------------
int vtkPolygon::IntersectConvex2DCells(
  vtkCell* cell1, vtkCell* cell2, double tol, double p0[3], double p1[3])
{
  // Intersect the six total edges of the two triangles against each other. Two points are
  // all that are required.
  double *x[2], pcoords[3], t, x0[3], x1[3];
  x[0] = p0;
  x[1] = p1;
  int subId, idx = 0;
  double t2 = tol * tol;

  // Loop over edges of second polygon and intersect against first polygon
  vtkIdType i, numPts = cell2->Points->GetNumberOfPoints();
  for (i = 0; i < numPts; i++)
  {
    cell2->Points->GetPoint(i, x0);
    cell2->Points->GetPoint((i + 1) % numPts, x1);

    if (cell1->IntersectWithLine(x0, x1, tol, t, x[idx], pcoords, subId))
    {
      if (idx == 0)
      {
        idx++;
      }
      else if (((x[1][0] - x[0][0]) * (x[1][0] - x[0][0]) +
                 (x[1][1] - x[0][1]) * (x[1][1] - x[0][1]) +
                 (x[1][2] - x[0][2]) * (x[1][2] - x[0][2])) > t2)
      {
        return 2;
      }
    } // if edge intersection
  }   // over all edges

  // Loop over edges of first polygon and intersect against second polygon
  numPts = cell1->Points->GetNumberOfPoints();
  for (i = 0; i < numPts; i++)
  {
    cell1->Points->GetPoint(i, x0);
    cell1->Points->GetPoint((i + 1) % numPts, x1);

    if (cell2->IntersectWithLine(x0, x1, tol, t, x[idx], pcoords, subId))
    {
      if (idx == 0)
      {
        idx++;
      }
      else if (((x[1][0] - x[0][0]) * (x[1][0] - x[0][0]) +
                 (x[1][1] - x[0][1]) * (x[1][1] - x[0][1]) +
                 (x[1][2] - x[0][2]) * (x[1][2] - x[0][2])) > t2)
      {
        return 2;
      }
    } // if edge intersection
  }   // over all edges

  // Evaluate what we got
  if (idx == 1)
  {
    return 1; // everything intersecting at single point
  }
  else
  {
    return 0;
  }
}
VTK_ABI_NAMESPACE_END
