set(classes
  vtkPLagrangianParticleTracker
  vtkPStreamTracer)

# Overrides for object factory.
set(parallel_overrides
  LagrangianParticleTracker
  StreamTracer
  )

foreach (parallel_override IN LISTS parallel_overrides)
  vtk_object_factory_declare(
    BASE      vtk${parallel_override}
    OVERRIDE  vtkP${parallel_override})
endforeach ()

vtk_object_factory_configure(
  SOURCE_FILE vtk_object_factory_source
  HEADER_FILE vtk_object_factory_header
  EXPORT_MACRO "VTKFILTERSPARALLELFLOWPATHS_EXPORT")

vtk_module_add_module(VTK::FiltersParallelFlowPaths
  CLASSES ${classes}
  SOURCES ${vtk_object_factory_source}
  HEADERS ${vtk_object_factory_header})
vtk_add_test_mangling(VTK::FiltersParallelFlowPaths)
