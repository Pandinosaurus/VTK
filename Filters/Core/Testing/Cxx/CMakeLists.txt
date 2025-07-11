vtk_add_test_cxx(vtkFiltersCoreCxxTests tests
  Test3DLinearGridPlaneCutterCellData.cxx
  TestAppendArcLength.cxx,NO_VALID
  TestAppendDataSets.cxx,NO_VALID
  TestAppendFilter.cxx,NO_VALID
  TestAppendMolecule.cxx,NO_VALID
  TestAppendPartitionedDataSetCollection.cxx,NO_VALID
  TestAppendPolyData.cxx,NO_VALID
  TestAppendSelection.cxx,NO_VALID
  TestArrayCalculator.cxx,NO_VALID
  TestArrayRename.cxx,NO_VALID
  TestAssignAttribute.cxx,NO_VALID
  TestAttributeDataToTableFilter.cxx,NO_VALID
  TestAttributeDataToTableHyperTreeGrid.cxx,NO_VALID
  TestBinCellDataFilter.cxx,NO_VALID
  TestCategoricalPointDataToCellData.cxx,NO_VALID
  TestCategoricalResampleWithDataSet.cxx,NO_VALID
  TestCellCenters.cxx,NO_VALID
  TestCellDataToPointData.cxx,NO_VALID
  TestCenterOfMass.cxx,NO_VALID
  TestCleanPolyData.cxx,NO_VALID
  TestCleanPolyData2.cxx,NO_VALID
  TestCleanPolyDataWithGhostCells.cxx
  TestClipPolyData.cxx,NO_VALID
  TestCompositeDataProbeFilterWithHyperTreeGrid.cxx
  TestConnectivityFilter.cxx,NO_VALID
  TestCutter.cxx,NO_VALID
  TestDataObjectToPartitionedDataSetCollection.cxx,NO_VALID
  TestDecimatePolylineFilter.cxx
  TestDecimatePro.cxx,NO_VALID
  TestDecimateProDegenerateTriangles.cxx,NO_VALID
  TestDelaunay2D.cxx
  TestDelaunay2DBestFittingPlane.cxx,NO_VALID
  TestDelaunay2DConstrained.cxx,NO_VALID
  TestDelaunay2DFindTriangle.cxx,NO_VALID
  TestDelaunay2DMeshes.cxx,NO_VALID
  TestDelaunay3D.cxx,NO_VALID
  TestExplicitStructuredGridCrop.cxx
  TestExplicitStructuredGridToUnstructuredGrid.cxx
  TestExecutionTimer.cxx,NO_VALID
  TestExtractCells.cxx,NO_VALID
  TestExtractCellsAlongPolyLine.cxx,NO_VALID
  TestFeatureEdges.cxx,NO_VALID
  TestFieldDataToDataSetAttribute.cxx,NO_VALID
  TestFlyingEdges.cxx
  TestGenerateIdsHTG.cxx,NO_VALID,NO_OUTPUT
  TestGenerateRegionIds.cxx,NO_VALID
  TestGlyph3D.cxx
  TestGlyph3DFollowCamera.cxx,NO_VALID
  TestHedgeHog.cxx,NO_VALID
  TestHyperTreeGridProbeFilter.cxx
  TestResampleHyperTreeGridWithDataSet.cxx
  TestImageDataToExplicitStructuredGrid.cxx
  TestImplicitPolyDataDistance.cxx
  TestImplicitPolyDataDistanceCube.cxx,NO_VALID
  TestImplicitProjectOnPlaneDistance.cxx
  TestMaskPoints.cxx,NO_VALID
  TestMaskPointsModes.cxx
  TestNamedComponents.cxx,NO_VALID
  TestPartitionedDataSetCollectionConvertors.cxx,NO_VALID
  TestPlaneCutter.cxx,NO_VALID
  TestPointDataToCellData.cxx,NO_VALID
  TestPolyDataConnectivityFilter.cxx,NO_VALID
  TestPolyDataNormals.cxx,NO_VALID
  TestPolyDataTangents.cxx
  TestProbeFilter.cxx,NO_VALID
  TestProbeFilterImageInput.cxx
  TestProbeFilterOutputAttributes.cxx,NO_VALID
  TestQuadricDecimationMapPointData.cxx
  TestQuadricDecimationMaximumError.cxx,NO_DATA,NO_VALID,NO_OUTPUT
  TestQuadricDecimationRegularization.cxx
  TestQuadricDecimationSetPointAttributeArray.cxx
  TestResampleToImage.cxx,NO_VALID
  TestResampleToImage2D.cxx,NO_VALID
  TestResampleWithDataSet.cxx,
  TestResampleWithDataSet2.cxx
  TestResampleWithDataSet3.cxx
  TestRemoveDuplicatePolys.cxx,NO_VALID
  TestSmoothPolyDataFilter.cxx,NO_VALID
  TestSMPPipelineContour.cxx,NO_VALID
  TestSlicePlanePrecision.cxx,NO_VALID
  TestStaticCleanPolyData.cxx,NO_VALID
  TestStripper.cxx,NO_VALID
  TestStructuredGridAppend.cxx,NO_VALID
  TestSynchronizedTemplates2D.cxx,NO_VALID
  TestSynchronizedTemplates2DRGB.cxx,NO_DATA,NO_VALID
  TestThreshold.cxx,NO_VALID
  TestThresholdPoints.cxx,NO_VALID
  TestTransposeTable.cxx,NO_VALID
  TestTriangleMeshPointNormals.cxx
  TestTubeBender.cxx
  TestTubeFilter.cxx
  TestUnstructuredGridQuadricDecimation.cxx,NO_VALID
  TestUnstructuredGridToExplicitStructuredGrid.cxx
  TestUnstructuredGridToExplicitStructuredGridEmpty.cxx
  TestVaryRadiusTubeFilter.cxx
  UnitTestMaskPoints.cxx,NO_VALID
  UnitTestMergeFilter.cxx,NO_VALID
  TestContourImplicitArrays.cxx
  )

# This test fails on vtk-m due to bug #804 (vtk/vtkm)
# TODO: Remove from condition when fixed
if (NOT VTK_ENABLE_VISKORES_OVERRIDES)
  vtk_add_test_cxx(vtkFiltersCoreCxxTests no_viskores_tests
    TestThresholdComponents.cxx,NO_VALID)
endif()

set(all_tests
  ${tests}
  ${no_viskores_tests}
  )

if (TARGET VTK::ParallelMPI)
  set(vtkFiltersCoreCxxTests-MPI_NUMPROCS 2)
  vtk_add_test_mpi(vtkFiltersCoreCxxTests-MPI mpi_test
    TESTING_DATA
    TestAppendFilterDistributed.cxx,NO_VALID
    TestCleanPolyDataWithGhostCells.cxx
  )

  vtk_test_cxx_executable(vtkFiltersCoreCxxTests-MPI mpi_test)
endif()

vtk_test_cxx_executable(vtkFiltersCoreCxxTests all_tests)
