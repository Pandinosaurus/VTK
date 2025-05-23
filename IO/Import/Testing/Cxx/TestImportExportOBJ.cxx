// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkTestUtilities.h>

// Importers
#include <vtk3DSImporter.h>
#include <vtkGLTFImporter.h>

// Exporters
#include <vtkOBJExporter.h>

#include <algorithm> // For transform()
#include <cctype>    // For to_lower
#include <sstream>   // For stringstream
#include <string>    // For find_last_of()

int TestImportExportOBJ(int argc, char* argv[])
{
  char* tname =
    vtkTestUtilities::GetArgOrEnvOrDefault("-T", argc, argv, "VTK_TEMP_DIR", "Testing/Temporary");
  delete[] tname;

  auto renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
  auto renderer = vtkSmartPointer<vtkRenderer>::New();

  std::string fileName = argv[1];
  std::string extension;
  // Make the extension lowercase
  std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
  if (fileName.find_last_of('.') != std::string::npos)
  {
    extension = fileName.substr(fileName.find_last_of('.') + 1);
  }
  if (extension == "3ds")
  {
    auto importer = vtkSmartPointer<vtk3DSImporter>::New();
    importer->SetFileName(argv[1]);
    importer->SetRenderWindow(renderWindow);
    renderWindow = importer->GetRenderWindow();
    renderer = importer->GetRenderer();
    if (!importer->Update())
    {
      std::cerr << "ERROR: Importer failed to update\n";
      return EXIT_FAILURE;
    }
  }
  else if (extension == "gltf" || extension == "glb")
  {
    auto importer = vtkSmartPointer<vtkGLTFImporter>::New();
    importer->SetFileName(argv[1]);
    importer->SetRenderWindow(renderWindow);
    renderWindow = importer->GetRenderWindow();
    renderer = importer->GetRenderer();
    if (!importer->Update())
    {
      std::cerr << "ERROR: Importer failed to update\n";
      return EXIT_FAILURE;
    }
  }
  else
  {
    std::cout << "Error: Extension " << extension << " is not supported" << std::endl;
    return EXIT_FAILURE;
  }

  std::stringstream comment;
  auto exporter = vtkSmartPointer<vtkOBJExporter>::New();
  comment << "Converted by ImportExport from " << fileName;
  exporter->SetFilePrefix(argv[2]);
  exporter->SetOBJFileComment(comment.str().c_str());
  exporter->SetMTLFileComment(comment.str().c_str());
  exporter->SetActiveRenderer(renderer);
  exporter->SetRenderWindow(renderWindow);
  exporter->Write();

  return EXIT_SUCCESS;
}
