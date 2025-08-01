// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause

/**
 * @class   vtkContextTransform
 * @brief   all children of this item are transformed
 * by the vtkTransform2D of this item.
 *
 *
 * This class can be used to transform all child items of this class. The
 * default transform is the identity.
 */

#ifndef vtkContextTransform_h
#define vtkContextTransform_h

#include "vtkAbstractContextItem.h"
#include "vtkRenderingContext2DModule.h" // For export macro
#include "vtkSmartPointer.h"             // Needed for SP ivars.
#include "vtkVector.h"                   // Needed for ivars.
#include "vtkWrappingHints.h"            // For VTK_MARSHALAUTO

VTK_ABI_NAMESPACE_BEGIN
class vtkTransform2D;

class VTKRENDERINGCONTEXT2D_EXPORT VTK_MARSHALAUTO vtkContextTransform
  : public vtkAbstractContextItem
{
public:
  vtkTypeMacro(vtkContextTransform, vtkAbstractContextItem);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /**
   * Creates a vtkContextTransform object.
   */
  static vtkContextTransform* New();

  /**
   * Add child items to this item. Increments reference count of item.
   * \return the index of the child item.
   */
  vtkIdType AddItem(vtkAbstractContextItem* item) override
  {
    return this->Superclass::AddItem(item);
  }

  /**
   * Get the item at the specified index.
   * \return the item at the specified index (null if index is invalid).
   */
  vtkAbstractContextItem* GetItem(vtkIdType index) override
  {
    return this->Superclass::GetItem(index);
  }

  /**
   * Remove all child items from this item.
   */
  void RemoveAllItems() { this->ClearItems(); }

  /**
   * Perform any updates to the item that may be necessary before rendering.
   * The scene should take care of calling this on all items before their
   * Paint function is invoked.
   */
  void Update() override;

  /**
   * Paint event for the item, called whenever the item needs to be drawn.
   */
  bool Paint(vtkContext2D* painter) override;

  /**
   * Reset the transform to the identity transformation.
   */
  virtual void Identity();

  /**
   * Translate the item by the specified amounts dx and dy in the x and y
   * directions.
   */
  virtual void Translate(float dx, float dy);

  /**
   * Scale the item by the specified amounts dx and dy in the x and y
   * directions.
   */
  virtual void Scale(float dx, float dy);

  /**
   * Rotate the item by the specified angle.
   */
  virtual void Rotate(float angle);

  /**
   * Access the vtkTransform2D that controls object transformation.
   */
  virtual vtkTransform2D* GetTransform();

  /**
   * Transforms a point to the parent coordinate system.
   */
  vtkVector2f MapToParent(const vtkVector2f& point) override;

  /**
   * Transforms a point from the parent coordinate system.
   */
  vtkVector2f MapFromParent(const vtkVector2f& point) override;

  ///@{
  /**
   * The mouse button from vtkContextMouseEvent to use for panning.
   * Default is vtkContextMouseEvent::LEFT_BUTTON.
   */
  vtkSetMacro(PanMouseButton, int);
  vtkGetMacro(PanMouseButton, int);
  ///@}

  ///@{
  /**
   * The modifier from vtkContextMouseEvent to use for panning.
   * Default is vtkContextMouseEvent::NO_MODIFIER.
   */
  vtkSetMacro(PanModifier, int);
  vtkGetMacro(PanModifier, int);
  ///@}

  ///@{
  /**
   * A secondary mouse button from vtkContextMouseEvent to use for panning.
   * Default is vtkContextMouseEvent::NO_BUTTON (disabled).
   */
  vtkSetMacro(SecondaryPanMouseButton, int);
  vtkGetMacro(SecondaryPanMouseButton, int);
  ///@}

  ///@{
  /**
   * A secondary modifier from vtkContextMouseEvent to use for panning.
   * Default is vtkContextMouseEvent::NO_MODIFIER.
   */
  vtkSetMacro(SecondaryPanModifier, int);
  vtkGetMacro(SecondaryPanModifier, int);
  ///@}

  ///@{
  /**
   * The mouse button from vtkContextMouseEvent to use for panning.
   * Default is vtkContextMouseEvent::RIGHT_BUTTON.
   */
  vtkSetMacro(ZoomMouseButton, int);
  vtkGetMacro(ZoomMouseButton, int);
  ///@}

  ///@{
  /**
   * The modifier from vtkContextMouseEvent to use for panning.
   * Default is vtkContextMouseEvent::NO_MODIFIER.
   */
  vtkSetMacro(ZoomModifier, int);
  vtkGetMacro(ZoomModifier, int);
  ///@}

  ///@{
  /**
   * A secondary mouse button from vtkContextMouseEvent to use for panning.
   * Default is vtkContextMouseEvent::LEFT_BUTTON.
   */
  vtkSetMacro(SecondaryZoomMouseButton, int);
  vtkGetMacro(SecondaryZoomMouseButton, int);
  ///@}

  ///@{
  /**
   * A secondary modifier from vtkContextMouseEvent to use for panning.
   * Default is vtkContextMouseEvent::SHIFT_MODIFIER.
   */
  vtkSetMacro(SecondaryZoomModifier, int);
  vtkGetMacro(SecondaryZoomModifier, int);
  ///@}

  ///@{
  /**
   * Whether to zoom on mouse wheels. Default is true.
   */
  vtkSetMacro(ZoomOnMouseWheel, bool);
  vtkGetMacro(ZoomOnMouseWheel, bool);
  vtkBooleanMacro(ZoomOnMouseWheel, bool);
  ///@}

  ///@{
  /**
   * Whether to pan in the Y direction on mouse wheels. Default is false.
   */
  vtkSetMacro(PanYOnMouseWheel, bool);
  vtkGetMacro(PanYOnMouseWheel, bool);
  vtkBooleanMacro(PanYOnMouseWheel, bool);
  ///@}

  /**
   * Returns true if the transform is interactive, false otherwise.
   */
  bool Hit(const vtkContextMouseEvent& mouse) override;

  /**
   * Mouse press event. Keep track of zoom anchor position.
   */
  bool MouseButtonPressEvent(const vtkContextMouseEvent& mouse) override;

  /**
   * Mouse move event. Perform pan or zoom as specified by the mouse bindings.
   */
  bool MouseMoveEvent(const vtkContextMouseEvent& mouse) override;

  /**
   * Mouse wheel event. Perform pan or zoom as specified by mouse bindings.
   */
  bool MouseWheelEvent(const vtkContextMouseEvent& mouse, int delta) override;

protected:
  vtkContextTransform();
  ~vtkContextTransform() override;

  vtkNew<vtkTransform2D> Transform;

  int PanMouseButton;
  int PanModifier;
  int ZoomMouseButton;
  int ZoomModifier;
  int SecondaryPanMouseButton;
  int SecondaryPanModifier;
  int SecondaryZoomMouseButton;
  int SecondaryZoomModifier;

  bool ZoomOnMouseWheel;
  bool PanYOnMouseWheel;

  vtkVector2f ZoomAnchor;

private:
  vtkContextTransform(const vtkContextTransform&) = delete;
  void operator=(const vtkContextTransform&) = delete;
};

VTK_ABI_NAMESPACE_END
#endif // vtkContextTransform_h
