#!/usr/bin/env python
from vtkmodules.vtkCommonCore import (
    vtkFloatArray,
    vtkPoints,
)
from vtkmodules.vtkCommonDataModel import (
    vtkCellArray,
    vtkDataObject,
    vtkPolyData,
)
from vtkmodules.vtkFiltersCore import vtkGenerateIds
from vtkmodules.vtkFiltersModeling import vtkBandedPolyDataContourFilter
from vtkmodules.vtkRenderingCore import (
    vtkActor,
    vtkActor2D,
    vtkPolyDataMapper,
    vtkRenderWindow,
    vtkRenderWindowInteractor,
    vtkRenderer,
)
from vtkmodules.vtkRenderingLabel import vtkLabeledDataMapper
import vtkmodules.vtkInteractionStyle
import vtkmodules.vtkRenderingFreeType
import vtkmodules.vtkRenderingOpenGL2
from vtkmodules.util.misc import vtkGetDataRoot
VTK_DATA_ROOT = vtkGetDataRoot()

# Manually create cells of various types: vertex, polyvertex, line,
# polyline, triangle, quad, pentagon, and triangle strip.
pts = vtkPoints()
pts.InsertPoint(0,0,0,0)
pts.InsertPoint(1,0,1,0)
pts.InsertPoint(2,0,2,0)
pts.InsertPoint(3,1,0,0)
pts.InsertPoint(4,1,1,0)
pts.InsertPoint(5,1,2,0)
pts.InsertPoint(6,2,0,0)
pts.InsertPoint(7,2,2,0)
pts.InsertPoint(8,3,0,0)
pts.InsertPoint(9,3,1,0)
pts.InsertPoint(10,3,2,0)
pts.InsertPoint(11,4,0,0)
pts.InsertPoint(12,6,0,0)
pts.InsertPoint(13,5,2,0)
pts.InsertPoint(14,7,0,0)
pts.InsertPoint(15,9,0,0)
pts.InsertPoint(16,7,2,0)
pts.InsertPoint(17,9,2,0)
pts.InsertPoint(18,10,0,0)
pts.InsertPoint(19,12,0,0)
pts.InsertPoint(20,10,1,0)
pts.InsertPoint(21,12,1,0)
pts.InsertPoint(22,10,2,0)
pts.InsertPoint(23,12,2,0)
pts.InsertPoint(24,10,3,0)
pts.InsertPoint(25,12,3,0)
verts = vtkCellArray()
verts.InsertNextCell(1)
verts.InsertCellPoint(0)
verts.InsertNextCell(1)
verts.InsertCellPoint(1)
verts.InsertNextCell(1)
verts.InsertCellPoint(2)
verts.InsertNextCell(3)
verts.InsertCellPoint(3)
verts.InsertCellPoint(4)
verts.InsertCellPoint(5)
lines = vtkCellArray()
lines.InsertNextCell(2)
lines.InsertCellPoint(6)
lines.InsertCellPoint(7)
lines.InsertNextCell(3)
lines.InsertCellPoint(8)
lines.InsertCellPoint(9)
lines.InsertCellPoint(10)
polys = vtkCellArray()
polys.InsertNextCell(4)
polys.InsertCellPoint(14)
polys.InsertCellPoint(15)
polys.InsertCellPoint(17)
polys.InsertCellPoint(16)
polys.InsertNextCell(3)
polys.InsertCellPoint(11)
polys.InsertCellPoint(12)
polys.InsertCellPoint(13)
strips = vtkCellArray()
strips.InsertNextCell(8)
strips.InsertCellPoint(19)
strips.InsertCellPoint(18)
strips.InsertCellPoint(21)
strips.InsertCellPoint(20)
strips.InsertCellPoint(23)
strips.InsertCellPoint(22)
strips.InsertCellPoint(25)
strips.InsertCellPoint(24)
scalars = vtkFloatArray()
scalars.SetName("SomeScalars")
scalars.SetNumberOfTuples(26)
scalars.SetTuple1(0,0)
scalars.SetTuple1(1,50)
scalars.SetTuple1(2,100)
scalars.SetTuple1(3,0)
scalars.SetTuple1(4,50)
scalars.SetTuple1(5,100)
scalars.SetTuple1(6,10)
scalars.SetTuple1(7,90)
scalars.SetTuple1(8,10)
scalars.SetTuple1(9,50)
scalars.SetTuple1(10,90)
scalars.SetTuple1(11,10)
scalars.SetTuple1(12,40)
scalars.SetTuple1(13,100)
scalars.SetTuple1(14,0)
scalars.SetTuple1(15,60)
scalars.SetTuple1(16,40)
scalars.SetTuple1(17,100)
scalars.SetTuple1(18,0)
scalars.SetTuple1(19,25)
scalars.SetTuple1(20,25)
scalars.SetTuple1(21,50)
scalars.SetTuple1(22,50)
scalars.SetTuple1(23,75)
scalars.SetTuple1(24,75)
scalars.SetTuple1(25,100)
polyData = vtkPolyData()
polyData.SetPoints(pts)
polyData.SetVerts(verts)
polyData.SetLines(lines)
polyData.SetPolys(polys)
polyData.SetStrips(strips)
polyData.GetPointData().AddArray(scalars)
bf = vtkBandedPolyDataContourFilter()
bf.SetInputArrayToProcess(0, 0, 0, vtkDataObject.FIELD_ASSOCIATION_POINTS, "SomeScalars")
bf.SetInputData(polyData)
bf.GenerateValues(3,25,75)
mapper = vtkPolyDataMapper()
mapper.SetInputConnection(bf.GetOutputPort())
mapper.SetScalarModeToUseCellData()
mapper.SetScalarRange(0,4)
actor = vtkActor()
actor.SetMapper(mapper)
ids = vtkGenerateIds()
ids.SetInputConnection(bf.GetOutputPort())
ids.PointIdsOn()
ids.CellIdsOn()
ids.FieldDataOn()
ldm = vtkLabeledDataMapper()
ldm.SetInputConnection(ids.GetOutputPort())
#  ldm SetLabelFormat "%g"
ldm.SetLabelModeToLabelFieldData()
pointLabels = vtkActor2D()
pointLabels.SetMapper(ldm)
# Create the RenderWindow, Renderer and both Actors
#
ren1 = vtkRenderer()
renWin = vtkRenderWindow()
renWin.AddRenderer(ren1)
iren = vtkRenderWindowInteractor()
iren.SetRenderWindow(renWin)
# Add the actors to the renderer, set the background and size
#
ren1.AddActor(actor)
#ren1 AddViewProp pointLabels #for debugging only
ren1.SetBackground(0,0,0)
renWin.SetSize(300,80)
renWin.Render()
ren1.GetActiveCamera().Zoom(3)
renWin.Render()
iren.Initialize()
# render the image
#
# prevent the tk window from showing up then start the event loop
# --- end of script --
