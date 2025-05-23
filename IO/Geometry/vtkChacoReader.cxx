// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-FileCopyrightText: Copyright (c) Sandia Corporation
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkChacoReader.h"

#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkDoubleArray.h"
#include "vtkInformation.h"
#include "vtkInformationDoubleVectorKey.h"
#include "vtkInformationVector.h"
#include "vtkIntArray.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkUnstructuredGrid.h"
#include <cctype>
#include <cstdio>
#include <vtksys/SystemTools.hxx>

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkChacoReader);

//------------------------------------------------------------------------------
// Description:
// Instantiate object with nullptr filename.
vtkChacoReader::vtkChacoReader()
{
  this->BaseName = nullptr;
  this->GenerateGlobalElementIdArray = 1;
  this->GenerateGlobalNodeIdArray = 1;
  this->GenerateVertexWeightArrays = 0;
  this->GenerateEdgeWeightArrays = 0;
  this->EarrayName = nullptr;
  this->VarrayName = nullptr;
  this->Dimensionality = -1;
  this->NumberOfVertices = 0;
  this->NumberOfEdges = 0;
  this->NumberOfVertexWeights = 0;
  this->NumberOfEdgeWeights = 0;
  this->GraphFileHasVertexNumbers = 0;

  this->NumberOfPointWeightArrays = 0;
  this->NumberOfCellWeightArrays = 0;

  this->CurrentGeometryFP = nullptr;
  this->CurrentGraphFP = nullptr;
  this->CurrentBaseName = nullptr;

  this->DataCache = vtkUnstructuredGrid::New();
  this->RemakeDataCacheFlag = 1;

  this->Offset = 0;
  this->Break_pnt = 200;
  this->Save_pnt = 0;

  this->SetNumberOfInputPorts(0);
}

//------------------------------------------------------------------------------
vtkChacoReader::~vtkChacoReader()
{
  this->SetBaseName(nullptr);
  this->SetCurrentBaseName(nullptr);

  this->ClearWeightArrayNames();

  this->DataCache->Delete();
  this->DataCache = nullptr;
}

//------------------------------------------------------------------------------
void vtkChacoReader::ClearWeightArrayNames()
{
  int i = 0;
  if (this->VarrayName)
  {
    for (i = 0; i < this->NumberOfVertexWeights; i++)
    {
      delete[] this->VarrayName[i];
    }
    delete[] this->VarrayName;
    this->VarrayName = nullptr;
  }

  if (this->EarrayName)
  {
    for (i = 0; i < this->NumberOfEdgeWeights; i++)
    {
      delete[] this->EarrayName[i];
    }
    delete[] this->EarrayName;
    this->EarrayName = nullptr;
  }
}

//------------------------------------------------------------------------------
void vtkChacoReader::MakeWeightArrayNames(int nv, int ne)
{
  int i = 0;
  if (nv > 0)
  {
    this->VarrayName = new char*[nv];
    for (i = 0; i < nv; i++)
    {
      this->VarrayName[i] = new char[64];
      snprintf(this->VarrayName[i], 64, "VertexWeight%d", i + 1);
    }
  }
  if (ne > 0)
  {
    this->EarrayName = new char*[ne];
    for (i = 0; i < ne; i++)
    {
      this->EarrayName[i] = new char[64];
      snprintf(this->EarrayName[i], 64, "EdgeWeight%d", i + 1);
    }
  }
}

//------------------------------------------------------------------------------
const char* vtkChacoReader::GetVertexWeightArrayName(int weight)
{
  if (this->GetGenerateVertexWeightArrays() && (weight > 0) &&
    (weight <= this->NumberOfVertexWeights))
  {
    return this->VarrayName[weight - 1];
  }

  return nullptr;
}

//------------------------------------------------------------------------------
const char* vtkChacoReader::GetEdgeWeightArrayName(int weight)
{
  if (this->GetGenerateEdgeWeightArrays() && (weight > 0) && (weight <= this->NumberOfEdgeWeights))
  {
    return this->EarrayName[weight - 1];
  }

  return nullptr;
}

//------------------------------------------------------------------------------
int vtkChacoReader::RequestInformation(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** vtkNotUsed(inputVector), vtkInformationVector* vtkNotUsed(outputVector))
{
  double x, y, z;

  if (!this->BaseName)
  {
    vtkErrorMacro(<< "No BaseName specified");
    return 0;
  }

  int newFile =
    ((this->CurrentBaseName == nullptr) || strcmp(this->CurrentBaseName, this->BaseName) != 0);

  if (!newFile)
  {
    return 1;
  }

  if (this->OpenCurrentFile() != 1)
  {
    return 0;
  }

  // Get the dimension of the coordinates from the vertex file

  int rc = this->InputGeom(1, 0, &x, &y, &z);

  this->ResetInputBuffers();

  if (rc)
  {
    // Get the number of vertices and edges, and number of
    // vertex weights and edge weights from the graph file.

    rc = this->InputGraph1();

    this->ResetInputBuffers();

    if (rc)
    {
      this->MakeWeightArrayNames(this->NumberOfVertexWeights, this->NumberOfEdgeWeights);
    }
  }

  // Close the file
  this->CloseCurrentFile();

  this->RemakeDataCacheFlag = 1;

  return rc;
}

//------------------------------------------------------------------------------
int vtkChacoReader::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** vtkNotUsed(inputVector), vtkInformationVector* outputVector)
{
  if (!this->BaseName)
  {
    vtkErrorMacro(<< "No BaseName specified");
    return 0;
  }

  vtkInformation* outInfo = outputVector->GetInformationObject(0);

  vtkUnstructuredGrid* output =
    vtkUnstructuredGrid::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));

  int retVal = this->BuildOutputGrid(output);

  return retVal;
}

//------------------------------------------------------------------------------
int vtkChacoReader::BuildOutputGrid(vtkUnstructuredGrid* output)
{
  int i = 0;
  if (this->OpenCurrentFile() != 1)
  {
    vtkWarningMacro(<< "Can't open file");
    return 0;
  }

  int ncells = this->DataCache->GetNumberOfCells();
  int haveVertexWeightArrays = 0;
  int haveEdgeWeightArrays = 0;

  if (ncells && (this->NumberOfVertexWeights > 0))
  {
    vtkDoubleArray* da = vtkArrayDownCast<vtkDoubleArray>(
      this->DataCache->GetPointData()->GetArray(this->VarrayName[0]));

    haveVertexWeightArrays = (da != nullptr);
  }

  if (ncells && (this->NumberOfEdgeWeights > 0))
  {
    vtkDoubleArray* da = vtkArrayDownCast<vtkDoubleArray>(
      this->DataCache->GetCellData()->GetArray(this->EarrayName[0]));

    haveEdgeWeightArrays = (da != nullptr);
  }

  if (!this->RemakeDataCacheFlag &&
    ((!haveVertexWeightArrays && this->GenerateVertexWeightArrays) ||
      (!haveEdgeWeightArrays && this->GenerateEdgeWeightArrays)))
  {
    this->RemakeDataCacheFlag = 1;
  }

  if (this->RemakeDataCacheFlag)
  {
    output->Initialize();
    int rc = this->ReadFile(output);

    if (rc == 0)
    {
      this->CloseCurrentFile();
      return 0;
    }

    if (this->GenerateGlobalElementIdArray)
    {
      this->AddElementIds(output);
    }

    if (this->GenerateGlobalNodeIdArray)
    {
      this->AddNodeIds(output);
    }

    // Save the output.  Next time we execute, it may be simply
    // because they turned off vertex or edge weights, or decided they
    // do or do not want element or point IDs.  For these we can just
    // modify the DataCache, rather than reading in the whole file
    // and creating a vtkUnstructuredGrid from it.

    this->DataCache->Initialize();
    this->DataCache->ShallowCopy(output);

    this->RemakeDataCacheFlag = 0;
  }
  else
  {
    // Just copy the output we calculated last time, after checking
    // to see if any parameters have changed

    if (haveVertexWeightArrays && !this->GenerateVertexWeightArrays)
    {
      for (i = 0; i < this->NumberOfVertexWeights; i++)
      {
        this->DataCache->GetPointData()->RemoveArray(this->VarrayName[i]);
      }

      this->NumberOfPointWeightArrays = 0;
    }

    if (haveEdgeWeightArrays && !this->GenerateEdgeWeightArrays)
    {
      for (i = 0; i < this->NumberOfEdgeWeights; i++)
      {
        this->DataCache->GetCellData()->RemoveArray(this->EarrayName[i]);
      }

      this->NumberOfCellWeightArrays = 0;
    }

    vtkIntArray* ia = vtkArrayDownCast<vtkIntArray>(
      this->DataCache->GetCellData()->GetArray(vtkChacoReader::GetGlobalElementIdArrayName()));

    if (!ia && this->GenerateGlobalElementIdArray)
    {
      this->AddElementIds(this->DataCache);
    }
    else if (ia && !this->GenerateGlobalElementIdArray)
    {
      this->DataCache->GetCellData()->RemoveArray(vtkChacoReader::GetGlobalElementIdArrayName());
    }

    ia = vtkArrayDownCast<vtkIntArray>(
      this->DataCache->GetPointData()->GetArray(vtkChacoReader::GetGlobalNodeIdArrayName()));

    if (!ia && this->GenerateGlobalNodeIdArray)
    {
      this->AddNodeIds(this->DataCache);
    }
    else if (ia && !this->GenerateGlobalNodeIdArray)
    {
      this->DataCache->GetPointData()->RemoveArray(vtkChacoReader::GetGlobalNodeIdArrayName());
    }

    output->ShallowCopy(this->DataCache);
  }

  // This just makes sure the arrays are the same size as the number
  // of nodes or cell
  output->CheckAttributes();

  // We may have some mem that can be condensed
  output->Squeeze();

  this->CloseCurrentFile();

  return 1;
}

//------------------------------------------------------------------------------
int vtkChacoReader::ReadFile(vtkUnstructuredGrid* output)
{
  int i = 0;
  vtkIdType id = 0;

  // Reset the entire unstructured grid
  output->Reset();

  this->NumberOfPointWeightArrays = 0;
  this->NumberOfCellWeightArrays = 0;

  // Read in the points.  Maintain the order in the original file.
  // The order indicates the global node ID.

  vtkPoints* ptarray = vtkPoints::New();
  ptarray->SetNumberOfPoints(this->NumberOfVertices);
  ptarray->SetDataTypeToDouble();

  int memoryOK = 1;

  double* x = new double[this->NumberOfVertices];
  double* y = nullptr;
  double* z = nullptr;

  if (!x)
  {
    memoryOK = 0;
  }
  else if (this->Dimensionality > 1)
  {
    y = new double[this->NumberOfVertices];
    if (!y)
    {
      memoryOK = 0;
    }
    else if (this->Dimensionality > 2)
    {
      z = new double[this->NumberOfVertices];
      if (!z)
      {
        memoryOK = 0;
      }
    }
  }

  if (!memoryOK)
  {
    vtkErrorMacro(<< "ReadFile memory allocation failure");
    delete[] x;
    delete[] y;
    return 0;
  }

  int rc = this->InputGeom(this->NumberOfVertices, this->Dimensionality, x, y, z);

  this->ResetInputBuffers();

  if (rc == 0)
  {
    delete[] x;
    delete[] y;
    delete[] z;
    return 0;
  }

  if (this->Dimensionality == 3)
  {
    for (id = 0; id < this->NumberOfVertices; id++)
    {
      ptarray->InsertNextPoint(x[id], y[id], z[id]);
    }
  }
  else if (this->Dimensionality == 2)
  {
    for (id = 0; id < this->NumberOfVertices; id++)
    {
      ptarray->InsertNextPoint(x[id], y[id], 0.0);
    }
  }
  else if (this->Dimensionality == 1)
  {
    for (id = 0; id < this->NumberOfVertices; id++)
    {
      ptarray->InsertNextPoint(x[id], 0.0, 0.0);
    }
  }

  output->SetPoints(ptarray);

  delete[] x;
  delete[] y;
  delete[] z;
  ptarray->Delete();

  // Read in cell topology and possibly cell and point weights.
  // (The unstructured grid "cells" are the Chaco "edges".)
  //
  // Note: The order in which point and cell arrays appear in the
  // output must be fixed.  This is because this reader is called
  // by vtkPChacoReader, and all processes must create output
  // ugrids with the cell arrays and point arrays in the same
  // order.  The order we choose for point arrays is:
  //   vertex weight arrays, if any, in order they appear in file
  //   global point IDs, if any
  //
  // The order for cell arrays is:
  //   edge weight arrays, if any, in order they appear in file
  //   global element IDs, if any

  int retVal = 1;

  vtkIdType* idx = nullptr;
  vtkIdType* nbors = nullptr;
  double* vweights = nullptr;
  double* eweights = nullptr;

  double** vw = nullptr;
  double** ew = nullptr;

  if (this->GetGenerateVertexWeightArrays() && (this->NumberOfVertexWeights > 0))
  {
    vw = &vweights;
  }

  if (this->GetGenerateEdgeWeightArrays() && (this->NumberOfEdgeWeights > 0))
  {
    ew = &eweights;
  }

  rc = this->InputGraph2(&idx, &nbors, vw, ew);

  this->ResetInputBuffers();

  if (rc == 0)
  {
    return 0;
  }

  vtkDoubleArray** varrays = nullptr;
  vtkDoubleArray** earrays = nullptr;
  double* vwgt = nullptr;
  double* ewgt = nullptr;

  if (vw)
  {
    varrays = new vtkDoubleArray*[this->NumberOfVertexWeights];
    for (i = 0; i < this->NumberOfVertexWeights; i++)
    {
      varrays[i] = vtkDoubleArray::New();
      varrays[i]->SetNumberOfValues(this->NumberOfVertices);
      varrays[i]->SetName(this->VarrayName[i]);
    }
    vwgt = vweights;
  }

  if (ew)
  {
    earrays = new vtkDoubleArray*[this->NumberOfEdgeWeights];
    for (i = 0; i < this->NumberOfEdgeWeights; i++)
    {
      earrays[i] = vtkDoubleArray::New();
      earrays[i]->SetNumberOfValues(this->NumberOfEdges);
      earrays[i]->SetName(this->EarrayName[i]);
    }
    ewgt = eweights;
  }

  vtkIdTypeArray* ca = vtkIdTypeArray::New();

  if (idx == nullptr)
  {
    // Special case: there are no edges in this graph.  Every
    // vertex will be a cell.

    ca->SetNumberOfValues(2 * this->NumberOfVertices);
    vtkIdType* captr = ca->GetPointer(0);

    for (id = 0; id < this->NumberOfVertices; id++)
    {
      *captr++ = 1;  // number of vertices in cell
      *captr++ = id; // internal ID of vertex

      if (vw)
      {
        for (int w = 0; w < this->NumberOfVertexWeights; w++)
        {
          varrays[w]->SetValue(id, *vwgt++);
        }
      }
    }
    vtkCellArray* cells = vtkCellArray::New();
    cells->AllocateExact(this->NumberOfVertices, ca->GetNumberOfValues() - this->NumberOfVertices);
    cells->ImportLegacyFormat(ca);
    output->SetCells(VTK_VERTEX, cells);
    cells->Delete();
  }
  else
  {
    // The usual case: most or all vertices are connected to
    // other vertices.

    ca->SetNumberOfValues(3 * this->NumberOfEdges);
    vtkIdType* captr = ca->GetPointer(0);

    vtkIdType edgeNum = -1;

    for (id = 0; id < this->NumberOfVertices; id++)
    {
      // Each edge in the Chaco file is listed twice, for each
      // vertex.  We only save the edge once.

      for (int n = idx[id]; n < idx[id + 1]; n++)
      {
        vtkIdType nbor = nbors[n] - 1; // internal id

        // Save each edge connected to this vertex, if it hasn't
        // been saved already.

        if (nbor > id)
        {
          edgeNum++;

          if (edgeNum == this->NumberOfEdges)
          {
            vtkErrorMacro(<< "Too many edges in Chaco file");
            retVal = 0;
            break;
          }

          *captr++ = 2;    // size of cell
          *captr++ = id;   // first vertex
          *captr++ = nbor; // second vertex

          if (ew)
          {
            // Save the edge weights associated with this edge

            for (i = 0; i < this->NumberOfEdgeWeights; i++)
            {
              earrays[i]->SetValue(edgeNum, *ewgt++);
            }
          }
        }
        else if (ew)
        {
          ewgt += this->NumberOfEdgeWeights; // Skip duplicate edge weights
        }
      }

      if (!retVal)
        break;

      // Save the weights associated with this vertex

      if (vw)
      {
        for (i = 0; i < this->NumberOfVertexWeights; i++)
        {
          varrays[i]->SetValue(id, *vwgt++);
        }
      }
    }

    if (edgeNum != this->NumberOfEdges - 1)
    {
      vtkErrorMacro(<< "Too few edges in Chaco file");
      retVal = 0;
    }

    delete[] idx;
    delete[] nbors;

    if (retVal)
    {
      vtkCellArray* cells = vtkCellArray::New();
      cells->AllocateExact(this->NumberOfEdges, ca->GetNumberOfValues() - this->NumberOfEdges);
      cells->ImportLegacyFormat(ca);
      output->SetCells(VTK_LINE, cells);
      cells->Delete();
    }
    else
    {
      output->Initialize();
    }
  }

  ca->Delete();

  if (retVal == 1)
  {
    this->NumberOfPointWeightArrays = this->NumberOfVertexWeights;
    this->NumberOfCellWeightArrays = this->NumberOfEdgeWeights;
  }

  if (vw)
  {
    delete[] vweights;
    for (i = 0; i < this->NumberOfVertexWeights; i++)
    {
      if (retVal)
      {
        output->GetPointData()->AddArray(varrays[i]);
      }
      varrays[i]->Delete();
    }

    delete[] varrays;
  }

  if (ew)
  {
    delete[] eweights;
    for (i = 0; i < this->NumberOfEdgeWeights; i++)
    {
      if (retVal)
      {
        output->GetCellData()->AddArray(earrays[i]);
      }
      earrays[i]->Delete();
    }

    delete[] earrays;
  }

  if (retVal)
  {
    output->Squeeze();
  }

  return retVal;
}
//------------------------------------------------------------------------------
void vtkChacoReader::AddElementIds(vtkUnstructuredGrid* output)
{
  // We arbitrarily assign the element ids, since Chaco files do
  // not have the notion of Element IDs.

  vtkIdType len = output->GetNumberOfCells();

  vtkIntArray* ia = vtkIntArray::New();
  ia->SetName(vtkChacoReader::GetGlobalElementIdArrayName());
  ia->SetNumberOfValues(len);

  for (vtkIdType i = 0; i < len; i++)
  {
    ia->SetValue(i, i + 1);
  }

  output->GetCellData()->AddArray(ia);
  ia->Delete();
}

//------------------------------------------------------------------------------
void vtkChacoReader::AddNodeIds(vtkUnstructuredGrid* output)
{
  // The vertex IDs in a Chaco file begin at 1 for the first
  // vertex in the .coords file, and increase by 1 thereafter.

  vtkIdType len = output->GetNumberOfPoints();

  vtkIntArray* ia = vtkIntArray::New();
  ia->SetName(vtkChacoReader::GetGlobalNodeIdArrayName());
  ia->SetNumberOfValues(len);

  for (vtkIdType i = 0; i < len; i++)
  {
    ia->SetValue(i, i + 1);
  }

  output->GetPointData()->AddArray(ia);
  ia->Delete();
}

//------------------------------------------------------------------------------
void vtkChacoReader::PrintSelf(ostream& os, vtkIndent indent)
{
  int i = 0;
  this->Superclass::PrintSelf(os, indent);

  if (this->GenerateGlobalElementIdArray)
  {
    os << indent << "GenerateGlobalElementIdArray: On\n";
  }
  else
  {
    os << indent << "GenerateGlobalElementIdArray: Off\n";
  }

  if (this->GenerateGlobalNodeIdArray)
  {
    os << indent << "GenerateGlobalNodeIdArray: On\n";
  }
  else
  {
    os << indent << "GenerateGlobalNodeIdArray: Off\n";
  }

  if (this->GenerateVertexWeightArrays)
  {
    os << indent << "GenerateVertexWeightArrays: On\n";
  }
  else
  {
    os << indent << "GenerateVertexWeightArrays: Off\n";
  }

  if (this->GenerateEdgeWeightArrays)
  {
    os << indent << "GenerateEdgeWeightArrays: On\n";
  }
  else
  {
    os << indent << "GenerateEdgeWeightArrays: Off\n";
  }

  os << indent << "Base Name: " << (this->BaseName ? this->BaseName : "(none)") << "\n";
  os << indent << "Dimensionality: " << this->Dimensionality << "\n";
  os << indent << "NumberOfVertices: " << this->NumberOfVertices << "\n";
  os << indent << "NumberOfEdges: " << this->NumberOfEdges << "\n";
  os << indent << "NumberOfVertexWeights: " << this->NumberOfVertexWeights << "\n";
  os << indent << "NumberOfEdgeWeights: " << this->NumberOfEdgeWeights << "\n";
  os << indent << "NumberOfPointWeightArrays: " << this->NumberOfPointWeightArrays << "\n";
  os << indent << "NumberOfCellWeightArrays: " << this->NumberOfCellWeightArrays << "\n";

  for (i = 1; i <= this->NumberOfPointWeightArrays; i++)
  {
    cout << "vertex weight array name: " << this->GetVertexWeightArrayName(i) << endl;
  }

  for (i = 1; i <= this->NumberOfCellWeightArrays; i++)
  {
    cout << "edge weight array name: " << this->GetEdgeWeightArrayName(i) << endl;
  }
}

//------------------------------------------------------------------------------
void vtkChacoReader::CloseCurrentFile()
{
  if (this->CurrentGeometryFP)
  {
    fclose(this->CurrentGeometryFP);
    fclose(this->CurrentGraphFP);
    this->CurrentGeometryFP = nullptr;
    this->CurrentGraphFP = nullptr;
  }
}

//------------------------------------------------------------------------------
int vtkChacoReader::OpenCurrentFile()
{
  int result = 0;

  if (this->CurrentGeometryFP == nullptr)
  {
    int len = static_cast<int>(strlen(this->BaseName));
    char* buf = new char[len + 64];
    snprintf(buf, len + 64, "%s.coords", this->BaseName);

    this->CurrentGeometryFP = vtksys::SystemTools::Fopen(buf, "r");

    if (this->CurrentGeometryFP == nullptr)
    {
      vtkErrorMacro(<< "Problem opening " << buf);
      this->SetCurrentBaseName(nullptr);
    }
    else
    {
      snprintf(buf, len + 64, "%s.graph", this->BaseName);

      this->CurrentGraphFP = vtksys::SystemTools::Fopen(buf, "r");

      if (this->CurrentGraphFP == nullptr)
      {
        vtkErrorMacro(<< "Problem opening " << buf);
        this->SetCurrentBaseName(nullptr);
        fclose(this->CurrentGeometryFP);
        this->CurrentGeometryFP = nullptr;
      }
      else
      {
        this->SetCurrentBaseName(this->GetBaseName());
        result = 1;
      }
    }
    delete[] buf;
  }

  return result;
}

//------------------------------------------------------------------------------
// Code to read Chaco files.
// This software was developed by Bruce Hendrickson and Robert Leland
// at Sandia National Laboratories under US Department of Energy
// contract DE-AC04-76DP00789.

void vtkChacoReader::ResetInputBuffers()
{
  this->Offset = 0;
  this->Break_pnt = 200;
  this->Save_pnt = 0;
}

//------------------------------------------------------------------------------
int vtkChacoReader::InputGeom(vtkIdType nvtxs, // Number of vertices to read in
  int igeom,                                   // Dimension (1, 2 or 3), or 0 if you don't know
  double* x, double* y, double* z)
{
  double xc = 0.0, yc = 0.0, zc = 0.0;
  int end_flag, ndims, i = 0;

  clearerr(this->CurrentGeometryFP);           // clear error and EOF flags
  fseek(this->CurrentGeometryFP, 0, SEEK_SET); // move to beginning

  end_flag = 1;
  while (end_flag == 1)
  {
    xc = this->ReadVal(this->CurrentGeometryFP, &end_flag);
  }

  if (end_flag == -1)
  {
    vtkErrorMacro(<< "No values found in geometry file " << this->BaseName << ".coords");
    return 0;
  }

  if (igeom == 0)
  {
    ndims = 1;
    yc = this->ReadVal(this->CurrentGeometryFP, &end_flag);
    if (end_flag == 0)
    {
      ndims = 2;
      zc = this->ReadVal(this->CurrentGeometryFP, &end_flag);
      if (end_flag == 0)
      {
        ndims = 3;
        this->ReadVal(this->CurrentGeometryFP, &end_flag);
        if (!end_flag)
        {
          vtkErrorMacro(<< "Invalid geometry file " << this->BaseName << ".coords");
          return 0;
        }
      }
    }
    this->Dimensionality = ndims;
  }
  else
  {
    ndims = this->Dimensionality;
    if (ndims > 1)
    {
      yc = this->ReadVal(this->CurrentGeometryFP, &end_flag);
      if (ndims > 2)
      {
        zc = this->ReadVal(this->CurrentGeometryFP, &end_flag);
      }
    }
    this->ReadVal(this->CurrentGeometryFP, &end_flag);
  }

  x[0] = xc;
  if (ndims > 1)
  {
    y[0] = yc;
    if (ndims > 2)
    {
      z[0] = zc;
    }
  }

  if (nvtxs == 1)
  {
    return 1;
  }

  for (int nread = 1; nread < nvtxs; nread++)
  {
    if (ndims == 1)
    {
      i = fscanf(this->CurrentGeometryFP, "%lf", x + nread);
    }
    else if (ndims == 2)
    {
      i = fscanf(this->CurrentGeometryFP, "%lf%lf", x + nread, y + nread);
    }
    else if (ndims == 3)
    {
      i = fscanf(this->CurrentGeometryFP, "%lf%lf%lf", x + nread, y + nread, z + nread);
    }

    if (i == EOF)
    {
      vtkErrorMacro(<< "Too few lines in " << this->BaseName << ".coords");
      return 0;
    }
    else if (i != ndims)
    {
      vtkErrorMacro(<< "Wrong dimension in " << this->BaseName << ".coords");
      return 0;
    }
  }

  return 1;
}

//------------------------------------------------------------------------------
int vtkChacoReader::InputGraph1()
{
  /* Read first line of input (= nvtxs, narcs, option). */
  /* The (decimal) digits of the option variable mean: 1's digit not zero => input
     edge weights 10's digit not zero => input vertex weights 100's digit not zero
     => include vertex numbers */

  FILE* fin = this->CurrentGraphFP;
  clearerr(fin);           // clear error and EOF flags
  fseek(fin, 0, SEEK_SET); // move to beginning

  /* Read any leading comment lines */
  int end_flag = 1;
  vtkIdType numVertices = 0;

  while (end_flag == 1)
  {
    numVertices = this->ReadInt(fin, &end_flag);
  }
  if (numVertices <= 0)
  {
    vtkErrorMacro(<< "Invalid file " << this->BaseName << ".graph");
    return 0;
  }

  this->NumberOfVertices = numVertices;

  this->NumberOfEdges = this->ReadInt(fin, &end_flag);
  if (this->NumberOfEdges < 0)
  {
    vtkErrorMacro(<< "Invalid file " << this->BaseName << ".graph");
    return 0;
  }

  this->NumberOfVertexWeights = 0;
  this->NumberOfEdgeWeights = 0;
  this->GraphFileHasVertexNumbers = 0;

  /*  Check if vertex or edge weights are used */
  if (!end_flag)
  {
    vtkIdType option = this->ReadInt(fin, &end_flag);
    this->NumberOfEdgeWeights = (int)(option - 10 * (option / 10));
    option /= 10;
    this->NumberOfVertexWeights = (int)(option - 10 * (option / 10));
    option /= 10;
    this->GraphFileHasVertexNumbers = (int)(option - 10 * (option / 10));
  }

  /* Read weight dimensions if they are specified separately */
  if (!end_flag && this->NumberOfVertexWeights == 1)
  {
    vtkIdType j = this->ReadInt(fin, &end_flag);
    if (!end_flag)
      this->NumberOfVertexWeights = (int)j;
  }
  if (!end_flag && this->NumberOfEdgeWeights == 1)
  {
    vtkIdType j = this->ReadInt(fin, &end_flag);
    if (!end_flag)
      this->NumberOfEdgeWeights = (int)j;
  }

  return 1;
}

//------------------------------------------------------------------------------
int vtkChacoReader::InputGraph2(
  vtkIdType** start,     // start[i]: location of vertex i in adjacency array
  vtkIdType** adjacency, // by vertex by vertex neighbor
  double** vweights,     // by vertex by weight (or nullptr if no weights wanted)
  double** eweights)     // edge weights in order in file (or nullptr)
{
  vtkIdType* adjptr;
  double* ewptr;
  vtkIdType vtx, sum_edges, vertex, new_vertex;
  double weight, eweight;
  vtkIdType neighbor, j;
  int retVal = 1;

  vtkIdType nvtxs = this->NumberOfVertices;
  vtkIdType narcs = this->NumberOfEdges;
  int vwgt_dim = this->NumberOfVertexWeights;
  int ewgt_dim = this->NumberOfEdgeWeights;
  int vtxnums = this->GraphFileHasVertexNumbers;

  if (nvtxs < 1)
  {
    vtkErrorMacro(<< "vtkChacoReader::InputGraph2, NumberOfVertices not set");
    return 0;
  }

  if (start == nullptr)
  {
    vtkErrorMacro(<< "vtkChacoReader::InputGraph2, parameter list");
    return 0;
  }

  *start = nullptr;
  if (adjacency)
  {
    *adjacency = nullptr;
  }
  if (vweights)
  {
    *vweights = nullptr;
  }
  if (eweights)
  {
    *eweights = nullptr;
  }

  int line_num = 0;

  FILE* fin = this->CurrentGraphFP;
  clearerr(fin);           // clear error and EOF flags
  fseek(fin, 0, SEEK_SET); // move to beginning

  /* Read past the first line containing the metadata */

  int end_flag = 1;
  while (end_flag == 1)
  {
    this->ReadInt(fin, &end_flag);
    ++line_num;
  }
  while (!end_flag)
  {
    this->ReadInt(fin, &end_flag);
  }
  ++line_num;

  /* Allocate space for rows and columns. */
  *start = new vtkIdType[nvtxs + 1];
  if (adjacency && (narcs > 0))
  {
    *adjacency = new vtkIdType[2 * narcs + 1]; // why +1 ?
  }
  if (vweights && (vwgt_dim > 0))
  {
    *vweights = new double[nvtxs * vwgt_dim];
  }

  if (eweights && (ewgt_dim > 0) && (narcs > 0))
  {
    *eweights = new double[(2 * narcs + 1) * ewgt_dim]; // why +1 ?
  }

  adjptr = (adjacency ? *adjacency : nullptr);
  ewptr = (eweights ? *eweights : nullptr);

  sum_edges = 0;
  (*start)[0] = 0;
  vertex = 0;
  vtx = 0;
  new_vertex = 1;
  while (((vwgt_dim > 0) || vtxnums || narcs) && end_flag != -1)
  {
    ++line_num;

    /* If multiple input lines per vertex, read vertex number. */
    if (vtxnums)
    {
      j = this->ReadInt(fin, &end_flag);
      if (end_flag)
      {
        if (vertex == nvtxs)
        {
          break;
        }

        vtkErrorMacro(<< "Missing vertex number " << this->BaseName << ".graph, line " << line_num);
        retVal = 0;
        goto done;
      }
      if (j != vertex && j != vertex + 1)
      {
        vtkErrorMacro(<< "Out of order vertex " << this->BaseName << ".graph, line " << line_num);
        retVal = 0;
        goto done;
      }
      if (j != vertex)
      {
        new_vertex = 1;
        vertex = j;
      }
      else
      {
        new_vertex = 0;
      }
    }
    else
    {
      vertex = ++vtx;
    }

    if (vertex > nvtxs)
      break;

    /* If vertices are weighted, read vertex weight. */
    if ((vwgt_dim > 0) && new_vertex)
    {
      for (j = 0; j < (vwgt_dim); j++)
      {
        weight = ReadVal(fin, &end_flag);
        if (end_flag)
        {
          vtkErrorMacro(<< "Vertex weights " << this->BaseName << ".graph, line " << line_num);
          retVal = 0;
          goto done;
        }

        if (vweights)
        {
          (*vweights)[(vertex - 1) * (vwgt_dim) + j] = weight;
        }
      }
    }

    /* Read number of adjacent vertex. */
    neighbor = this->ReadInt(fin, &end_flag);

    while (!end_flag)
    {
      if (ewgt_dim > 0)
      {
        for (j = 0; j < ewgt_dim; j++)
        {
          eweight = ReadVal(fin, &end_flag);

          if (end_flag)
          {
            vtkErrorMacro(<< "Edge weights " << this->BaseName << ".graph, line " << line_num);
            retVal = 0;
            goto done;
          }

          if (ewptr)
          {
            *ewptr++ = eweight;
          }
        }
      }

      /* Add edge to data structure. */
      if (++sum_edges > 2 * narcs)
      {
        vtkErrorMacro(<< "Too many adjacencies " << this->BaseName << ".graph, line " << line_num);
        retVal = 0;
        goto done;
      }

      if (adjptr)
      {
        *adjptr++ = neighbor;
      }

      /* Read number of next adjacent vertex. */
      neighbor = this->ReadInt(fin, &end_flag);
    }

    (*start)[vertex] = sum_edges;
  }

done:

  if ((vertex == 0) || (retVal == 0))
  {
    /* Graph was empty */
    delete[] *start;
    *start = nullptr;

    delete[] *adjacency;
    *adjacency = nullptr;

    delete[] *vweights;
    *vweights = nullptr;

    delete[] *eweights;
    *eweights = nullptr;
  }

  return retVal;
}

//------------------------------------------------------------------------------
double vtkChacoReader::ReadVal(FILE* infile, int* end_flag)
{
  double val;
  char* ptr;
  char* ptr2;
  int length;
  int length_left;
  int white_seen;
  int done;
  int i;

  *end_flag = 0;

  if (Offset == 0 || this->Offset >= this->Break_pnt)
  {
    if (Offset >= this->Break_pnt)
    {
      length_left = this->Line_length - this->Save_pnt - 1;
      ptr2 = this->Line;
      ptr = &Line[Save_pnt];
      for (i = length_left; i; i--)
        *ptr2++ = *ptr++;
      length = this->Save_pnt + 1;
    }
    else
    {
      length = this->Line_length;
      length_left = 0;
    }

    this->Line[this->Line_length - 1] = ' ';
    this->Line[this->Line_length - 2] = ' ';
    /* Now read next line, or next segment of current one. */
    ptr2 = fgets(&Line[length_left], length, infile);

    if (ptr2 == nullptr)
    {
      *end_flag = -1;
      return 0.0;
    }

    if (Line[this->Line_length - 1] == '\0' && this->Line[this->Line_length - 2] != '\0' &&
      this->Line[this->Line_length - 2] != '\n' && this->Line[this->Line_length - 2] != '\f')
    {
      /* Line too long.  Find last safe place in line. */
      this->Break_pnt = this->Line_length - 1;
      this->Save_pnt = this->Break_pnt;
      white_seen = 0;
      done = 0;
      while (!done)
      {
        --Break_pnt;
        if (Line[Break_pnt] != '\0')
        {
          if (isspace((int)(Line[Break_pnt])))
          {
            if (!white_seen)
            {
              this->Save_pnt = this->Break_pnt + 1;
              white_seen = 1;
            }
          }
          else if (white_seen)
          {
            done = 1;
          }
        }
      }
    }
    else
    {
      this->Break_pnt = this->Line_length;
    }

    this->Offset = 0;
  }

  while (isspace((int)(Line[Offset])) && this->Offset < this->Line_length)
    this->Offset++;
  if (Line[Offset] == '%' || this->Line[Offset] == '#')
  {
    *end_flag = 1;
    if (Break_pnt < this->Line_length)
    {
      FlushLine(infile);
    }
    return 0.0;
  }

  ptr = &(Line[Offset]);
  val = strtod(ptr, &ptr2);

  if (ptr2 == ptr)
  {
    this->Offset = 0;
    *end_flag = 1;
    return 0.0;
  }
  else
  {
    this->Offset = (int)(ptr2 - this->Line) / sizeof(char);
  }

  return (val);
}

//------------------------------------------------------------------------------
vtkIdType vtkChacoReader::ReadInt(FILE* infile, int* end_flag)
{
  vtkIdType val;
  char* ptr;
  char* ptr2;
  int length;
  int length_left;
  int white_seen;
  int done;
  int i;

  *end_flag = 0;

  if (Offset == 0 || this->Offset >= this->Break_pnt)
  {
    if (Offset >= this->Break_pnt)
    {
      length_left = this->Line_length - this->Save_pnt - 1;
      ptr2 = this->Line;
      ptr = &Line[Save_pnt];
      for (i = length_left; i; i--)
        *ptr2++ = *ptr++;
      length = this->Save_pnt + 1;
    }
    else
    {
      length = this->Line_length;
      length_left = 0;
    }

    this->Line[this->Line_length - 1] = ' ';
    this->Line[this->Line_length - 2] = ' ';
    /* Now read next line, or next segment of current one. */
    ptr2 = fgets(&Line[length_left], length, infile);

    if (ptr2 == nullptr)
    {
      *end_flag = -1;
      return (0);
    }

    if (this->Line[this->Line_length - 1] == '\0' && this->Line[this->Line_length - 2] != '\0' &&
      this->Line[this->Line_length - 2] != '\n' && this->Line[this->Line_length - 2] != '\f')
    {
      /*Line too long.  Find last safe place in line. */
      this->Break_pnt = this->Line_length - 1;
      this->Save_pnt = this->Break_pnt;
      white_seen = 0;
      done = 0;
      while (!done)
      {
        --Break_pnt;
        if (Line[Break_pnt] != '\0')
        {
          if (isspace((int)(Line[Break_pnt])))
          {
            if (!white_seen)
            {
              this->Save_pnt = this->Break_pnt + 1;
              white_seen = 1;
            }
          }
          else if (white_seen)
          {
            done = 1;
          }
        }
      }
    }
    else
    {
      this->Break_pnt = this->Line_length;
    }

    this->Offset = 0;
  }

  while (isspace((int)(Line[Offset])) && this->Offset < this->Line_length)
    this->Offset++;
  if (Line[Offset] == '%' || this->Line[Offset] == '#')
  {
    *end_flag = 1;
    if (Break_pnt < this->Line_length)
    {
      FlushLine(infile);
    }
    return (0);
  }

  ptr = &(Line[Offset]);
  val = (int)strtol(ptr, &ptr2, 10);

  if (ptr2 == ptr)
  {
    this->Offset = 0;
    *end_flag = 1;
    return (0);
  }
  else
  {
    this->Offset = (int)(ptr2 - this->Line) / sizeof(char);
  }

  return (val);
}

//------------------------------------------------------------------------------
void vtkChacoReader::FlushLine(FILE* infile)
{
  char c;

  c = getc(infile);
  while (c != '\n' && c != '\f')
  {
    c = getc(infile);
  }
}
VTK_ABI_NAMESPACE_END
