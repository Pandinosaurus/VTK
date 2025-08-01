// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

#include "vtkAMREnzoReader.h"
#include "vtkAMRBox.h"
#include "vtkDataArray.h"
#include "vtkDataArraySelection.h"
#include "vtkIndent.h"
#include "vtkInformation.h"
#include "vtkObjectFactory.h"
#include "vtkOverlappingAMR.h"
#include "vtkPolyData.h"
#include "vtkUniformGrid.h"
#include "vtksys/FStream.hxx"
#include "vtksys/SystemTools.hxx"

#include "vtkCellData.h"
#include "vtkDataSet.h"
#include "vtkDoubleArray.h"
#include "vtkFloatArray.h"
#include "vtkIntArray.h"
#include "vtkLongArray.h"
#include "vtkLongLongArray.h"
#include "vtkShortArray.h"
#include "vtkUnsignedCharArray.h"
#include "vtkUnsignedIntArray.h"
#include "vtkUnsignedShortArray.h"

#define H5_USE_16_API
#include "vtk_hdf5.h"

#include <cassert>
#include <sstream>
#include <string>
#include <vector>

#include "vtkAMREnzoReaderInternal.h"

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkAMREnzoReader);

VTK_ABI_NAMESPACE_END
#include <limits>

VTK_ABI_NAMESPACE_BEGIN
void vtkAMREnzoReader::ComputeStats(
  vtkEnzoReaderInternal* internal, std::vector<unsigned int>& numBlocks, double min[3])
{
  min[0] = min[1] = min[2] = std::numeric_limits<double>::max();
  numBlocks.resize(this->Internal->NumberOfLevels, 0);

  for (int i = 0; i < internal->NumberOfBlocks; ++i)
  {
    vtkEnzoReaderBlock& theBlock = internal->Blocks[i + 1];
    double* gridMin = theBlock.MinBounds;
    if (gridMin[0] < min[0])
    {
      min[0] = gridMin[0];
    }
    if (gridMin[1] < min[1])
    {
      min[1] = gridMin[1];
    }
    if (gridMin[2] < min[2])
    {
      min[2] = gridMin[2];
    }
    numBlocks[theBlock.Level]++;
  }
}

//------------------------------------------------------------------------------
vtkAMREnzoReader::vtkAMREnzoReader()
{
  this->Internal = new vtkEnzoReaderInternal();
  this->IsReady = false;
  this->Initialize();
  this->ConvertToCGS = 1;
}

//------------------------------------------------------------------------------
vtkAMREnzoReader::~vtkAMREnzoReader()
{
  delete this->Internal;
  this->Internal = nullptr;

  this->BlockMap.clear();
}

//------------------------------------------------------------------------------
void vtkAMREnzoReader::PrintSelf(std::ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//------------------------------------------------------------------------------
int vtkAMREnzoReader::GetIndexFromArrayName(std::string arrayName)
{
  char stringIdx[2];
  stringIdx[0] = arrayName.at(arrayName.size() - 2);
  stringIdx[1] = '\0';
  return (atoi(stringIdx));
}

//------------------------------------------------------------------------------
double vtkAMREnzoReader::GetConversionFactor(const std::string& name)
{
  if (this->label2idx.find(name) != this->label2idx.end())
  {
    int idx = this->label2idx[name];
    if (this->conversionFactors.find(idx) != this->conversionFactors.end())
    {
      return (this->conversionFactors[idx]);
    }
    else
    {
      return (1.0);
    }
  }
  return (1.0);
}

//------------------------------------------------------------------------------
void vtkAMREnzoReader::ParseLabel(const std::string& labelString, int& idx, std::string& label)
{

  std::vector<std::string> strings;

  std::istringstream iss(labelString);
  std::string word;
  while (iss >> word)
  {
    if (!vtksys::SystemTools::StringStartsWith(word.c_str(), "="))
    {
      strings.push_back(word);
    }
  }

  idx = this->GetIndexFromArrayName(strings[0]);
  label = strings[strings.size() - 1];
}

//------------------------------------------------------------------------------
void vtkAMREnzoReader::ParseCFactor(const std::string& labelString, int& idx, double& factor)
{
  std::vector<std::string> strings;

  std::istringstream iss(labelString);
  std::string word;
  while (iss >> word)
  {
    if (!vtksys::SystemTools::StringStartsWith(word.c_str(), "="))
    {
      strings.push_back(word);
    }
  }

  idx = this->GetIndexFromArrayName(strings[0]);
  factor = atof(strings[strings.size() - 1].c_str());
}

//------------------------------------------------------------------------------
void vtkAMREnzoReader::ParseConversionFactors()
{
  assert("pre: FileName should not be nullptr" && (this->FileName != nullptr));

  // STEP 0: Extract the parameters file from the user-supplied filename
  std::string baseDir = vtksys::SystemTools::GetFilenamePath(std::string(this->FileName));

  std::string paramsFile =
    baseDir + "/" + vtksys::SystemTools::GetFilenameWithoutExtension(std::string(this->FileName));

  // STEP 1: Open Parameters file
  vtksys::ifstream ifs;
  ifs.open(paramsFile.c_str());
  if (!ifs.is_open())
  {
    vtkWarningMacro("Cannot open ENZO parameters file!\n");
    return;
  }

  // STEP 2: Parsing parameters file
  std::string line;  // temp string to store a line read from the params file
  std::string label; // stores the attribute name
  double cf;         // stores the conversion factor
  int idx;           // stores the attribute label index
  while (getline(ifs, line))
  {
    if (vtksys::SystemTools::StringStartsWith(line.c_str(), "DataLabel"))
    {
      this->ParseLabel(line, idx, label);
      this->label2idx[label] = idx;
    }
    else if (vtksys::SystemTools::StringStartsWith(line.c_str(), "#DataCGSConversionFactor"))
    {
      this->ParseCFactor(line, idx, cf);
      this->conversionFactors[idx] = cf;
    }
  }

  // STEP 3: Close parameters file
  ifs.close();
}

//------------------------------------------------------------------------------
void vtkAMREnzoReader::SetFileName(const char* fileName)
{
  assert("pre: Internal Enzo AMR Reader is nullptr" && (this->Internal != nullptr));

  if (fileName && strcmp(fileName, "") != 0 &&
    ((this->FileName == nullptr) || (strcmp(fileName, this->FileName) != 0)))
  {
    std::string tempName(fileName);
    std::string bExtName(".boundary");
    std::string hExtName(".hierarchy");

    if (tempName.length() > hExtName.length() &&
      tempName.substr(tempName.length() - hExtName.length()) == hExtName)
    {
      this->Internal->MajorFileName = tempName.substr(0, tempName.length() - hExtName.length());
      this->Internal->HierarchyFileName = tempName;
      this->Internal->BoundaryFileName = this->Internal->MajorFileName + bExtName;
    }
    else if (tempName.length() > bExtName.length() &&
      tempName.substr(tempName.length() - bExtName.length()) == bExtName)
    {
      this->Internal->MajorFileName = tempName.substr(0, tempName.length() - bExtName.length());
      this->Internal->BoundaryFileName = tempName;
      this->Internal->HierarchyFileName = this->Internal->MajorFileName + hExtName;
    }
    else
    {
      vtkErrorMacro("Enzo file has invalid extension!");
      return;
    }

    this->IsReady = true;
    this->Internal->DirectoryName = GetEnzoDirectory(this->Internal->MajorFileName.c_str());
  }

  if (this->IsReady)
  {
    this->BlockMap.clear();
    this->Internal->Blocks.clear();
    this->Internal->NumberOfBlocks = 0;
    this->LoadedMetaData = false;

    if (this->FileName != nullptr)
    {
      delete[] this->FileName;
      this->FileName = nullptr;
      this->Internal->SetFileName(nullptr);
    }
    this->FileName = new char[strlen(fileName) + 1];
    strcpy(this->FileName, fileName);
    this->FileName[strlen(fileName)] = '\0';
    this->Internal->SetFileName(this->FileName);
    this->ParseConversionFactors();

    this->Internal->ReadMetaData();
    this->SetUpDataArraySelections();
    this->InitializeArraySelections();
  }

  this->Modified();
}

//------------------------------------------------------------------------------
void vtkAMREnzoReader::ReadMetaData()
{
  assert("pre: Internal Enzo Reader is nullptr" && (this->Internal != nullptr));

  if (!this->IsReady)
  {
    return;
  }

  this->Internal->ReadMetaData();
}

//------------------------------------------------------------------------------
int vtkAMREnzoReader::GetBlockLevel(int blockIdx)
{
  assert("pre: Internal Enzo Reader is nullptr" && (this->Internal != nullptr));

  if (!this->IsReady)
  {
    return (-1);
  }

  this->Internal->ReadMetaData();

  if (blockIdx < 0 || blockIdx >= this->Internal->NumberOfBlocks)
  {
    vtkErrorMacro("Block Index (" << blockIdx << ") is out-of-bounds!");
    return (-1);
  }
  return (this->Internal->Blocks[blockIdx + 1].Level);
}

//------------------------------------------------------------------------------
int vtkAMREnzoReader::GetNumberOfBlocks()
{
  assert("pre: Internal Enzo Reader is nullptr" && (this->Internal != nullptr));
  if (!this->IsReady)
  {
    return 0;
  }

  this->Internal->ReadMetaData();
  return (this->Internal->NumberOfBlocks);
}

//------------------------------------------------------------------------------
int vtkAMREnzoReader::GetNumberOfLevels()
{
  assert("pre: Internal Enzo Reader is nullptr" && (this->Internal != nullptr));
  if (!this->IsReady)
  {
    return 0;
  }

  this->Internal->ReadMetaData();
  return (this->Internal->NumberOfLevels);
}

//------------------------------------------------------------------------------
int vtkAMREnzoReader::FillMetaData()
{
  assert("pre: Internal Enzo Reader is nullptr" && (this->Internal != nullptr));
  assert("pre: metadata object is nullptr" && (this->Metadata != nullptr));
  if (!this->IsReady)
  {
    return 0;
  }

  this->Internal->ReadMetaData();

  double origin[3];
  std::vector<unsigned int> blocksPerLevel;
  this->ComputeStats(this->Internal, blocksPerLevel, origin);

  this->Metadata->Initialize(blocksPerLevel);
  this->Metadata->SetGridDescription(vtkStructuredData::VTK_STRUCTURED_XYZ_GRID);
  this->Metadata->SetOrigin(origin);

  std::vector<int> b2level(this->Internal->NumberOfLevels + 1, 0);
  for (int block = 0; block < this->Internal->NumberOfBlocks; ++block)
  {
    vtkEnzoReaderBlock& theBlock = this->Internal->Blocks[block + 1];
    int level = theBlock.Level;
    int internalIdx = block;
    int id = b2level[level];

    // compute spacing
    double spacing[3];
    for (int d = 0; d < 3; ++d)
    {
      spacing[d] = (theBlock.BlockNodeDimensions[d] > 1)
        ? (theBlock.MaxBounds[d] - theBlock.MinBounds[d]) / (theBlock.BlockNodeDimensions[d] - 1.0)
        : 1.0;
    }
    // compute AMRBox
    vtkAMRBox box(theBlock.MinBounds, theBlock.BlockNodeDimensions, spacing, origin,
      vtkStructuredData::VTK_STRUCTURED_XYZ_GRID);

    // set meta data
    this->Metadata->SetSpacing(level, spacing);
    this->Metadata->SetAMRBox(level, id, box);
    this->Metadata->SetAMRBlockSourceIndex(level, id, internalIdx);
    b2level[level]++;
  }
  this->Metadata->GenerateParentChildInformation();
  this->Metadata->GetInformation()->Set(vtkDataObject::DATA_TIME_STEP(), this->Internal->DataTime);
  return (1);
}

//------------------------------------------------------------------------------
vtkUniformGrid* vtkAMREnzoReader::GetAMRGrid(int blockIdx)
{
  assert("pre: Internal Enzo Reader is nullptr" && (this->Internal != nullptr));

  if (!this->IsReady)
  {
    return nullptr;
  }

  this->Internal->ReadMetaData();

  // this->Internal->Blocks includes a pseudo block --- the root as block #0
  vtkEnzoReaderBlock& theBlock = this->Internal->Blocks[blockIdx + 1];
  double blockMin[3];
  double blockMax[3];
  double spacings[3];

  for (int i = 0; i < 3; ++i)
  {
    blockMin[i] = theBlock.MinBounds[i];
    blockMax[i] = theBlock.MaxBounds[i];
    spacings[i] = (theBlock.BlockNodeDimensions[i] > 1)
      ? (blockMax[i] - blockMin[i]) / (theBlock.BlockNodeDimensions[i] - 1.0)
      : 1.0;
  }

  vtkUniformGrid* ug = vtkUniformGrid::New();
  ug->SetDimensions(theBlock.BlockNodeDimensions);
  ug->SetOrigin(blockMin[0], blockMin[1], blockMin[2]);
  ug->SetSpacing(spacings[0], spacings[1], spacings[2]);
  return (ug);
}

//------------------------------------------------------------------------------
void vtkAMREnzoReader::GetAMRGridData(int blockIdx, vtkUniformGrid* block, const char* field)
{
  assert("pre: AMR block is nullptr" && (block != nullptr));

  this->Internal->GetBlockAttribute(field, blockIdx, block);
  if (this->ConvertToCGS == 1)
  {
    double conversionFactor = this->GetConversionFactor(field);
    if (conversionFactor != 1.0)
    {
      vtkDataArray* data = block->GetCellData()->GetArray(field);
      assert("pre: data array is nullptr!" && (data != nullptr));

      vtkIdType numTuples = data->GetNumberOfTuples();
      for (vtkIdType t = 0; t < numTuples; ++t)
      {
        int numComp = data->GetNumberOfComponents();
        for (int c = 0; c < numComp; ++c)
        {
          double f = data->GetComponent(t, c);
          data->SetComponent(t, c, f * conversionFactor);
        } // END for all components
      }   // END for all tuples
    }     // END if the conversion factor is not 1.0
  }       // END if conversion to CGS units is requested
}

//------------------------------------------------------------------------------
void vtkAMREnzoReader::SetUpDataArraySelections()
{
  assert("pre: Internal Enzo Reader is nullptr" && (this->Internal != nullptr));
  this->Internal->ReadMetaData();
  this->Internal->GetAttributeNames();

  int numAttrs = static_cast<int>(this->Internal->BlockAttributeNames.size());
  for (int i = 0; i < numAttrs; i++)
  {
    this->CellDataArraySelection->AddArray(this->Internal->BlockAttributeNames[i].c_str());
  } // END for all attributes
}
VTK_ABI_NAMESPACE_END
