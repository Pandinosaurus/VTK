vtk_add_test_cxx(vtkParallelCoreCxxTests tests
  NO_DATA NO_VALID NO_OUTPUT
  TestFieldDataSerialization.cxx
  TestThreadedCallbackQueue.cxx
  TestThreadedTaskQueue.cxx
  )
vtk_test_cxx_executable(vtkParallelCoreCxxTests tests)

if (Python3_EXECUTABLE AND
    NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
  vtk_module_test_executable(
    vtkParallelCore-TestSocketCommunicator TestSocketCommunicator.cxx)

  add_test(NAME vtkParallelCore-TestSocketCommunicator
           COMMAND ${Python3_EXECUTABLE}
                   ${VTK_SOURCE_DIR}/CMake/vtkTestDriver.py
                   # indicate that the processes are not run sequentially,
                   --process
                     $<TARGET_FILE:vtkParallelCore-TestSocketCommunicator>
                     -T ${_vtk_build_TEST_OUTPUT_DIRECTORY}
                     --server
                   --process
                     $<TARGET_FILE:vtkParallelCore-TestSocketCommunicator>
                     -T ${_vtk_build_TEST_OUTPUT_DIRECTORY}
          )
endif()
