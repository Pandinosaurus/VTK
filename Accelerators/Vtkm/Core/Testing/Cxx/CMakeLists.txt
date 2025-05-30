# We are splitting the tests into two executables to workaround an issue in
# cuda. With all the tests in the same executable several tests are failing
# in cuda. We have not identified the root cause of the problem yet.
vtk_add_test_cxx(vtkAcceleratorsVTKmCoreCxxTests tests
  TestVTKMDataArray.cxx,NO_VALID
  TestVTKMImplicitDataArray.cxx,NO_VALID
  )

if (TARGET viskores::cuda OR TARGET viskores::kokkos_cuda)
  foreach(src IN LISTS tests)
    string(REPLACE "," ";" src ${src})
    list(GET src 0 src)

    set_source_files_properties(${src} PROPERTIES LANGUAGE CUDA)
  endforeach()

  #the tests aren't scoped as a child directory of vtkAcceleratorsVTKmCore
  #so we need to redo this logic
  viskores_get_cuda_flags(CMAKE_CUDA_FLAGS)

  # Temporarily suppress "has address taken but no possible call to it" warnings,
  # until we figure out its implications.
  # We are disabling all warnings as nvlink has no known way to suppress
  # individual warning types.
  string(APPEND CMAKE_CUDA_FLAGS " -Xnvlink -w")
endif()

vtk_test_cxx_executable(vtkAcceleratorsVTKmCoreCxxTests tests)

if (TARGET viskores::cuda)
  # When cuda is enabled VTK::AcceleratorsVTKmCore is built statically but with fpic
  # enabled so the tests are also built with fpic enabled
  set_target_properties(vtkAcceleratorsVTKmCoreCxxTests PROPERTIES
    CUDA_ARCHITECTURES OFF
    POSITION_INDEPENDENT_CODE ON
    )
endif()
