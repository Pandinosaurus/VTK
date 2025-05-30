//============================================================================
//  The contents of this file are covered by the Viskores license. See
//  LICENSE.txt for details.
//
//  By contributing to this file, all contributors agree to the Developer
//  Certificate of Origin Version 1.1 (DCO 1.1) as stated in DCO.txt.
//============================================================================

//============================================================================
//  Copyright (c) Kitware, Inc.
//  All rights reserved.
//  See LICENSE.txt for details.
//
//  This software is distributed WITHOUT ANY WARRANTY; without even
//  the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
//  PURPOSE.  See the above copyright notice for more information.
//============================================================================

#ifndef viskores_filter_flow_ParticleAdvection_h
#define viskores_filter_flow_ParticleAdvection_h

#include <viskores/filter/flow/FilterParticleAdvectionSteadyState.h>
#include <viskores/filter/flow/FlowTypes.h>
#include <viskores/filter/flow/viskores_filter_flow_export.h>

#include <viskores/filter/flow/worklet/Analysis.h>
#include <viskores/filter/flow/worklet/Field.h>
#include <viskores/filter/flow/worklet/Termination.h>

namespace viskores
{
namespace filter
{
namespace flow
{

class ParticleAdvection;

template <>
struct FlowTraits<ParticleAdvection>
{
  using ParticleType = viskores::Particle;
  using TerminationType = viskores::worklet::flow::NormalTermination;
  using AnalysisType = viskores::worklet::flow::NoAnalysis<ParticleType>;
  using ArrayType = viskores::cont::ArrayHandle<viskores::Vec3f>;
  using FieldType = viskores::worklet::flow::VelocityField<ArrayType>;
};

/// \brief Advect particles in a vector field.

/// Takes as input a vector field and seed locations and generates the
/// end points for each seed through the vector field.

class VISKORES_FILTER_FLOW_EXPORT ParticleAdvection
  : public viskores::filter::flow::FilterParticleAdvectionSteadyState<ParticleAdvection>
{
public:
  using ParticleType = typename FlowTraits<ParticleAdvection>::ParticleType;
  using TerminationType = typename FlowTraits<ParticleAdvection>::TerminationType;
  using AnalysisType = typename FlowTraits<ParticleAdvection>::AnalysisType;
  using ArrayType = typename FlowTraits<ParticleAdvection>::ArrayType;
  using FieldType = typename FlowTraits<ParticleAdvection>::FieldType;

  VISKORES_CONT FieldType GetField(const viskores::cont::DataSet& data) const;

  VISKORES_CONT TerminationType GetTermination(const viskores::cont::DataSet& data) const;

  VISKORES_CONT AnalysisType GetAnalysis(const viskores::cont::DataSet& data) const;
};

}
}
} // namespace viskores::filter::flow

#endif // viskores_filter_flow_ParticleAdvection_h
