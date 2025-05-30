#!/usr/bin/env python
from vtkmodules.vtkCommonCore import (
    vtkLookupTable,
    vtkPoints,
)
from vtkmodules.vtkCommonTransforms import vtkThinPlateSplineTransform
from vtkmodules.vtkFiltersHybrid import (
    vtkGridTransform,
    vtkTransformToGrid,
)
from vtkmodules.vtkIOImage import vtkBMPReader
from vtkmodules.vtkImagingCore import (
    vtkImageBlend,
    vtkImageMapToColors,
    vtkImageReslice,
)
from vtkmodules.vtkImagingSources import vtkImageGridSource
from vtkmodules.vtkInteractionImage import vtkImageViewer
import vtkmodules.vtkInteractionStyle
import vtkmodules.vtkRenderingFreeType
import vtkmodules.vtkRenderingOpenGL2
from vtkmodules.util.misc import vtkGetDataRoot
VTK_DATA_ROOT = vtkGetDataRoot()

# first, create an image to warp
imageGrid = vtkImageGridSource()
imageGrid.SetGridSpacing(16,16,0)
imageGrid.SetGridOrigin(0,0,0)
imageGrid.SetDataExtent(0,255,0,255,0,0)
imageGrid.SetDataScalarTypeToUnsignedChar()
table = vtkLookupTable()
table.SetTableRange(0,1)
table.SetValueRange(1.0,0.0)
table.SetSaturationRange(0.0,0.0)
table.SetHueRange(0.0,0.0)
table.SetAlphaRange(0.0,1.0)
table.Build()
alpha = vtkImageMapToColors()
alpha.SetInputConnection(imageGrid.GetOutputPort())
alpha.SetLookupTable(table)
reader1 = vtkBMPReader()
reader1.SetFileName(VTK_DATA_ROOT + "/Data/masonry.bmp")
blend = vtkImageBlend()
blend.AddInputConnection(reader1.GetOutputPort())
blend.AddInputConnection(alpha.GetOutputPort())
# next, create a ThinPlateSpline transform
p1 = vtkPoints()
p1.SetNumberOfPoints(8)
p1.SetPoint(0,0,0,0)
p1.SetPoint(1,0,255,0)
p1.SetPoint(2,255,0,0)
p1.SetPoint(3,255,255,0)
p1.SetPoint(4,96,96,0)
p1.SetPoint(5,96,159,0)
p1.SetPoint(6,159,159,0)
p1.SetPoint(7,159,96,0)
p2 = vtkPoints()
p2.SetNumberOfPoints(8)
p2.SetPoint(0,0,0,0)
p2.SetPoint(1,0,255,0)
p2.SetPoint(2,255,0,0)
p2.SetPoint(3,255,255,0)
p2.SetPoint(4,96,159,0)
p2.SetPoint(5,159,159,0)
p2.SetPoint(6,159,96,0)
p2.SetPoint(7,96,96,0)
thinPlate = vtkThinPlateSplineTransform()
thinPlate.SetSourceLandmarks(p2)
thinPlate.SetTargetLandmarks(p1)
thinPlate.SetBasisToR2LogR()
# convert the thin plate spline into a grid
# for nearest neighbor interpolation, the grid should precisely
# overlay the image you want to warp
transformToGrid = vtkTransformToGrid()
transformToGrid.SetInput(thinPlate)
transformToGrid.SetGridSpacing(1,1,1)
transformToGrid.SetGridOrigin(0,0,0)
transformToGrid.SetGridExtent(0,255,0,255,0,0)
transformToGrid.Update()
transform = vtkGridTransform()
transform.SetDisplacementGridConnection(transformToGrid.GetOutputPort())
transform.SetInterpolationModeToNearestNeighbor()
# must lower the tolerance or it won't invert
transform.SetInverseTolerance(2.0)
# you must invert the transform before passing it to vtkImageReslice
transform.Inverse()
# apply the grid warp to the image
reslice = vtkImageReslice()
reslice.SetInputConnection(blend.GetOutputPort())
reslice.SetResliceTransform(transform)
reslice.SetInterpolationModeToLinear()
# set the window/level to 255.0/127.5 to view full range
viewer = vtkImageViewer()
viewer.SetInputConnection(reslice.GetOutputPort())
viewer.SetColorWindow(255.0)
viewer.SetColorLevel(127.5)
viewer.SetZSlice(0)
viewer.Render()
# --- end of script --
