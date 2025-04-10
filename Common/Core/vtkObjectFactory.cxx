// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "vtkObjectFactory.h"

#include "vtkDebugLeaks.h"
#include "vtkDynamicLoader.h"
#include "vtkObjectFactoryCollection.h"
#include "vtkOverrideInformation.h"
#include "vtkOverrideInformationCollection.h"
#include "vtkVersion.h"

#include "vtksys/Directory.hxx"

#include <cctype>

VTK_ABI_NAMESPACE_BEGIN
vtkObjectFactoryCollection* vtkObjectFactory::RegisteredFactories = nullptr;
static unsigned int vtkObjectFactoryRegistryCleanupCounter = 0;

vtkObjectFactoryRegistryCleanup::vtkObjectFactoryRegistryCleanup()
{
  ++vtkObjectFactoryRegistryCleanupCounter;
}

vtkObjectFactoryRegistryCleanup::~vtkObjectFactoryRegistryCleanup()
{
  if (--vtkObjectFactoryRegistryCleanupCounter == 0)
  {
    vtkObjectFactory::UnRegisterAllFactories();
  }
}

// Create an instance of a named vtk object using the loaded
// factories

vtkObject* vtkObjectFactory::CreateInstance(const char* vtkclassname, bool)
{
  if (!vtkObjectFactory::RegisteredFactories)
  {
    vtkObjectFactory::Init();
  }

  vtkObjectFactory* factory;
  vtkCollectionSimpleIterator osit;
  for (vtkObjectFactory::RegisteredFactories->InitTraversal(osit);
       (factory = vtkObjectFactory::RegisteredFactories->GetNextObjectFactory(osit));)
  {
    vtkObject* newobject = factory->CreateObject(vtkclassname);
    if (newobject)
    {
      return newobject;
    }
  }

  return nullptr;
}

// A one time initialization method.
void vtkObjectFactory::Init()
{
  // Don't do anything if we are already initialized
  if (vtkObjectFactory::RegisteredFactories)
  {
    return;
  }

  vtkObjectFactory::RegisteredFactories = vtkObjectFactoryCollection::New();
  vtkObjectFactory::RegisterDefaults();
  vtkObjectFactory::LoadDynamicFactories();
}

// Register any factories that are always present in VTK like
// the OpenGL factory, currently this is not done.

void vtkObjectFactory::RegisterDefaults() {}

// Load all libraries in VTK_AUTOLOAD_PATH

void vtkObjectFactory::LoadDynamicFactories()
{
  // follow PATH conventions
#ifdef _WIN32
  char PathSeparator = ';';
#else
  char PathSeparator = ':';
#endif

  char* LoadPath = nullptr;

#ifndef _WIN32_WCE
  LoadPath = getenv("VTK_AUTOLOAD_PATH");
#endif
  if (LoadPath == nullptr || LoadPath[0] == 0)
  {
    return;
  }
  std::string CurrentPath;
  CurrentPath.reserve(strlen(LoadPath) + 1);
  char* SeparatorPosition = LoadPath; // initialize to env variable
  while (SeparatorPosition)
  {
    CurrentPath.clear();

    size_t PathLength = 0;
    // find PathSeparator in LoadPath
    SeparatorPosition = strchr(LoadPath, PathSeparator);
    // if not found then use the whole string
    if (SeparatorPosition == nullptr)
    {
      PathLength = strlen(LoadPath);
    }
    else
    {
      PathLength = static_cast<size_t>(SeparatorPosition - LoadPath);
    }
    // copy the path out of LoadPath into CurrentPath
    CurrentPath.append(LoadPath, PathLength);
    // Get ready for the next path
    LoadPath = SeparatorPosition + 1;
    // Load the libraries in the current path
    vtkObjectFactory::LoadLibrariesInPath(CurrentPath);
  }
}

// A file scope helper function to concat path and file into
// a full path
static char* CreateFullPath(const std::string& path, const char* file)
{
  size_t lenpath = path.size();
  char* ret = new char[lenpath + strlen(file) + 2];
#ifdef _WIN32
  const char sep = '\\';
#else
  const char sep = '/';
#endif
  // make sure the end of path is a separator
  strcpy(ret, path.c_str());
  if (ret[lenpath - 1] != sep)
  {
    ret[lenpath] = sep;
    ret[lenpath + 1] = 0;
  }
  strcat(ret, file);
  return ret;
}

// A file scope typedef to make the cast code to the load
// function cleaner to read.
typedef vtkObjectFactory* (*VTK_LOAD_FUNCTION)();
typedef const char* (*VTK_VERSION_FUNCTION)();
typedef const char* (*VTK_COMPILER_FUNCTION)();

// A file scoped function to determine if a file has
// the shared library extension in its name, this converts name to lower
// case before the compare, vtkDynamicLoader always uses
// lower case for LibExtension values.

inline int vtkNameIsSharedLibrary(const char* name)
{
  int len = static_cast<int>(strlen(name));
  char* copy = new char[len + 1];

  for (int i = 0; i < len; i++)
  {
    copy[i] = static_cast<char>(tolower(name[i]));
  }
  copy[len] = 0;
  char* ret = strstr(copy, vtkDynamicLoader::LibExtension());
  delete[] copy;
  return (ret != nullptr);
}

void vtkObjectFactory::LoadLibrariesInPath(const std::string& path)
{
  vtksys::Directory dir;
  if (!dir.Load(path))
  {
    return;
  }

  // Attempt to load each file in the directory as a shared library
  for (unsigned long i = 0; i < dir.GetNumberOfFiles(); i++)
  {
    const char* file = dir.GetFile(i);
    // try to make sure the file has at least the extension
    // for a shared library in it.
    if (vtkNameIsSharedLibrary(file))
    {
      char* fullpath = CreateFullPath(path, file);
      vtkLibHandle lib = vtkDynamicLoader::OpenLibrary(fullpath);
      if (lib)
      {
        // Look for the symbol vtkLoad and vtkGetFactoryVersion in the library
        VTK_LOAD_FUNCTION loadfunction =
          (VTK_LOAD_FUNCTION)(vtkDynamicLoader::GetSymbolAddress(lib, "vtkLoad"));
        VTK_VERSION_FUNCTION versionFunction =
          (VTK_VERSION_FUNCTION)(vtkDynamicLoader::GetSymbolAddress(lib, "vtkGetFactoryVersion"));
        // if the symbol is found call it to create the factory
        // from the library
        if (loadfunction && versionFunction)
        {
          const char* version = (*versionFunction)();
          if (strcmp(version, vtkVersion::GetVTKSourceVersion()) != 0)
          {
            vtkGenericWarningMacro(<< "Incompatible factory rejected:"
                                   << "\nRunning VTK version: " << vtkVersion::GetVTKSourceVersion()
                                   << "\nFactory version: " << version
                                   << "\nPath to rejected factory: " << fullpath << "\n");
          }
          else
          {
            vtkObjectFactory* newfactory = (*loadfunction)();
            newfactory->LibraryVTKVersion = strcpy(new char[strlen(version) + 1], version);
            // initialize class members if load worked
            newfactory->LibraryHandle = static_cast<void*>(lib);
            newfactory->LibraryPath = strcpy(new char[strlen(fullpath) + 1], fullpath);
            vtkObjectFactory::RegisterFactory(newfactory);
            newfactory->Delete();
          }
        }
        // if only the loadfunction is found, then warn
        else if (loadfunction)
        {
          vtkGenericWarningMacro(
            "Old Style Factory not loaded.  Shared object has vtkLoad, but is missing "
            "vtkGetFactoryVersion.  Recompile factory: "
            << fullpath << ", and use VTK_FACTORY_INTERFACE_IMPLEMENT macro.");
        }
      }
      delete[] fullpath;
    }
  }
}

// Recheck the VTK_AUTOLOAD_PATH for new libraries

void vtkObjectFactory::ReHash()
{
  vtkObjectFactory::UnRegisterAllFactories();
  vtkObjectFactory::Init();
}

// initialize class members
vtkObjectFactory::vtkObjectFactory()
{
  this->LibraryHandle = nullptr;
  this->LibraryPath = nullptr;
  this->OverrideArray = nullptr;
  this->OverrideClassNames = nullptr;
  this->SizeOverrideArray = 0;
  this->OverrideArrayLength = 0;
  this->LibraryVTKVersion = nullptr;
}

// Unload the library and free the path string
vtkObjectFactory::~vtkObjectFactory()
{
  delete[] this->LibraryVTKVersion;
  delete[] this->LibraryPath;
  this->LibraryPath = nullptr;

  for (int i = 0; i < this->OverrideArrayLength; i++)
  {
    delete[] this->OverrideClassNames[i];
    delete[] this->OverrideArray[i].Description;
    delete[] this->OverrideArray[i].OverrideWithName;
  }
  delete[] this->OverrideArray;
  delete[] this->OverrideClassNames;
  this->OverrideArray = nullptr;
  this->OverrideClassNames = nullptr;
}

// Add a factory to the registered list
void vtkObjectFactory::RegisterFactory(vtkObjectFactory* factory)
{
  if (factory->LibraryHandle == nullptr)
  {
    const char* nonDynamicName = "Non-Dynamicly loaded factory";
    factory->LibraryPath = strcpy(new char[strlen(nonDynamicName) + 1], nonDynamicName);
    factory->LibraryVTKVersion = strcpy(
      new char[strlen(vtkVersion::GetVTKSourceVersion()) + 1], vtkVersion::GetVTKSourceVersion());
  }
  else
  {
    if (strcmp(factory->LibraryVTKVersion, vtkVersion::GetVTKSourceVersion()) != 0)
    {
      vtkGenericWarningMacro(<< "Possible incompatible factory load:"
                             << "\nRunning vtk version :\n"
                             << vtkVersion::GetVTKSourceVersion() << "\nLoaded Factory version:\n"
                             << factory->LibraryVTKVersion << "\nRejecting factory:\n"
                             << factory->LibraryPath << "\n");
      return;
    }
    if (strcmp(factory->GetVTKSourceVersion(), vtkVersion::GetVTKSourceVersion()) != 0)
    {
      vtkGenericWarningMacro(<< "Possible incompatible factory load:"
                             << "\nRunning vtk version :\n"
                             << vtkVersion::GetVTKSourceVersion() << "\nLoaded Factory version:\n"
                             << factory->GetVTKSourceVersion() << "\nRejecting factory:\n"
                             << factory->LibraryPath << "\n");
      return;
    }
  }

  vtkObjectFactory::Init();
  vtkObjectFactory::RegisteredFactories->AddItem(factory);
}

// print ivars to stream
void vtkObjectFactory::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  if (this->LibraryPath)
  {
    os << indent << "Factory DLL path: " << this->LibraryPath << "\n";
  }
  if (this->LibraryVTKVersion)
  {
    os << indent << "Library version: " << this->LibraryVTKVersion << "\n";
  }
  os << indent << "Factory description: " << this->GetDescription() << endl;
  int num = this->GetNumberOfOverrides();
  os << indent << "Factory overrides " << num << " classes:" << endl;
  indent = indent.GetNextIndent();
  for (int i = 0; i < num; i++)
  {
    os << indent << "Class : " << this->GetClassOverrideName(i) << endl;
    os << indent << "Overridden with: " << this->GetClassOverrideWithName(i) << endl;
    os << indent << "Enable flag: " << this->GetEnableFlag(i) << endl;
    os << endl;
  }
}

// Remove a factory from the list of registered factories.
void vtkObjectFactory::UnRegisterFactory(vtkObjectFactory* factory)
{
  void* lib = factory->LibraryHandle;
  vtkObjectFactory::RegisteredFactories->RemoveItem(factory);
  if (lib)
  {
    vtkDynamicLoader::CloseLibrary(static_cast<vtkLibHandle>(lib));
  }
}

// unregister all factories and delete the RegisteredFactories list
void vtkObjectFactory::UnRegisterAllFactories()
{
  // do not do anything if this is null
  if (!vtkObjectFactory::RegisteredFactories)
  {
    return;
  }
  int num = vtkObjectFactory::RegisteredFactories->GetNumberOfItems();
  // collect up all the library handles so they can be closed
  // AFTER the factory has been deleted.
  void** libs = new void*[num + 1];
  vtkObjectFactory* factory;
  vtkCollectionSimpleIterator osit;
  vtkObjectFactory::RegisteredFactories->InitTraversal(osit);
  int index = 0;
  while ((factory = vtkObjectFactory::RegisteredFactories->GetNextObjectFactory(osit)))
  {
    libs[index++] = factory->LibraryHandle;
  }
  // delete the factory list and its factories
  vtkObjectFactory::RegisteredFactories->Delete();
  vtkObjectFactory::RegisteredFactories = nullptr;
  // now close the libraries
  for (int i = 0; i < num; i++)
  {
    void* lib = libs[i];
    if (lib)
    {
      vtkDynamicLoader::CloseLibrary(reinterpret_cast<vtkLibHandle>(lib));
    }
  }
  delete[] libs;
}

// Register an override function with a factory.
void vtkObjectFactory::RegisterOverride(const char* classOverride, const char* subclass,
  const char* description, int enableFlag, CreateFunction createFunction)
{
  this->GrowOverrideArray();
  int nextIndex = this->OverrideArrayLength;
  this->OverrideArrayLength++;
  char* className = strcpy(new char[strlen(classOverride) + 1], classOverride);
  char* desc = strcpy(new char[strlen(description) + 1], description);
  char* ocn = strcpy(new char[strlen(subclass) + 1], subclass);
  this->OverrideClassNames[nextIndex] = className;
  this->OverrideArray[nextIndex].Description = desc;
  this->OverrideArray[nextIndex].OverrideWithName = ocn;
  this->OverrideArray[nextIndex].EnabledFlag = enableFlag;
  this->OverrideArray[nextIndex].CreateCallback = createFunction;
}

// Create an instance of an object
vtkObject* vtkObjectFactory::CreateObject(const char* vtkclassname)
{
  for (int i = 0; i < this->OverrideArrayLength; i++)
  {
    if (this->OverrideArray[i].EnabledFlag &&
      strcmp(this->OverrideClassNames[i], vtkclassname) == 0)
    {
      return (*this->OverrideArray[i].CreateCallback)();
    }
  }
  return nullptr;
}

// grow the array if the length is greater than the size.
void vtkObjectFactory::GrowOverrideArray()
{
  if (this->OverrideArrayLength + 1 > this->SizeOverrideArray)
  {
    int newLength = this->OverrideArrayLength + 50;
    OverrideInformation* newArray = new OverrideInformation[newLength];
    char** newNameArray = new char*[newLength];
    for (int i = 0; i < this->OverrideArrayLength; i++)
    {
      newNameArray[i] = this->OverrideClassNames[i];
      newArray[i] = this->OverrideArray[i];
    }
    delete[] this->OverrideClassNames;
    this->OverrideClassNames = newNameArray;
    delete[] this->OverrideArray;
    this->OverrideArray = newArray;
  }
}

int vtkObjectFactory::GetNumberOfOverrides() VTK_FUTURE_CONST
{
  return this->OverrideArrayLength;
}

const char* vtkObjectFactory::GetClassOverrideName(int index) VTK_FUTURE_CONST
{
  return this->OverrideClassNames[index];
}

const char* vtkObjectFactory::GetClassOverrideWithName(int index) VTK_FUTURE_CONST
{
  return this->OverrideArray[index].OverrideWithName;
}

vtkTypeBool vtkObjectFactory::GetEnableFlag(int index) VTK_FUTURE_CONST
{
  return this->OverrideArray[index].EnabledFlag;
}

const char* vtkObjectFactory::GetOverrideDescription(int index) VTK_FUTURE_CONST
{
  return this->OverrideArray[index].Description;
}

// Set the enable flag for a class / subclassName pair
void vtkObjectFactory::SetEnableFlag(
  vtkTypeBool flag, const char* className, const char* subclassName)
{
  for (int i = 0; i < this->OverrideArrayLength; i++)
  {
    if (strcmp(this->OverrideClassNames[i], className) == 0)
    {
      // if subclassName is null, then set on className match
      if (!subclassName)
      {
        this->OverrideArray[i].EnabledFlag = flag;
      }
      else
      {
        if (strcmp(this->OverrideArray[i].OverrideWithName, subclassName) == 0)
        {
          this->OverrideArray[i].EnabledFlag = flag;
        }
      }
    }
  }
}

// Get the enable flag for a className/subclassName pair
vtkTypeBool vtkObjectFactory::GetEnableFlag(
  const char* className, const char* subclassName) VTK_FUTURE_CONST
{
  for (int i = 0; i < this->OverrideArrayLength; i++)
  {
    if (strcmp(this->OverrideClassNames[i], className) == 0)
    {
      if (strcmp(this->OverrideArray[i].OverrideWithName, subclassName) == 0)
      {
        return this->OverrideArray[i].EnabledFlag;
      }
    }
  }
  return 0;
}

// Set the EnabledFlag to 0 for a given classname
void vtkObjectFactory::Disable(const char* className)
{
  for (int i = 0; i < this->OverrideArrayLength; i++)
  {
    if (strcmp(this->OverrideClassNames[i], className) == 0)
    {
      this->OverrideArray[i].EnabledFlag = 0;
    }
  }
}

// 1,0 is the class overridden by className
vtkTypeBool vtkObjectFactory::HasOverride(const char* className) VTK_FUTURE_CONST
{
  for (int i = 0; i < this->OverrideArrayLength; i++)
  {
    if (strcmp(this->OverrideClassNames[i], className) == 0)
    {
      return 1;
    }
  }
  return 0;
}

// 1,0 is the class overridden by className/subclassName pair
vtkTypeBool vtkObjectFactory::HasOverride(
  const char* className, const char* subclassName) VTK_FUTURE_CONST
{
  for (int i = 0; i < this->OverrideArrayLength; i++)
  {
    if (strcmp(this->OverrideClassNames[i], className) == 0)
    {
      if (strcmp(this->OverrideArray[i].OverrideWithName, subclassName) == 0)
      {
        return 1;
      }
    }
  }
  return 0;
}

vtkObjectFactoryCollection* vtkObjectFactory::GetRegisteredFactories()
{
  if (!vtkObjectFactory::RegisteredFactories)
  {
    vtkObjectFactory::Init();
  }

  return vtkObjectFactory::RegisteredFactories;
}

// 1,0 is the className overridden by any registered factories
vtkTypeBool vtkObjectFactory::HasOverrideAny(const char* className)
{
  vtkObjectFactory* factory;
  vtkCollectionSimpleIterator osit;
  for (vtkObjectFactory::RegisteredFactories->InitTraversal(osit);
       (factory = vtkObjectFactory::RegisteredFactories->GetNextObjectFactory(osit));)
  {
    if (factory->HasOverride(className))
    {
      return 1;
    }
  }
  return 0;
}

// collect up information about current registered factories
void vtkObjectFactory::GetOverrideInformation(
  const char* name, vtkOverrideInformationCollection* ret)
{
  // create the collection to return
  vtkOverrideInformation* overInfo; // info object pointer
  vtkObjectFactory* factory;        // factory pointer for traversal
  vtkCollectionSimpleIterator osit;
  vtkObjectFactory::RegisteredFactories->InitTraversal(osit);
  for (; (factory = vtkObjectFactory::RegisteredFactories->GetNextObjectFactory(osit));)
  {
    for (int i = 0; i < factory->OverrideArrayLength; i++)
    {
      if (strcmp(name, factory->OverrideClassNames[i]) == 0)
      {
        // Create a new override info class
        overInfo = vtkOverrideInformation::New();
        // Set the class name
        overInfo->SetClassOverrideName(factory->OverrideClassNames[i]);
        // Set the override class name
        overInfo->SetClassOverrideWithName(factory->OverrideArray[i].OverrideWithName);
        // Set the Description for the override
        overInfo->SetDescription(factory->OverrideArray[i].Description);
        // Set the factory for the override
        overInfo->SetObjectFactory(factory);
        // add the item to the collection
        ret->AddItem(overInfo);
        overInfo->Delete();
      }
    }
  }
}

// set enable flag for all registered factories for the given className
void vtkObjectFactory::SetAllEnableFlags(vtkTypeBool flag, const char* className)
{
  vtkObjectFactory* factory;
  vtkCollectionSimpleIterator osit;
  for (vtkObjectFactory::RegisteredFactories->InitTraversal(osit);
       (factory = vtkObjectFactory::RegisteredFactories->GetNextObjectFactory(osit));)
  {
    factory->SetEnableFlag(flag, className, nullptr);
  }
}

// set enable flag for the first factory that that
// has an override for className/subclassName pair
void vtkObjectFactory::SetAllEnableFlags(
  vtkTypeBool flag, const char* className, const char* subclassName)
{
  vtkObjectFactory* factory;
  vtkCollectionSimpleIterator osit;
  for (vtkObjectFactory::RegisteredFactories->InitTraversal(osit);
       (factory = vtkObjectFactory::RegisteredFactories->GetNextObjectFactory(osit));)
  {
    factory->SetEnableFlag(flag, className, subclassName);
  }
}

void vtkObjectFactory::CreateAllInstance(const char* vtkclassname, vtkCollection* retList)
{
  vtkObjectFactory* f;
  vtkObjectFactoryCollection* collection = vtkObjectFactory::GetRegisteredFactories();
  vtkCollectionSimpleIterator osit;
  for (collection->InitTraversal(osit); (f = collection->GetNextObjectFactory(osit));)
  {
    vtkObject* o = f->CreateObject(vtkclassname);
    if (o)
    {
      retList->AddItem(o);
      o->Delete();
    }
  }
}
VTK_ABI_NAMESPACE_END
