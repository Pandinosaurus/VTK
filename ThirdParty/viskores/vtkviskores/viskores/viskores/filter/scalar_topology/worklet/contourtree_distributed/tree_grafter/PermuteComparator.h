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
// Copyright (c) 2018, The Regents of the University of California, through
// Lawrence Berkeley National Laboratory (subject to receipt of any required approvals
// from the U.S. Dept. of Energy).  All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this
//     list of conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National
//     Laboratory, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without
//     specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
// IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
// INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
// OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
//
//=============================================================================
//
//  This code is an extension of the algorithm presented in the paper:
//  Parallel Peak Pruning for Scalable SMP Contour Tree Computation.
//  Hamish Carr, Gunther Weber, Christopher Sewell, and James Ahrens.
//  Proceedings of the IEEE Symposium on Large Data Analysis and Visualization
//  (LDAV), October 2016, Baltimore, Maryland.
//
//  The PPP2 algorithm and software were jointly developed by
//  Hamish Carr (University of Leeds), Gunther H. Weber (LBNL), and
//  Oliver Ruebel (LBNL)
//==============================================================================

#ifndef viskores_worklet_contourtree_distributed_tree_grafter_permute_comparator_h
#define viskores_worklet_contourtree_distributed_tree_grafter_permute_comparator_h


#include <viskores/cont/ExecutionObjectBase.h>
#include <viskores/filter/scalar_topology/worklet/contourtree_augmented/Types.h>

namespace viskores
{
namespace worklet
{
namespace contourtree_distributed
{
namespace tree_grafter
{

/// Comparator used  in TreeGrafter::ListNewSupernodes to sort the NewSupernodes arrays
class PermuteComparatorImpl : public viskores::worklet::WorkletMapField
{
public:
  using IdArrayPortalType = viskores::worklet::contourtree_augmented::IdArrayType::ReadPortalType;

  // Default Constructor
  VISKORES_EXEC_CONT
  PermuteComparatorImpl(const IdArrayPortalType& lookupArrayPortal)
    : LookupArrayPortal(lookupArrayPortal)
  {
  }

  VISKORES_EXEC bool operator()(const viskores::Id& i, const viskores::Id& j) const
  { // operator ()
    // primary sort on indexed value
    if (this->LookupArrayPortal.Get(i) < this->LookupArrayPortal.Get(j))
    {
      return true;
    }
    if (this->LookupArrayPortal.Get(i) > this->LookupArrayPortal.Get(j))
    {
      return false;
    }
    // secondary sort on index
    if (i < j)
    {
      return true;
    }
    if (j < i)
    {
      return false;
    }

    // fallback just in case
    return false;

  } // operator ()

private:
  IdArrayPortalType LookupArrayPortal;

}; // PermuteComparator


class PermuteComparator : public viskores::cont::ExecutionObjectBase
{
public:
  VISKORES_CONT
  PermuteComparator(const viskores::worklet::contourtree_augmented::IdArrayType& lookupArray)
    : LookupArray(lookupArray)
  {
  }

  VISKORES_CONT PermuteComparatorImpl PrepareForExecution(viskores::cont::DeviceAdapterId device,
                                                          viskores::cont::Token& token) const
  {
    return PermuteComparatorImpl(this->LookupArray.PrepareForInput(device, token));
  }

private:
  const viskores::worklet::contourtree_augmented::IdArrayType& LookupArray;
};

} // namespace tree_grafter
} // namespace contourtree_distributed
} // namespace worklet
} // namespace viskores

#endif
