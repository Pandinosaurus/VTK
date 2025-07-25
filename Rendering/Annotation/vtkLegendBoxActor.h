// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkLegendBoxActor
 * @brief   draw symbols with text
 *
 * vtkLegendBoxActor is used to associate a symbol with a text string.
 * The user specifies a vtkPolyData to use as the symbol, and a string
 * associated with the symbol. The actor can then be placed in the scene
 * in the same way that any other vtkActor2D can be used.
 *
 * To use this class, you must define the position of the legend box by using
 * the superclasses' vtkActor2D::Position coordinate and
 * Position2 coordinate. Then define the set of symbols and text strings that
 * make up the menu box. The font attributes of the entries can be set through
 * the vtkTextProperty associated to this actor. The class will
 * scale the symbols and text to fit in the legend box defined by
 * (Position,Position2). Optional features like turning on a border line and
 * setting the spacing between the border and the symbols/text can also be
 * set.
 *
 * @sa
 * vtkXYPlotActor vtkActor2D vtkGlyphSource2D
 */

#ifndef vtkLegendBoxActor_h
#define vtkLegendBoxActor_h

#include "vtkActor2D.h"
#include "vtkRenderingAnnotationModule.h" // For export macro
#include "vtkWrappingHints.h"             // For VTK_MARSHALAUTO

VTK_ABI_NAMESPACE_BEGIN
class vtkActor;
class vtkDoubleArray;
class vtkImageData;
class vtkPolyData;
class vtkPolyDataMapper2D;
class vtkPolyDataMapper;
class vtkPlaneSource;
class vtkTextMapper;
class vtkTextProperty;
class vtkTexturedActor2D;
class vtkTransform;
class vtkTransformPolyDataFilter;
class vtkProperty2D;

class VTKRENDERINGANNOTATION_EXPORT VTK_MARSHALAUTO vtkLegendBoxActor : public vtkActor2D
{
public:
  vtkTypeMacro(vtkLegendBoxActor, vtkActor2D);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Instantiate object with a rectangle in normaled view coordinates
   * of (0.2,0.85, 0.8, 0.95).
   */
  static vtkLegendBoxActor* New();

  /**
   * Specify the number of entries in the legend box.
   */
  void SetNumberOfEntries(int num);
  int GetNumberOfEntries() { return this->NumberOfEntries; }

  ///@{
  /**
   * Add an entry to the legend box. You must supply a vtkPolyData to be
   * used as a symbol (it can be NULL) and a text string (which also can
   * be NULL). The vtkPolyData is assumed to be defined in the x-y plane,
   * and the text is assumed to be a single line in height. Note that when
   * this method is invoked previous entries are deleted. Also supply a text
   * string and optionally a color. (If a color is not specified, then the
   * entry color is the same as this actor's color.) (Note: use the set
   * methods when you use SetNumberOfEntries().)
   */
  void SetEntry(int i, vtkPolyData* symbol, const char* string, double color[3]);
  void SetEntry(int i, vtkImageData* symbol, const char* string, double color[3]);
  void SetEntry(
    int i, vtkPolyData* symbol, vtkImageData* icon, const char* string, double color[3]);
  ///@}

  void SetEntrySymbol(int i, vtkPolyData* symbol);
  void SetEntryIcon(int i, vtkImageData* icon);
  void SetEntryString(int i, const char* string);
  void SetEntryColor(int i, double color[3]);
  void SetEntryColor(int i, double r, double g, double b);

  vtkPolyData* GetEntrySymbol(int i);
  vtkImageData* GetEntryIcon(int i);
  const char* GetEntryString(int i);
  double* GetEntryColor(int i) VTK_SIZEHINT(3);

  int GetNumberOfEntrySymbols() { return NumberOfEntries; }
  int GetNumberOfEntryColors() { return NumberOfEntries; }
  int GetNumberOfEntryStrings() { return NumberOfEntries; }
  int GetNumberOfEntryIcons() { return NumberOfEntries; }
  ///@{
  /**
   * Set/Get the text property.
   */
  virtual void SetEntryTextProperty(vtkTextProperty* p);
  vtkGetObjectMacro(EntryTextProperty, vtkTextProperty);
  ///@}

  ///@{
  /**
   * Set/Get the flag that controls whether a border will be drawn
   * around the legend box.
   */
  vtkSetMacro(Border, vtkTypeBool);
  vtkGetMacro(Border, vtkTypeBool);
  vtkBooleanMacro(Border, vtkTypeBool);
  ///@}

  ///@{
  /**
   * Set/Get the flag that controls whether the border and legend
   * placement is locked into the rectangle defined by (Position,Position2).
   * If off, then the legend box will adjust its size so that the border
   * fits nicely around the text and symbols. (The ivar is off by default.)
   * Note: the legend box is guaranteed to lie within the original border
   * definition.
   */
  vtkSetMacro(LockBorder, vtkTypeBool);
  vtkGetMacro(LockBorder, vtkTypeBool);
  vtkBooleanMacro(LockBorder, vtkTypeBool);
  ///@}

  ///@{
  /**
   * Set/Get the flag that controls whether a box will be drawn/filled
   * corresponding to the legend box.
   */
  vtkSetMacro(Box, vtkTypeBool);
  vtkGetMacro(Box, vtkTypeBool);
  vtkBooleanMacro(Box, vtkTypeBool);
  ///@}

  /**
   * Get the box vtkProperty2D.
   */
  vtkProperty2D* GetBoxProperty() { return this->BoxActor->GetProperty(); }

  ///@{
  /**
   * Set/Get the padding between the legend entries and the border. The value
   * is specified in pixels.
   */
  vtkSetClampMacro(Padding, int, 0, 50);
  vtkGetMacro(Padding, int);
  ///@}

  ///@{
  /**
   * Turn on/off flag to control whether the symbol's scalar data
   * is used to color the symbol. If off, the color of the
   * vtkLegendBoxActor is used.
   */
  vtkSetMacro(ScalarVisibility, vtkTypeBool);
  vtkGetMacro(ScalarVisibility, vtkTypeBool);
  vtkBooleanMacro(ScalarVisibility, vtkTypeBool);
  ///@}

  ///@{
  /**
   * Turn on/off background.
   */
  vtkSetMacro(UseBackground, vtkTypeBool);
  vtkGetMacro(UseBackground, vtkTypeBool);
  vtkBooleanMacro(UseBackground, vtkTypeBool);
  ///@}

  ///@{
  /**
   * Set/Get background color.
   * Default is: (0.3, 0.3, 0.3).
   */
  vtkSetVector3Macro(BackgroundColor, double);
  vtkGetVector3Macro(BackgroundColor, double);
  ///@}

  ///@{
  /**
   * Set/Get background opacity.
   * Default is: 1.0
   */
  vtkSetClampMacro(BackgroundOpacity, double, 0.0, 1.0);
  vtkGetMacro(BackgroundOpacity, double);
  ///@}

  /**
   * Shallow copy of this scaled text actor. Overloads the virtual
   * vtkProp method.
   */
  void ShallowCopy(vtkProp* prop) override;

  /**
   * WARNING: INTERNAL METHOD - NOT INTENDED FOR GENERAL USE
   * DO NOT USE THIS METHOD OUTSIDE OF THE RENDERING PROCESS.
   * Release any graphics resources that are being consumed by this actor.
   * The parameter window could be used to determine which graphic
   * resources to release.
   */
  void ReleaseGraphicsResources(vtkWindow*) override;

  ///@{
  /**
   * WARNING: INTERNAL METHOD - NOT INTENDED FOR GENERAL USE
   * DO NOT USE THIS METHOD OUTSIDE OF THE RENDERING PROCESS.
   * Draw the legend box to the screen.
   */
  int RenderOpaqueGeometry(vtkViewport* viewport) override;
  int RenderTranslucentPolygonalGeometry(vtkViewport*) override { return 0; }
  int RenderOverlay(vtkViewport* viewport) override;
  ///@}

  /**
   * Does this prop have some translucent polygonal geometry?
   */
  vtkTypeBool HasTranslucentPolygonalGeometry() override;

protected:
  vtkLegendBoxActor();
  ~vtkLegendBoxActor() override;

  void InitializeEntries();

  vtkPolyData createTexturedPlane();

  vtkTypeBool Border;
  vtkTypeBool Box;
  int Padding;
  vtkTypeBool LockBorder;
  vtkTypeBool ScalarVisibility;
  double BoxOpacity;

  // Internal actors, mappers, data to represent the legend
  int NumberOfEntries;
  int Size; // allocation size
  vtkDoubleArray* Colors;
  vtkTextMapper** TextMapper;
  vtkActor2D** TextActor;

  vtkPolyData** Symbol;
  vtkTransform** Transform;
  vtkTransformPolyDataFilter** SymbolTransform;
  vtkPolyDataMapper2D** SymbolMapper;
  vtkActor2D** SymbolActor;

  vtkPlaneSource** Icon;
  vtkTransform** IconTransform;
  vtkTransformPolyDataFilter** IconTransformFilter;
  vtkPolyDataMapper2D** IconMapper;
  vtkTexturedActor2D** IconActor;
  vtkImageData** IconImage;

  vtkPolyData* BorderPolyData;
  vtkPolyDataMapper2D* BorderMapper;
  vtkActor2D* BorderActor;
  vtkPolyData* BoxPolyData;
  vtkPolyDataMapper2D* BoxMapper;
  vtkActor2D* BoxActor;
  vtkTextProperty* EntryTextProperty;

  // Background plane.
  vtkTypeBool UseBackground;
  double BackgroundOpacity;
  double BackgroundColor[3];
  vtkPlaneSource* Background;

  // May use texture.
  vtkTexturedActor2D* BackgroundActor;
  vtkPolyDataMapper2D* BackgroundMapper;

  // Used to control whether the stuff is recomputed
  int LegendEntriesVisible;
  int CachedSize[2];
  vtkTimeStamp BuildTime;

private:
  vtkLegendBoxActor(const vtkLegendBoxActor&) = delete;
  void operator=(const vtkLegendBoxActor&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
