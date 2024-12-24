// Copyright 2023 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/PowerPC/JitCommon/ConstantPropagation.h"

namespace JitCommon
{
static constexpr u32 BitOR(u32 a, u32 b)
{
  return a | b;
}

static constexpr u32 BitAND(u32 a, u32 b)
{
  return a & b;
}

static constexpr u32 BitXOR(u32 a, u32 b)
{
  return a ^ b;
}

ConstantPropagationResult ConstantPropagation::EvaluateInstruction(UGeckoInstruction inst) const
{
  switch (inst.OPCD)
  {
  case 24:  // ori
  case 25:  // oris
    return EvaluateBitwiseImm(inst, BitOR);
  case 26:  // xori
  case 27:  // xoris
    return EvaluateBitwiseImm(inst, BitXOR);
  case 28:  // andi
  case 29:  // andis
    return EvaluateBitwiseImm(inst, BitAND);
  default:
    return {};
  }
}

ConstantPropagationResult ConstantPropagation::EvaluateBitwiseImm(UGeckoInstruction inst,
                                                                  u32 (*do_op)(u32, u32)) const
{
  const bool is_and = do_op == &BitAND;
  const u32 immediate = inst.OPCD & 1 ? inst.UIMM << 16 : inst.UIMM;

  if (inst.UIMM == 0 && !is_and && inst.RA == inst.RS)
    return DO_NOTHING;

  if (!HasGPR(inst.RS))
    return {};

  return ConstantPropagationResult(inst.RA, do_op(m_gpr_values[inst.RS], immediate), is_and);
}

void ConstantPropagation::Apply(ConstantPropagationResult result)
{
  if (result.gpr >= 0)
    SetGPR(result.gpr, result.gpr_value);
}

}  // namespace JitCommon
