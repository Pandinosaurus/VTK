// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkQuadratureSchemeDefinition.h"

#include "vtkInformationQuadratureSchemeDefinitionVectorKey.h"
#include "vtkInformationStringKey.h"
#include "vtkObjectFactory.h"
#include "vtkXMLDataElement.h"
#include <sstream>
using std::istringstream;
using std::ostringstream;
#include <string>
using std::string;

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkQuadratureSchemeDefinition);

//------------------------------------------------------------------------------
vtkInformationKeyMacro(vtkQuadratureSchemeDefinition, DICTIONARY, QuadratureSchemeDefinitionVector);

vtkInformationKeyMacro(vtkQuadratureSchemeDefinition, QUADRATURE_OFFSET_ARRAY_NAME, String);

//------------------------------------------------------------------------------
vtkQuadratureSchemeDefinition::vtkQuadratureSchemeDefinition()
{
  this->ShapeFunctionWeights = nullptr;
  this->QuadratureWeights = nullptr;
  this->ShapeFunctionDerivativeWeights = nullptr;
  this->CellType = -1;
  this->QuadratureKey = -1;
  this->NumberOfNodes = 0;
  this->NumberOfQuadraturePoints = 0;
}

//------------------------------------------------------------------------------
vtkQuadratureSchemeDefinition::~vtkQuadratureSchemeDefinition()
{
  this->ReleaseResources();
}

//------------------------------------------------------------------------------
int vtkQuadratureSchemeDefinition::DeepCopy(const vtkQuadratureSchemeDefinition* other)
{
  this->ShapeFunctionWeights = nullptr;
  this->QuadratureWeights = nullptr;
  this->ShapeFunctionDerivativeWeights = nullptr;
  this->CellType = -1;
  this->QuadratureKey = -1;
  this->NumberOfNodes = 0;
  this->NumberOfQuadraturePoints = 0;
  //
  this->CellType = other->CellType;
  this->QuadratureKey = other->QuadratureKey;
  this->NumberOfNodes = other->NumberOfNodes;
  this->NumberOfQuadraturePoints = other->NumberOfQuadraturePoints;
  //
  this->SecureResources();
  //
  this->SetShapeFunctionWeights(other->GetShapeFunctionWeights());
  this->SetQuadratureWeights(other->GetQuadratureWeights());
  //
  return 1;
}

//------------------------------------------------------------------------------
void vtkQuadratureSchemeDefinition::Initialize(
  int cellType, int numberOfNodes, int numberOfQuadraturePoints, double* shapeFunctionWeights)
{
  this->ReleaseResources();
  //
  this->CellType = cellType;
  this->QuadratureKey = -1;
  this->NumberOfNodes = numberOfNodes;
  this->NumberOfQuadraturePoints = numberOfQuadraturePoints;
  this->Dimension = 0;
  //
  this->SecureResources();
  //
  this->SetShapeFunctionWeights(shapeFunctionWeights);
}

//------------------------------------------------------------------------------
void vtkQuadratureSchemeDefinition::Initialize(int cellType, int numberOfNodes,
  int numberOfQuadraturePoints, double* shapeFunctionWeights, double* quadratureWeights)
{
  this->ReleaseResources();
  //
  this->CellType = cellType;
  this->QuadratureKey = -1;
  this->NumberOfNodes = numberOfNodes;
  this->NumberOfQuadraturePoints = numberOfQuadraturePoints;
  this->Dimension = 0;
  //
  this->SecureResources();
  //
  this->SetShapeFunctionWeights(shapeFunctionWeights);
  this->SetQuadratureWeights(quadratureWeights);
}

//------------------------------------------------------------------------------
void vtkQuadratureSchemeDefinition::Initialize(int cellType, int numberOfNodes,
  int numberOfQuadraturePoints, const double* shapeFunctionWeights, const double* quadratureWeights,
  int dim, const double* shapeFunctionDerivativeWeights)
{
  this->ReleaseResources();
  //
  this->CellType = cellType;
  this->QuadratureKey = -1;
  this->NumberOfNodes = numberOfNodes;
  this->NumberOfQuadraturePoints = numberOfQuadraturePoints;
  this->Dimension = dim;
  //
  this->SecureResources();
  //
  this->SetShapeFunctionWeights(shapeFunctionWeights);
  this->SetQuadratureWeights(quadratureWeights);
  this->SetShapeFunctionDerivativeWeights(shapeFunctionDerivativeWeights);
}

//------------------------------------------------------------------------------
void vtkQuadratureSchemeDefinition::ReleaseResources()
{
  delete[] this->ShapeFunctionWeights;
  this->ShapeFunctionWeights = nullptr;

  delete[] this->QuadratureWeights;
  this->QuadratureWeights = nullptr;

  delete[] this->ShapeFunctionDerivativeWeights;
  this->ShapeFunctionDerivativeWeights = nullptr;
}

//------------------------------------------------------------------------------
int vtkQuadratureSchemeDefinition::SecureResources()
{
  if ((this->NumberOfQuadraturePoints <= 0) || (this->NumberOfNodes <= 0))
  {
    vtkWarningMacro("Failed to allocate. Invalid buffer size.");
    return 0;
  }

  // Delete weights if they have been allocated
  this->ReleaseResources();

  // Shape function weights, one vector for each quad point.
  this->ShapeFunctionWeights = new double[this->NumberOfQuadraturePoints * this->NumberOfNodes];
  for (int i = 0; i < this->NumberOfQuadraturePoints * this->NumberOfNodes; i++)
  {
    this->ShapeFunctionWeights[i] = 0.0;
  }

  // Quadrature weights, one double for each quad point
  this->QuadratureWeights = new double[this->NumberOfQuadraturePoints];
  for (int i = 0; i < this->NumberOfQuadraturePoints; i++)
  {
    this->QuadratureWeights[i] = 0.0;
  }

  // Shape function derivative weights, one matrix for each quad point.
  this->ShapeFunctionDerivativeWeights =
    new double[this->NumberOfQuadraturePoints * this->NumberOfNodes * this->Dimension];
  for (int i = 0; i < this->NumberOfQuadraturePoints * this->NumberOfNodes * this->Dimension; i++)
  {
    this->ShapeFunctionDerivativeWeights[i] = 0.0;
  }
  return 1;
}

//------------------------------------------------------------------------------
void vtkQuadratureSchemeDefinition::SetShapeFunctionWeights(const double* weights)
{
  if ((this->NumberOfQuadraturePoints <= 0) || (this->NumberOfNodes <= 0) ||
    (this->ShapeFunctionWeights == nullptr) || !weights)
  {
    return;
  }
  // Copy
  int n = this->NumberOfQuadraturePoints * this->NumberOfNodes;
  for (int i = 0; i < n; ++i)
  {
    this->ShapeFunctionWeights[i] = weights[i];
  }
}

//------------------------------------------------------------------------------
void vtkQuadratureSchemeDefinition::SetQuadratureWeights(const double* weights)
{
  if ((this->NumberOfQuadraturePoints <= 0) || (this->NumberOfNodes <= 0) ||
    (this->QuadratureWeights == nullptr) || !weights)
  {
    return;
  }
  // Copy
  for (int i = 0; i < this->NumberOfQuadraturePoints; ++i)
  {
    this->QuadratureWeights[i] = weights[i];
  }
}

//------------------------------------------------------------------------------
void vtkQuadratureSchemeDefinition::SetShapeFunctionDerivativeWeights(const double* weights)
{
  if ((this->NumberOfQuadraturePoints <= 0) || (this->NumberOfNodes <= 0) ||
    (this->ShapeFunctionDerivativeWeights == nullptr) || !weights)
  {
    return;
  }
  // Copy
  for (int i = 0; i < this->NumberOfNodes * this->NumberOfQuadraturePoints * this->Dimension; ++i)
  {
    this->ShapeFunctionDerivativeWeights[i] = weights[i];
  }
}

//------------------------------------------------------------------------------
void vtkQuadratureSchemeDefinition::PrintSelf(ostream& sout, vtkIndent indent)
{

  vtkObject::PrintSelf(sout, indent);

  double* pSfWt = this->ShapeFunctionWeights;

  for (int ptId = 0; ptId < this->NumberOfQuadraturePoints; ++ptId)
  {
    sout << indent << "(" << pSfWt[0];
    ++pSfWt;
    for (int nodeId = 1; nodeId < this->NumberOfNodes; ++nodeId)
    {
      sout << indent << ", " << pSfWt[0];
      ++pSfWt;
    }
    sout << ")" << endl;
  }
}

// NOTE: These are used by XML readers/writers.
//------------------------------------------------------------------------------
ostream& operator<<(ostream& sout, const vtkQuadratureSchemeDefinition& def)
{
  /*
  stream will have this space delimited format:
  [cell type][number of cell nodes][number quadrature points][Qp1 ... QpN][Qwt1...QwtN]
  */

  // Size of arrays
  int nQuadPts = def.GetNumberOfQuadraturePoints();
  int nNodes = def.GetNumberOfNodes();

  // Write header
  sout << def.GetCellType() << " " << nNodes << " " << nQuadPts;

  if ((nNodes > 0) && (nQuadPts > 0))
  {
    sout.setf(ios::floatfield, ios::scientific);
    sout.precision(16);

    const double* pWt;
    // Write shape function weights
    pWt = def.GetShapeFunctionWeights();
    for (int ptId = 0; ptId < nQuadPts; ++ptId)
    {
      for (int nodeId = 0; nodeId < nNodes; ++nodeId)
      {
        sout << " " << pWt[0];
        ++pWt;
      }
    }
    // Write quadrature weights
    pWt = def.GetQuadratureWeights();
    for (int nodeId = 0; nodeId < nNodes; ++nodeId)
    {
      sout << " " << pWt[0];
      ++pWt;
    }
  }
  else
  {
    vtkGenericWarningMacro("Empty definition written to stream.");
  }
  return sout;
}

//------------------------------------------------------------------------------
istream& operator>>(istream& sin, vtkQuadratureSchemeDefinition& def)
{
  /*
  stream will have this space delimited format:
  [cell type][number of cell nodes][number quadrature points][Qp1 ... QpN][Qwt1...QwtN]
  */

  // read the header
  int cellType, nNodes, nQuadPts;
  sin >> cellType >> nNodes >> nQuadPts;

  double *SfWt = nullptr, *QWt = nullptr, *pWt = nullptr;
  if ((nNodes > 0) && (nQuadPts > 0))
  {
    // read shape function weights
    SfWt = new double[nQuadPts * nNodes];
    pWt = SfWt;
    for (int ptId = 0; ptId < nQuadPts; ++ptId)
    {
      for (int nodeId = 0; nodeId < nNodes; ++nodeId)
      {
        sin >> pWt[0];
        ++pWt;
      }
    }
    // Write quadrature weights
    QWt = new double[nQuadPts];
    pWt = QWt;
    for (int nodeId = 0; nodeId < nNodes; ++nodeId)
    {
      sin >> pWt[0];
      ++pWt;
    }
  }
  else
  {
    vtkGenericWarningMacro("Empty definition found in stream.");
  }

  // initialize the object
  def.Initialize(cellType, nNodes, nQuadPts, SfWt, QWt);

  // clean up
  delete[] SfWt;
  delete[] QWt;

  return sin;
}

//------------------------------------------------------------------------------
int vtkQuadratureSchemeDefinition::SaveState(vtkXMLDataElement* root)
{
  // Quick sanity check, we're not nesting rather treating
  // this as a root, to be nested by the caller as needed.
  if (root->GetName() != nullptr || root->GetNumberOfNestedElements() > 0)
  {
    vtkWarningMacro("Can't save state to non-empty element.");
    return 0;
  }

  root->SetName("vtkQuadratureSchemeDefinition");

  vtkXMLDataElement* e;
  e = vtkXMLDataElement::New();
  e->SetName("CellType");
  e->SetIntAttribute("value", this->CellType);
  root->AddNestedElement(e);
  e->Delete();

  e = vtkXMLDataElement::New();
  e->SetName("NumberOfNodes");
  e->SetIntAttribute("value", this->NumberOfNodes);
  root->AddNestedElement(e);
  e->Delete();

  e = vtkXMLDataElement::New();
  e->SetName("NumberOfQuadraturePoints");
  e->SetIntAttribute("value", this->NumberOfQuadraturePoints);
  root->AddNestedElement(e);
  e->Delete();

  vtkXMLDataElement* eShapeWts = vtkXMLDataElement::New();
  eShapeWts->SetName("ShapeFunctionWeights");
  eShapeWts->SetCharacterDataWidth(4);
  root->AddNestedElement(eShapeWts);
  eShapeWts->Delete();

  vtkXMLDataElement* eQuadWts = vtkXMLDataElement::New();
  eQuadWts->SetName("QuadratureWeights");
  eQuadWts->SetCharacterDataWidth(4);
  root->AddNestedElement(eQuadWts);
  eQuadWts->Delete();

  if ((this->NumberOfNodes > 0) && (this->NumberOfQuadraturePoints > 0))
  {
    // Write shape function weights
    ostringstream ssShapeWts;
    ssShapeWts.setf(ios::floatfield, ios::scientific);
    ssShapeWts.precision(16);
    ssShapeWts << this->ShapeFunctionWeights[0];
    int nIds = this->NumberOfNodes * this->NumberOfQuadraturePoints;
    for (int id = 1; id < nIds; ++id)
    {
      ssShapeWts << " " << this->ShapeFunctionWeights[id];
    }
    string sShapeWts = ssShapeWts.str();
    eShapeWts->SetCharacterData(sShapeWts.c_str(), static_cast<int>(sShapeWts.size()));

    // Write quadrature weights
    ostringstream ssQuadWts;
    ssQuadWts.setf(ios::floatfield, ios::scientific);
    ssQuadWts.precision(16);
    ssQuadWts << this->QuadratureWeights[0];
    for (int id = 1; id < this->NumberOfQuadraturePoints; ++id)
    {
      ssQuadWts << " " << this->QuadratureWeights[id];
    }
    string sQuadWts = ssQuadWts.str();
    eQuadWts->SetCharacterData(sQuadWts.c_str(), static_cast<int>(sQuadWts.size()));
  }
  else
  {
    vtkGenericWarningMacro("Empty definition written to stream.");
    return 0;
  }

  return 1;
}

//------------------------------------------------------------------------------
int vtkQuadratureSchemeDefinition::RestoreState(vtkXMLDataElement* root)
{
  // A quick sanity check to be sure we have the correct tag.
  if (strcmp(root->GetName(), "vtkQuadratureSchemeDefinition") != 0)
  {
    vtkWarningMacro("Attempting to restore the state in "
      << root->GetName() << " into vtkQuadratureSchemeDefinition.");
    return 0;
  }

  vtkXMLDataElement* e;
  const char* value;
  // Transfer state from XML hierarchy.
  e = root->FindNestedElementWithName("CellType");
  if (e == nullptr)
  {
    vtkWarningMacro("Expected nested element \"CellType\" "
                    "is not present.");
    return 0;
  }
  value = e->GetAttribute("value");
  this->CellType = atoi(value);
  //
  e = root->FindNestedElementWithName("NumberOfNodes");
  if (e == nullptr)
  {
    vtkWarningMacro("Expected nested element \"NumberOfNodes\" "
                    "is not present.");
    return 0;
  }
  value = e->GetAttribute("value");
  this->NumberOfNodes = atoi(value);
  //
  e = root->FindNestedElementWithName("NumberOfQuadraturePoints");
  if (e == nullptr)
  {
    vtkWarningMacro("Expected nested element \"NumberOfQuadraturePoints\" "
                    "is not present.");
    return 0;
  }
  value = e->GetAttribute("value");
  this->NumberOfQuadraturePoints = atoi(value);
  // Extract the weights.
  if (this->SecureResources())
  {
    istringstream issWts;
    //
    e = root->FindNestedElementWithName("ShapeFunctionWeights");
    if (e == nullptr)
    {
      vtkWarningMacro("Expected nested element \"ShapeFunctionWeights\" "
                      "is not present.");
      return 0;
    }
    value = e->GetCharacterData();
    if (value == nullptr)
    {
      vtkWarningMacro("Character data in nested element"
                      " \"ShapeFunctionWeights\" is not present.");
      return 0;
    }
    issWts.str(value);
    int nWts = this->NumberOfNodes * this->NumberOfQuadraturePoints;
    for (int id = 0; id < nWts; ++id)
    {
      if (!issWts.good())
      {
        vtkWarningMacro("Character data for \"ShapeFunctionWeights\" "
                        "is short.");
        return 0;
      }
      issWts >> this->ShapeFunctionWeights[id];
    }
    //
    e = root->FindNestedElementWithName("QuadratureWeights");
    if (e == nullptr)
    {
      vtkWarningMacro("Expected element \"QuadratureWeights\" "
                      "is not present.");
      return 0;
    }
    value = e->GetCharacterData();
    if (value == nullptr)
    {
      vtkWarningMacro("Character data in expected nested element"
                      " \"QuadratureWeights\" is not present.");
      return 0;
    }
    issWts.str(value);
    for (int id = 0; id < this->NumberOfQuadraturePoints; ++id)
    {
      if (!issWts.good())
      {
        vtkWarningMacro("Character data for \"QuadratureWeights\" "
                        "is short.");
        return 0;
      }
      issWts >> this->QuadratureWeights[id];
    }
  }

  return 1;
}
VTK_ABI_NAMESPACE_END
