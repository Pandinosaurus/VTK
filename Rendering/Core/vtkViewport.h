// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
/**
 * @class   vtkViewport
 * @brief   abstract specification for Viewports
 *
 * vtkViewport provides an abstract specification for Viewports. A Viewport
 * is an object that controls the rendering process for objects. Rendering
 * is the process of converting geometry, a specification for lights, and
 * a camera view into an image. vtkViewport also performs coordinate
 * transformation between world coordinates, view coordinates (the computer
 * graphics rendering coordinate system), and display coordinates (the
 * actual screen coordinates on the display device). Certain advanced
 * rendering features such as two-sided lighting can also be controlled.
 *
 * @sa
 * vtkWindow vtkRenderer
 */

#ifndef vtkViewport_h
#define vtkViewport_h

#include "vtkDeprecation.h" // For VTK_DEPRECATED_IN_9_5_0
#include "vtkObject.h"
#include "vtkRenderingCoreModule.h" // For export macro
#include "vtkWrappingHints.h"       // For VTK_MARSHALAUTO

#include "vtkSelection.h"    // Needed for selection
#include "vtkSmartPointer.h" // Needed for assigning default nullptr value

#include <array> // To store matrices

VTK_ABI_NAMESPACE_BEGIN
class vtkActor2DCollection;
class vtkAssemblyPath;
class vtkProp;
class vtkPropCollection;
class vtkWindow;

class VTKRENDERINGCORE_EXPORT VTK_MARSHALAUTO vtkViewport : public vtkObject
{
public:
  vtkTypeMacro(vtkViewport, vtkObject);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Add a prop to the list of props. Does nothing if the prop is
   * nullptr or already present. Prop is the superclass of all actors, volumes,
   * 2D actors, composite props etc.
   */
  VTK_MARSHALEXCLUDE(VTK_MARSHAL_EXCLUDE_REASON_IS_INTERNAL)
  void AddViewProp(vtkProp*);

  /**
   * Return any props in this viewport.
   */
  vtkPropCollection* GetViewProps() { return this->Props; }

  /**
   * Query if a prop is in the list of props. Returns false for nullptr.
   */
  vtkTypeBool HasViewProp(vtkProp*);

  /**
   * Remove a prop from the list of props. Does nothing if the prop
   * is nullptr or not already present.
   */
  VTK_MARSHALEXCLUDE(VTK_MARSHAL_EXCLUDE_REASON_IS_INTERNAL)
  void RemoveViewProp(vtkProp*);

  /**
   * Remove all props from the list of props.
   */
  VTK_MARSHALEXCLUDE(VTK_MARSHAL_EXCLUDE_REASON_IS_INTERNAL)
  void RemoveAllViewProps();

  ///@{
  /**
   * Add the given prop to the renderer. This is a synonym for AddViewProp.
   */
  VTK_DEPRECATED_IN_9_5_0("Use AddViewProp() instead.")
  VTK_MARSHALEXCLUDE(VTK_MARSHAL_EXCLUDE_REASON_IS_INTERNAL)
  void AddActor2D(vtkProp* p);

  /**
   * Remove the given prop from the renderer. This is a synonym for RemoveViewProp.
   */
  VTK_DEPRECATED_IN_9_5_0("Use RemoveViewProp() instead.")
  VTK_MARSHALEXCLUDE(VTK_MARSHAL_EXCLUDE_REASON_IS_INTERNAL)
  void RemoveActor2D(vtkProp* p);

  /**
   * Loops through the props and returns a collection of those
   * that are vtkActor2D (or one of its subclasses).
   */
  VTK_MARSHALEXCLUDE(VTK_MARSHAL_EXCLUDE_REASON_IS_INTERNAL)
  vtkActor2DCollection* GetActors2D();
  ///@}

  ///@{
  /**
   * Set/Get the background color of the rendering screen using an rgb color
   * specification.
   */
  vtkSetVector3Macro(Background, double);
  vtkGetVector3Macro(Background, double);
  ///@}

  ///@{
  /**
   * Set/Get the second background color of the rendering screen
   * for gradient backgrounds using an rgb color specification.
   */
  vtkSetVector3Macro(Background2, double);
  vtkGetVector3Macro(Background2, double);
  ///@}
  //

  ///@{
  /**
   * Set/Get the alpha value used to fill the background with.
   * By default, this is set to 0.0.
   */
  vtkSetClampMacro(BackgroundAlpha, double, 0.0, 1.0);
  vtkGetMacro(BackgroundAlpha, double);
  ///@}

  ///@{
  /**
   * Set/Get whether this viewport should have a gradient background
   * using the Background (bottom) and Background2 (top) colors.
   * Default is off.
   */
  vtkSetMacro(GradientBackground, bool);
  vtkGetMacro(GradientBackground, bool);
  vtkBooleanMacro(GradientBackground, bool);
  ///@}

  ///@{
  /**
   * Set/Get whether this viewport should use dithering to reduce
   * color banding when using gradient backgrounds.
   * By default, this feature is enabled.
   */
  vtkSetMacro(DitherGradient, bool);
  vtkGetMacro(DitherGradient, bool);
  vtkBooleanMacro(DitherGradient, bool);
  ///@}

  enum class GradientModes : int
  {
    // Background color is used at the bottom, Background2 color is used at the top.
    VTK_GRADIENT_VERTICAL,
    // Background color on the left, Background2 color on the right.
    VTK_GRADIENT_HORIZONTAL,
    // Background color in the center, Background2 color on and beyond the ellipse edge.
    // Ellipse touches all sides of the viewport. The ellipse is a circle for viewports with equal
    // width and height.
    VTK_GRADIENT_RADIAL_VIEWPORT_FARTHEST_SIDE,
    // Background color in the center, Background2 color on and beyond the ellipse edge.
    // Ellipse touches all corners of the viewport. The ellipse is a circle for viewports with equal
    // width and height.
    VTK_GRADIENT_RADIAL_VIEWPORT_FARTHEST_CORNER,
  };

  ///@{
  /**
   * Specify the direction of the gradient background.
   * All modes smoothly interpolate the color from `Background` to `Background2`
   * @sa vtkViewport::GradientModes
   */
  vtkSetEnumMacro(GradientMode, GradientModes);
  vtkGetEnumMacro(GradientMode, GradientModes);
  ///@}

  ///@{
  /**
   * Set the aspect ratio of the rendered image. This is computed
   * automatically and should not be set by the user.
   */
  vtkSetVector2Macro(Aspect, double);
  vtkGetVectorMacro(Aspect, double, 2);
  virtual void ComputeAspect();
  ///@}

  ///@{
  /**
   * Set the aspect ratio of a pixel in the rendered image.
   * This factor permits the image to rendered anisotropically
   * (i.e., stretched in one direction or the other).
   */
  vtkSetVector2Macro(PixelAspect, double);
  vtkGetVectorMacro(PixelAspect, double, 2);
  ///@}

  ///@{
  /**
   * Specify the viewport for the Viewport to draw in the rendering window.
   * Coordinates are expressed as (xmin,ymin,xmax,ymax), where each
   * coordinate is 0 <= coordinate <= 1.0.
   */
  vtkSetVector4Macro(Viewport, double);
  vtkGetVectorMacro(Viewport, double, 4);
  ///@}

  ///@{
  /**
   * Set/get a point location in display (or screen) coordinates.
   * The lower left corner of the window is the origin and y increases
   * as you go up the screen.
   */
  vtkSetVector3Macro(DisplayPoint, double);
  vtkGetVectorMacro(DisplayPoint, double, 3);
  ///@}

  ///@{
  /**
   * Specify a point location in view coordinates. The origin is in the
   * middle of the viewport and it extends from -1 to 1 in all three
   * dimensions.
   */
  vtkSetVector3Macro(ViewPoint, double);
  vtkGetVectorMacro(ViewPoint, double, 3);
  ///@}

  ///@{
  /**
   * Specify a point location in world coordinates. This method takes
   * homogeneous coordinates.
   */
  vtkSetVector4Macro(WorldPoint, double);
  vtkGetVectorMacro(WorldPoint, double, 4);
  ///@}

  /**
   * Return the center of this viewport in display coordinates.
   */
  virtual double* GetCenter() VTK_SIZEHINT(2);

  /**
   * Is a given display point in this Viewport's viewport.
   */
  virtual vtkTypeBool IsInViewport(int x, int y);

  /**
   * Return the vtkWindow that owns this vtkViewport.
   */
  virtual vtkWindow* GetVTKWindow() = 0;

  /**
   * Convert display coordinates to view coordinates.
   */
  virtual void DisplayToView(); // these get modified in subclasses

  /**
   * Convert view coordinates to display coordinates.
   */
  virtual void ViewToDisplay(); // to handle stereo rendering

  /**
   * Convert world point coordinates to view coordinates.
   */
  virtual void WorldToView();

  /**
   * Convert view point coordinates to world coordinates.
   */
  virtual void ViewToWorld();

  /**
   * Convert display (or screen) coordinates to world coordinates.
   */
  void DisplayToWorld()
  {
    this->DisplayToView();
    this->ViewToWorld();
  }

  /**
   * Convert world point coordinates to display (or screen) coordinates.
   */
  void WorldToDisplay()
  {
    this->WorldToView();
    this->ViewToDisplay();
  }

  /**
   * Convert world point coordinates to display (or screen) coordinates.
   */
  void WorldToDisplay(double& x, double& y, double& z)
  {
    this->WorldToView(x, y, z);
    this->ViewToDisplay(x, y, z);
  }

  ///@{
  /**
   * These methods map from one coordinate system to another.
   * They are primarily used by the vtkCoordinate object and
   * are often strung together. These methods return valid information
   * only if the window has been realized (e.g., GetSize() returns
   * something other than (0,0)).
   */
  virtual void LocalDisplayToDisplay(double& x, double& y);
  virtual void DisplayToNormalizedDisplay(double& u, double& v);
  virtual void NormalizedDisplayToViewport(double& x, double& y);
  virtual void ViewportToNormalizedViewport(double& u, double& v);
  virtual void NormalizedViewportToView(double& x, double& y, double& z);
  virtual void ViewToPose(double&, double&, double&) {}
  virtual void PoseToWorld(double&, double&, double&) {}
  virtual void DisplayToLocalDisplay(double& x, double& y);
  virtual void NormalizedDisplayToDisplay(double& u, double& v);
  virtual void ViewportToNormalizedDisplay(double& x, double& y);
  virtual void NormalizedViewportToViewport(double& u, double& v);
  virtual void ViewToNormalizedViewport(double& x, double& y, double& z);
  virtual void PoseToView(double&, double&, double&) {}
  virtual void WorldToPose(double&, double&, double&) {}
  virtual void ViewToWorld(double&, double&, double&) {}
  virtual void WorldToView(double&, double&, double&) {}
  virtual void ViewToDisplay(double& x, double& y, double& z);
  ///@}

  ///@{
  /**
   * Get the size and origin of the viewport in display coordinates. Note:
   * if the window has not yet been realized, GetSize() and GetOrigin()
   * return (0,0).
   */
  virtual int* GetSize() VTK_SIZEHINT(2);
  virtual int* GetOrigin() VTK_SIZEHINT(2);
  void GetTiledSize(int* width, int* height);
  virtual void GetTiledSizeAndOrigin(int* width, int* height, int* lowerLeftX, int* lowerLeftY);
  ///@}

  // The following methods describe the public pick interface for picking
  // Props in a viewport without/with setting fieldAssociation and selection.

  /**
   * Return the Prop that has the highest z value at the given x, y position
   * in the viewport.  Basically, the top most prop that renders the pixel at
   * selectionX, selectionY will be returned.  If no Props are there NULL is
   * returned.  This method selects from the Viewports Prop list.
   */
  virtual vtkAssemblyPath* PickProp(double selectionX, double selectionY) = 0;

  /**
   * Return the Prop that has the highest z value at the given x1, y1
   * and x2,y2 positions in the viewport.  Basically, the top most prop that
   * renders the pixel at selectionX1, selectionY1, selectionX2, selectionY2
   * will be returned.  If no Props are there NULL is returned.  This method
   * selects from the Viewports Prop list.
   */
  virtual vtkAssemblyPath* PickProp(
    double selectionX1, double selectionY1, double selectionX2, double selectionY2) = 0;

  /**
   * Same as PickProp with two arguments, but selects from the given
   * collection of Props instead of the Renderers props.  Make sure
   * the Props in the collection are in this renderer.
   */
  vtkAssemblyPath* PickPropFrom(double selectionX, double selectionY, vtkPropCollection*);

  /**
   * Same as PickProp with four arguments, but selects from the given
   * collection of Props instead of the Renderers props.  Make sure
   * the Props in the collection are in this renderer.
   */
  vtkAssemblyPath* PickPropFrom(double selectionX1, double selectionY1, double selectionX2,
    double selectionY2, vtkPropCollection*);

  /**
   * Return the Prop that has the highest z value at the given x, y position
   * in the viewport.  Basically, the top most prop that renders the pixel at
   * selectionX, selectionY will be returned.  If no Props are there, NULL is
   * returned.  This method selects from the Viewports Prop list. Additionally,
   * you can set the field association of the hardware selector used internally,
   * and get its selection result by passing a non-null vtkSmartPointer<vtkSelection>.
   */
  virtual vtkAssemblyPath* PickProp(double selectionX, double selectionY, int fieldAssociation,
    vtkSmartPointer<vtkSelection> selection) = 0;

  /**
   * Return the Prop that has the highest z value at the given x1, y1
   * and x2,y2 positions in the viewport.  Basically, the top most prop that
   * renders the pixel at selectionX1, selectionY1, selectionX2, selectionY2
   * will be returned.  If no Props are there, NULL is returned.  This method
   * selects from the Viewports Prop list. Additionally, you can set the field
   * association of the hardware selector used internally, and get its selection
   * result by passing a non-null vtkSmartPointer<vtkSelection>.
   */
  virtual vtkAssemblyPath* PickProp(double selectionX1, double selectionY1, double selectionX2,
    double selectionY2, int fieldAssociation, vtkSmartPointer<vtkSelection> selection) = 0;

  /**
   * Same as PickProp with two arguments, but selects from the given
   * collection of Props instead of the Renderers props.  Make sure
   * the Props in the collection are in this renderer. Additionally, you can set
   * the field association of the hardware selector used internally, and get its
   * selection result by passing a non-null vtkSmartPointer<vtkSelection>.
   */
  vtkAssemblyPath* PickPropFrom(double selectionX, double selectionY, vtkPropCollection*,
    int fieldAssociation, vtkSmartPointer<vtkSelection> selection);

  /**
   * Same as PickProp with four arguments, but selects from the given
   * collection of Props instead of the Renderers props.  Make sure
   * the Props in the collection are in this renderer. Additionally, you can set
   * the field association of the hardware selector used internally, and get its
   * selection result by passing a non-null vtkSmartPointer<vtkSelection>.
   */
  vtkAssemblyPath* PickPropFrom(double selectionX1, double selectionY1, double selectionX2,
    double selectionY2, vtkPropCollection*, int fieldAssociation,
    vtkSmartPointer<vtkSelection> selection);

  ///@{
  /**
   * Methods used to return the pick (x,y) in local display coordinates (i.e.,
   * it's that same as selectionX and selectionY).
   */
  double GetPickX() const { return (this->PickX1 + this->PickX2) * 0.5; }
  double GetPickY() const { return (this->PickY1 + this->PickY2) * 0.5; }
  double GetPickWidth() const { return this->PickX2 - this->PickX1 + 1; }
  double GetPickHeight() const { return this->PickY2 - this->PickY1 + 1; }
  double GetPickX1() const { return this->PickX1; }
  double GetPickY1() const { return this->PickY1; }
  double GetPickX2() const { return this->PickX2; }
  double GetPickY2() const { return this->PickY2; }
  VTK_MARSHALEXCLUDE(VTK_MARSHAL_EXCLUDE_REASON_IS_INTERNAL)
  vtkGetObjectMacro(PickResultProps, vtkPropCollection);
  ///@}

  /**
   * Return the Z value for the last picked Prop.
   */
  virtual double GetPickedZ() { return this->PickedZ; }

  ///@{
  /**
   * Set/Get the constant environmental color using an rgb color specification.
   * Note this is currently ignored outside of RayTracing.
   */
  vtkSetVector3Macro(EnvironmentalBG, double);
  vtkGetVector3Macro(EnvironmentalBG, double);
  ///@}

  ///@{
  /**
   * Set/Get the second environmental gradient color using an rgb color specification.
   * Note this is currently ignored outside of RayTracing.
   */
  vtkSetVector3Macro(EnvironmentalBG2, double);
  vtkGetVector3Macro(EnvironmentalBG2, double);
  ///@}
  ///@{
  /**
   * Set/Get whether this viewport should enable the gradient environment
   * using the EnvironmentalBG (bottom) and EnvironmentalBG2 (top) colors.
   * Note this is currently ignored outside of RayTracing.
   * Default is off.
   */
  vtkSetMacro(GradientEnvironmentalBG, bool);
  vtkGetMacro(GradientEnvironmentalBG, bool);
  vtkBooleanMacro(GradientEnvironmentalBG, bool);
  ///@}

protected:
  // Create a vtkViewport with a black background, a white ambient light,
  // two-sided lighting turned on, a viewport of (0,0,1,1), and back face
  // culling turned off.
  vtkViewport();
  ~vtkViewport() override;

  // Ivars for picking
  // Store a picked Prop (contained in an assembly path)
  vtkAssemblyPath* PickedProp;
  vtkPropCollection* PickFromProps;
  vtkPropCollection* PickResultProps;
  double PickX1;
  double PickY1;
  double PickX2;
  double PickY2;
  double PickedZ;
  // End Ivars for picking

  vtkPropCollection* Props;
  vtkActor2DCollection* Actors2D;
  vtkWindow* VTKWindow;
  double Background[3];
  double Background2[3];
  double BackgroundAlpha;
  double Viewport[4];
  double Aspect[2];
  double PixelAspect[2];
  double Center[2];
  bool GradientBackground;
  bool DitherGradient;
  GradientModes GradientMode = GradientModes::VTK_GRADIENT_VERTICAL;

  double EnvironmentalBG[3];
  double EnvironmentalBG2[3];
  bool GradientEnvironmentalBG;

  int Size[2];
  int Origin[2];
  double DisplayPoint[3];
  double ViewPoint[3];
  double WorldPoint[4];

private:
  std::array<int, 2> LastComputeAspectSize;
  std::array<double, 4> LastComputeAspectVPort;
  std::array<double, 2> LastComputeAspectPixelAspect;

  vtkViewport(const vtkViewport&) = delete;
  void operator=(const vtkViewport&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif
