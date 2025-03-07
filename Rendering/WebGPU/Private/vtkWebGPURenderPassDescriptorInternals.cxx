// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
#include "Private/vtkWebGPURenderPassDescriptorInternals.h"

VTK_ABI_NAMESPACE_BEGIN
//------------------------------------------------------------------------------
vtkWebGPURenderPassDescriptorInternals::~vtkWebGPURenderPassDescriptorInternals() = default;

//------------------------------------------------------------------------------
vtkWebGPURenderPassDescriptorInternals::vtkWebGPURenderPassDescriptorInternals(
  const std::vector<wgpu::TextureView>& colorAttachmentInfo,
  wgpu::TextureView depthStencil /*= wgpu::TextureView()*/, bool clearColor /*= true*/,
  bool clearDepth /*= true*/, bool clearStencil /*= true*/)
{
  const wgpu::LoadOp colorLoadOp = clearColor ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
  for (uint32_t i = 0; i < kMaxColorAttachments; ++i)
  {
    this->ColorAttachments[i].loadOp = colorLoadOp;
    this->ColorAttachments[i].storeOp = wgpu::StoreOp::Store;
    this->ColorAttachments[i].clearValue = { 0.0f, 0.0f, 0.0f, 0.0f };
  }

  const wgpu::LoadOp depthLoadOp = clearDepth ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
  const wgpu::LoadOp stencilLoadOp = clearStencil ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
  this->DepthStencilAttachmentInfo.depthClearValue = 1.0f;
  this->DepthStencilAttachmentInfo.stencilClearValue = 0;
  this->DepthStencilAttachmentInfo.depthLoadOp = depthLoadOp;
  this->DepthStencilAttachmentInfo.depthStoreOp = wgpu::StoreOp::Store;
  this->DepthStencilAttachmentInfo.stencilLoadOp = stencilLoadOp;
  this->DepthStencilAttachmentInfo.stencilStoreOp = wgpu::StoreOp::Store;

  colorAttachmentCount = static_cast<uint32_t>(colorAttachmentInfo.size());
  uint32_t colorAttachmentIndex = 0;
  for (const wgpu::TextureView& colorAttachment : colorAttachmentInfo)
  {
    if (colorAttachment.Get() != nullptr)
    {
      this->ColorAttachments[colorAttachmentIndex].view = colorAttachment;
    }
    ++colorAttachmentIndex;
  }

  if (!this->ColorAttachments.empty())
  {
    colorAttachments = this->ColorAttachments.data();
  }
  else
  {
    colorAttachments = nullptr;
  }

  if (depthStencil.Get() != nullptr)
  {
    this->DepthStencilAttachmentInfo.view = depthStencil;
    depthStencilAttachment = &this->DepthStencilAttachmentInfo;
  }
  else
  {
    depthStencilAttachment = nullptr;
  }
}

//------------------------------------------------------------------------------
vtkWebGPURenderPassDescriptorInternals::vtkWebGPURenderPassDescriptorInternals(
  const vtkWebGPURenderPassDescriptorInternals& other)
  : RenderPassDescriptor(other)
{
  *this = other;
}

//------------------------------------------------------------------------------
const vtkWebGPURenderPassDescriptorInternals& vtkWebGPURenderPassDescriptorInternals::operator=(
  const vtkWebGPURenderPassDescriptorInternals& otherRenderPass)
{
  this->DepthStencilAttachmentInfo = otherRenderPass.DepthStencilAttachmentInfo;
  this->ColorAttachments = otherRenderPass.ColorAttachments;
  colorAttachmentCount = otherRenderPass.colorAttachmentCount;
  colorAttachments = this->ColorAttachments.data();
  if (otherRenderPass.depthStencilAttachment != nullptr)
  {
    // Assign desc.depthStencilAttachment to this->depthStencilAttachmentInfo;
    depthStencilAttachment = &(this->DepthStencilAttachmentInfo);
  }
  else
  {
    depthStencilAttachment = nullptr;
  }
  return *this;
}

//------------------------------------------------------------------------------
void vtkWebGPURenderPassDescriptorInternals::UnsetDepthStencilLoadStoreOpsForFormat(
  wgpu::TextureFormat format)
{
  switch (format)
  {
    case wgpu::TextureFormat::Depth24Plus:
    case wgpu::TextureFormat::Depth32Float:
    case wgpu::TextureFormat::Depth16Unorm:
      this->DepthStencilAttachmentInfo.stencilLoadOp = wgpu::LoadOp::Undefined;
      this->DepthStencilAttachmentInfo.stencilStoreOp = wgpu::StoreOp::Undefined;
      break;
    case wgpu::TextureFormat::Stencil8:
      this->DepthStencilAttachmentInfo.depthLoadOp = wgpu::LoadOp::Undefined;
      this->DepthStencilAttachmentInfo.depthStoreOp = wgpu::StoreOp::Undefined;
      break;
    default:
      break;
  }
}
VTK_ABI_NAMESPACE_END
