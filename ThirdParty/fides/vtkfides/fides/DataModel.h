//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//
//============================================================================

#ifndef fides_datamodel_DataModel_H_
#define fides_datamodel_DataModel_H_

#include <fides/DataSource.h>

#include <viskores/cont/UnknownArrayHandle.h>

#include <fides_rapidjson.h>
// clang-format off
#include FIDES_RAPIDJSON(rapidjson/document.h)
// clang-format on

#include <string>
#include <unordered_map>
#include <vector>

namespace fides
{
namespace datamodel
{

using DataSourceType = fides::io::DataSource;
using DataSourcesType = std::unordered_map<std::string, std::shared_ptr<DataSourceType>>;

/// \brief Superclass for all data model classes.
///
/// Data model classes represent different structures that reside
/// in a Viskores dataset and that are mapped to different variables
/// read by data sources. This class provides common basic functionality
/// to all data model objects. These can be overriden by subclasses.
struct DataModelBase
{
  DataModelBase() = default;
  DataModelBase(const DataModelBase& other)
  {
    if (this != &other)
    {
      this->ObjectName = other.ObjectName;
      this->DataSourceName = other.DataSourceName;
      this->VariableName = other.VariableName;
      this->IsStatic = other.IsStatic;
    }
  }

  virtual ~DataModelBase() = default;

  /// Perform basic parsing of the JSON object, filling in
  /// common data members such as the data source and variable
  /// name.
  virtual void ProcessJSON(const rapidjson::Value& json, DataSourcesType& sources);

  std::string ObjectName = "";
  std::string DataSourceName = "";
  std::string VariableName = "";
  // Is the variable time dependent or static.
  bool IsStatic = false;

protected:
  std::string FindDataSource(const rapidjson::Value& dataModel, DataSourcesType& sources) const;

  // Data reading usually happens through this method, which works
  // with the data source. This also handles data caching for static
  // variables.
  std::vector<viskores::cont::UnknownArrayHandle> ReadSelf(
    const std::unordered_map<std::string, std::string>& paths,
    DataSourcesType& sources,
    const fides::metadata::MetaData& selections,
    fides::io::IsVector isItVector = fides::io::IsVector::Auto);

  std::vector<viskores::cont::UnknownArrayHandle> Cache;
};

/// Utility function that returns an ArrayHandle with the data from `uah`.
/// However, the returned ArrayHandle does not own data. The deleter of the Buffer is a no-op.
viskores::cont::UnknownArrayHandle make_ArrayHandleWithoutDataOwnership(
  const viskores::cont::UnknownArrayHandle& uah);

/// Convenience function that returns a vector of ownerless ArrayHandle objects for
/// each ArrayHandle in the input vector.
std::vector<viskores::cont::UnknownArrayHandle> make_ArrayHandlesWithoutDataOwnership(
  const std::vector<viskores::cont::UnknownArrayHandle>& uahs);

}
}

#endif
