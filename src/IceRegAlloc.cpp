//===- subzero/src/IceRegAlloc.cpp - Linear-scan implementation -----------===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the LinearScan class, which performs the
// linear-scan register allocation after liveness analysis has been
// performed.
//
//===----------------------------------------------------------------------===//

#include "IceCfg.h"
#include "IceInst.h"
#include "IceOperand.h"
#include "IceRegAlloc.h"
#include "IceTargetLowering.h"

namespace Ice {

namespace {

// Returns true if Var has any definitions within Item's live range.
// TODO(stichnot): Consider trimming the Definitions list similar to
// how the live ranges are trimmed, since all the overlapsDefs() tests
// are whether some variable's definitions overlap Cur, and trimming
// is with respect Cur.start.  Initial tests show no measurable
// performance difference, so we'll keep the code simple for now.
bool overlapsDefs(const Cfg *Func, const LiveRangeWrapper &Item,
                  const Variable *Var) {
  const bool UseTrimmed = true;
  VariablesMetadata *VMetadata = Func->getVMetadata();
  const InstDefList &Defs = VMetadata->getDefinitions(Var);
  for (size_t i = 0; i < Defs.size(); ++i) {
    if (Item.range().overlapsInst(Defs[i]->getNumber(), UseTrimmed))
      return true;
  }
  return false;
}

void dumpDisableOverlap(const Cfg *Func, const Variable *Var,
                        const char *Reason) {
  if (Func->getContext()->isVerbose(IceV_LinearScan)) {
    VariablesMetadata *VMetadata = Func->getVMetadata();
    Ostream &Str = Func->getContext()->getStrDump();
    Str << "Disabling Overlap due to " << Reason << " " << *Var
        << " LIVE=" << Var->getLiveRange() << " Defs=";
    const InstDefList &Defs = VMetadata->getDefinitions(Var);
    for (size_t i = 0; i < Defs.size(); ++i) {
      if (i > 0)
        Str << ",";
      Str << Defs[i]->getNumber();
    }
    Str << "\n";
  }
}

bool compareRanges(const LiveRangeWrapper &L, const LiveRangeWrapper &R) {
  InstNumberT Lstart = L.Var->getLiveRange().getStart();
  InstNumberT Rstart = R.Var->getLiveRange().getStart();
  if (Lstart == Rstart)
    return L.Var->getIndex() < R.Var->getIndex();
  return Lstart < Rstart;
}

} // end of anonymous namespace

// Implements the linear-scan algorithm.  Based on "Linear Scan
// Register Allocation in the Context of SSA Form and Register
// Constraints" by Hanspeter Mössenböck and Michael Pfeiffer,
// ftp://ftp.ssw.uni-linz.ac.at/pub/Papers/Moe02.PDF .  This
// implementation is modified to take affinity into account and allow
// two interfering variables to share the same register in certain
// cases.
//
// Requires running Cfg::liveness(Liveness_Intervals) in
// preparation.  Results are assigned to Variable::RegNum for each
// Variable.
void LinearScan::scan(const llvm::SmallBitVector &RegMaskFull) {
  TimerMarker T(TimerStack::TT_linearScan, Func);
  assert(RegMaskFull.any()); // Sanity check
  Unhandled.clear();
  UnhandledPrecolored.clear();
  Handled.clear();
  Inactive.clear();
  Active.clear();
  Ostream &Str = Func->getContext()->getStrDump();
  bool Verbose = Func->getContext()->isVerbose(IceV_LinearScan);
  Func->resetCurrentNode();
  VariablesMetadata *VMetadata = Func->getVMetadata();

  // Gather the live ranges of all variables and add them to the
  // Unhandled set.
  const VarList &Vars = Func->getVariables();
  {
    TimerMarker T(TimerStack::TT_initUnhandled, Func);
    Unhandled.reserve(Vars.size());
    for (Variable *Var : Vars) {
      // Explicitly don't consider zero-weight variables, which are
      // meant to be spill slots.
      if (Var->getWeight() == RegWeight::Zero)
        continue;
      // Don't bother if the variable has a null live range, which means
      // it was never referenced.
      if (Var->getLiveRange().isEmpty())
        continue;
      Var->untrimLiveRange();
      LiveRangeWrapper R(Var);
      Unhandled.push_back(R);
      if (Var->hasReg()) {
        Var->setRegNumTmp(Var->getRegNum());
        Var->setLiveRangeInfiniteWeight();
        UnhandledPrecolored.push_back(R);
      }
    }
    // Do a reverse sort so that erasing elements (from the end) is fast.
    std::sort(Unhandled.rbegin(), Unhandled.rend(), compareRanges);
    std::sort(UnhandledPrecolored.rbegin(), UnhandledPrecolored.rend(),
              compareRanges);
  }

  // RegUses[I] is the number of live ranges (variables) that register
  // I is currently assigned to.  It can be greater than 1 as a result
  // of AllowOverlap inference below.
  std::vector<int> RegUses(RegMaskFull.size());
  // Unhandled is already set to all ranges in increasing order of
  // start points.
  assert(Active.empty());
  assert(Inactive.empty());
  assert(Handled.empty());
  UnorderedRanges::iterator Next;

  while (!Unhandled.empty()) {
    LiveRangeWrapper Cur = Unhandled.back();
    Unhandled.pop_back();
    if (Verbose) {
      Str << "\nConsidering  ";
      Cur.dump(Func);
      Str << "\n";
    }
    const llvm::SmallBitVector RegMask =
        RegMaskFull &
        Func->getTarget()->getRegisterSetForType(Cur.Var->getType());

    // Check for precolored ranges.  If Cur is precolored, it
    // definitely gets that register.  Previously processed live
    // ranges would have avoided that register due to it being
    // precolored.  Future processed live ranges won't evict that
    // register because the live range has infinite weight.
    if (Cur.Var->hasReg()) {
      int32_t RegNum = Cur.Var->getRegNum();
      // RegNumTmp should have already been set above.
      assert(Cur.Var->getRegNumTmp() == RegNum);
      if (Verbose) {
        Str << "Precoloring  ";
        Cur.dump(Func);
        Str << "\n";
      }
      Active.push_back(Cur);
      assert(RegUses[RegNum] >= 0);
      ++RegUses[RegNum];
      assert(!UnhandledPrecolored.empty());
      assert(UnhandledPrecolored.back().Var == Cur.Var);
      UnhandledPrecolored.pop_back();
      continue;
    }

    // Check for active ranges that have expired or become inactive.
    for (auto I = Active.begin(), E = Active.end(); I != E; I = Next) {
      Next = I;
      ++Next;
      LiveRangeWrapper Item = *I;
      Item.Var->trimLiveRange(Cur.range().getStart());
      bool Moved = false;
      if (Item.endsBefore(Cur)) {
        // Move Item from Active to Handled list.
        if (Verbose) {
          Str << "Expiring     ";
          Item.dump(Func);
          Str << "\n";
        }
        Handled.splice(Handled.end(), Active, I);
        Moved = true;
      } else if (!Item.overlapsStart(Cur)) {
        // Move Item from Active to Inactive list.
        if (Verbose) {
          Str << "Inactivating ";
          Item.dump(Func);
          Str << "\n";
        }
        Inactive.splice(Inactive.end(), Active, I);
        Moved = true;
      }
      if (Moved) {
        // Decrement Item from RegUses[].
        assert(Item.Var->hasRegTmp());
        int32_t RegNum = Item.Var->getRegNumTmp();
        --RegUses[RegNum];
        assert(RegUses[RegNum] >= 0);
      }
    }

    // Check for inactive ranges that have expired or reactivated.
    for (auto I = Inactive.begin(), E = Inactive.end(); I != E; I = Next) {
      Next = I;
      ++Next;
      LiveRangeWrapper Item = *I;
      Item.Var->trimLiveRange(Cur.range().getStart());
      // As an optimization, don't bother checking pure point-valued
      // Inactive ranges, because the overlapsStart() test will never
      // succeed, and the endsBefore() test will generally only
      // succeed after the last call instruction, which statistically
      // happens near the end.  TODO(stichnot): Consider suppressing
      // this check every N iterations in case calls are only at the
      // beginning of the function.
      if (!Item.range().isNonpoints())
        continue;
      if (Item.endsBefore(Cur)) {
        // Move Item from Inactive to Handled list.
        if (Verbose) {
          Str << "Expiring     ";
          Item.dump(Func);
          Str << "\n";
        }
        Handled.splice(Handled.end(), Inactive, I);
      } else if (Item.overlapsStart(Cur)) {
        // Move Item from Inactive to Active list.
        if (Verbose) {
          Str << "Reactivating ";
          Item.dump(Func);
          Str << "\n";
        }
        Active.splice(Active.end(), Inactive, I);
        // Increment Item in RegUses[].
        assert(Item.Var->hasRegTmp());
        int32_t RegNum = Item.Var->getRegNumTmp();
        assert(RegUses[RegNum] >= 0);
        ++RegUses[RegNum];
      }
    }

    // Calculate available registers into Free[].
    llvm::SmallBitVector Free = RegMask;
    for (SizeT i = 0; i < RegMask.size(); ++i) {
      if (RegUses[i] > 0)
        Free[i] = false;
    }

    // Infer register preference and allowable overlap.  Only form a
    // preference when the current Variable has an unambiguous "first"
    // definition.  The preference is some source Variable of the
    // defining instruction that either is assigned a register that is
    // currently free, or that is assigned a register that is not free
    // but overlap is allowed.  Overlap is allowed when the Variable
    // under consideration is single-definition, and its definition is
    // a simple assignment - i.e., the register gets copied/aliased
    // but is never modified.  Furthermore, overlap is only allowed
    // when preferred Variable definition instructions do not appear
    // within the current Variable's live range.
    Variable *Prefer = NULL;
    int32_t PreferReg = Variable::NoRegister;
    bool AllowOverlap = false;
    if (const Inst *DefInst = VMetadata->getFirstDefinition(Cur.Var)) {
      assert(DefInst->getDest() == Cur.Var);
      bool IsAssign = DefInst->isSimpleAssign();
      bool IsSingleDef = !VMetadata->isMultiDef(Cur.Var);
      for (SizeT i = 0; i < DefInst->getSrcSize(); ++i) {
        // TODO(stichnot): Iterate through the actual Variables of the
        // instruction, not just the source operands.  This could
        // capture Load instructions, including address mode
        // optimization, for Prefer (but not for AllowOverlap).
        if (Variable *SrcVar = llvm::dyn_cast<Variable>(DefInst->getSrc(i))) {
          int32_t SrcReg = SrcVar->getRegNumTmp();
          // Only consider source variables that have (so far) been
          // assigned a register.  That register must be one in the
          // RegMask set, e.g. don't try to prefer the stack pointer
          // as a result of the stacksave intrinsic.
          if (SrcVar->hasRegTmp() && RegMask[SrcReg]) {
            if (!Free[SrcReg]) {
              // Don't bother trying to enable AllowOverlap if the
              // register is already free.
              AllowOverlap =
                  IsSingleDef && IsAssign && !overlapsDefs(Func, Cur, SrcVar);
            }
            if (AllowOverlap || Free[SrcReg]) {
              Prefer = SrcVar;
              PreferReg = SrcReg;
            }
          }
        }
      }
    }
    if (Verbose) {
      if (Prefer) {
        Str << "Initial Prefer=" << *Prefer << " R=" << PreferReg
            << " LIVE=" << Prefer->getLiveRange() << " Overlap=" << AllowOverlap
            << "\n";
      }
    }

    // Remove registers from the Free[] list where an Inactive range
    // overlaps with the current range.
    for (const LiveRangeWrapper &Item : Inactive) {
      if (Item.overlaps(Cur)) {
        int32_t RegNum = Item.Var->getRegNumTmp();
        // Don't assert(Free[RegNum]) because in theory (though
        // probably never in practice) there could be two inactive
        // variables that were marked with AllowOverlap.
        Free[RegNum] = false;
        // Disable AllowOverlap if an Inactive variable, which is not
        // Prefer, shares Prefer's register, and has a definition
        // within Cur's live range.
        if (AllowOverlap && Item.Var != Prefer && RegNum == PreferReg &&
            overlapsDefs(Func, Cur, Item.Var)) {
          AllowOverlap = false;
          dumpDisableOverlap(Func, Item.Var, "Inactive");
        }
      }
    }

    // Disable AllowOverlap if an Active variable, which is not
    // Prefer, shares Prefer's register, and has a definition within
    // Cur's live range.
    for (const LiveRangeWrapper &Item : Active) {
      int32_t RegNum = Item.Var->getRegNumTmp();
      if (Item.Var != Prefer && RegNum == PreferReg &&
          overlapsDefs(Func, Cur, Item.Var)) {
        AllowOverlap = false;
        dumpDisableOverlap(Func, Item.Var, "Active");
      }
    }

    std::vector<RegWeight> Weights(RegMask.size());

    // Remove registers from the Free[] list where an Unhandled
    // precolored range overlaps with the current range, and set those
    // registers to infinite weight so that they aren't candidates for
    // eviction.  Cur.endsBefore(Item) is an early exit check that
    // turns a guaranteed O(N^2) algorithm into expected linear
    // complexity.
    llvm::SmallBitVector PrecoloredUnhandledMask(RegMask.size());
    // Note: PrecoloredUnhandledMask is only used for dumping.
    for (auto I = UnhandledPrecolored.rbegin(), E = UnhandledPrecolored.rend();
         I != E; ++I) {
      LiveRangeWrapper &Item = *I;
      assert(Item.Var->hasReg());
      if (Cur.endsBefore(Item))
        break;
      if (Item.overlaps(Cur)) {
        int32_t ItemReg = Item.Var->getRegNum(); // Note: not getRegNumTmp()
        Weights[ItemReg].setWeight(RegWeight::Inf);
        Free[ItemReg] = false;
        PrecoloredUnhandledMask[ItemReg] = true;
        // Disable AllowOverlap if the preferred register is one of
        // these precolored unhandled overlapping ranges.
        if (AllowOverlap && ItemReg == PreferReg) {
          AllowOverlap = false;
          dumpDisableOverlap(Func, Item.Var, "PrecoloredUnhandled");
        }
      }
    }

    // Print info about physical register availability.
    if (Verbose) {
      for (SizeT i = 0; i < RegMask.size(); ++i) {
        if (RegMask[i]) {
          Str << Func->getTarget()->getRegName(i, IceType_i32)
              << "(U=" << RegUses[i] << ",F=" << Free[i]
              << ",P=" << PrecoloredUnhandledMask[i] << ") ";
        }
      }
      Str << "\n";
    }

    if (Prefer && (AllowOverlap || Free[PreferReg])) {
      // First choice: a preferred register that is either free or is
      // allowed to overlap with its linked variable.
      Cur.Var->setRegNumTmp(PreferReg);
      if (Verbose) {
        Str << "Preferring   ";
        Cur.dump(Func);
        Str << "\n";
      }
      assert(RegUses[PreferReg] >= 0);
      ++RegUses[PreferReg];
      Active.push_back(Cur);
    } else if (Free.any()) {
      // Second choice: any free register.  TODO: After explicit
      // affinity is considered, is there a strategy better than just
      // picking the lowest-numbered available register?
      int32_t RegNum = Free.find_first();
      Cur.Var->setRegNumTmp(RegNum);
      if (Verbose) {
        Str << "Allocating   ";
        Cur.dump(Func);
        Str << "\n";
      }
      assert(RegUses[RegNum] >= 0);
      ++RegUses[RegNum];
      Active.push_back(Cur);
    } else {
      // Fallback: there are no free registers, so we look for the
      // lowest-weight register and see if Cur has higher weight.
      // Check Active ranges.
      for (const LiveRangeWrapper &Item : Active) {
        assert(Item.overlaps(Cur));
        int32_t RegNum = Item.Var->getRegNumTmp();
        assert(Item.Var->hasRegTmp());
        Weights[RegNum].addWeight(Item.range().getWeight());
      }
      // Same as above, but check Inactive ranges instead of Active.
      for (const LiveRangeWrapper &Item : Inactive) {
        int32_t RegNum = Item.Var->getRegNumTmp();
        assert(Item.Var->hasRegTmp());
        if (Item.overlaps(Cur))
          Weights[RegNum].addWeight(Item.range().getWeight());
      }

      // All the weights are now calculated.  Find the register with
      // smallest weight.
      int32_t MinWeightIndex = RegMask.find_first();
      // MinWeightIndex must be valid because of the initial
      // RegMask.any() test.
      assert(MinWeightIndex >= 0);
      for (SizeT i = MinWeightIndex + 1; i < Weights.size(); ++i) {
        if (RegMask[i] && Weights[i] < Weights[MinWeightIndex])
          MinWeightIndex = i;
      }

      if (Cur.range().getWeight() <= Weights[MinWeightIndex]) {
        // Cur doesn't have priority over any other live ranges, so
        // don't allocate any register to it, and move it to the
        // Handled state.
        Handled.push_back(Cur);
        if (Cur.range().getWeight().isInf()) {
          Func->setError("Unable to find a physical register for an "
                         "infinite-weight live range");
        }
      } else {
        // Evict all live ranges in Active that register number
        // MinWeightIndex is assigned to.
        for (auto I = Active.begin(), E = Active.end(); I != E; I = Next) {
          Next = I;
          ++Next;
          LiveRangeWrapper Item = *I;
          if (Item.Var->getRegNumTmp() == MinWeightIndex) {
            if (Verbose) {
              Str << "Evicting     ";
              Item.dump(Func);
              Str << "\n";
            }
            --RegUses[MinWeightIndex];
            assert(RegUses[MinWeightIndex] >= 0);
            Item.Var->setRegNumTmp(Variable::NoRegister);
            Handled.splice(Handled.end(), Active, I);
          }
        }
        // Do the same for Inactive.
        for (auto I = Inactive.begin(), E = Inactive.end(); I != E; I = Next) {
          Next = I;
          ++Next;
          LiveRangeWrapper Item = *I;
          // Note: The Item.overlaps(Cur) clause is not part of the
          // description of AssignMemLoc() in the original paper.  But
          // there doesn't seem to be any need to evict an inactive
          // live range that doesn't overlap with the live range
          // currently being considered.  It's especially bad if we
          // would end up evicting an infinite-weight but
          // currently-inactive live range.  The most common situation
          // for this would be a scratch register kill set for call
          // instructions.
          if (Item.Var->getRegNumTmp() == MinWeightIndex &&
              Item.overlaps(Cur)) {
            if (Verbose) {
              Str << "Evicting     ";
              Item.dump(Func);
              Str << "\n";
            }
            Item.Var->setRegNumTmp(Variable::NoRegister);
            Handled.splice(Handled.end(), Inactive, I);
          }
        }
        // Assign the register to Cur.
        Cur.Var->setRegNumTmp(MinWeightIndex);
        assert(RegUses[MinWeightIndex] >= 0);
        ++RegUses[MinWeightIndex];
        Active.push_back(Cur);
        if (Verbose) {
          Str << "Allocating   ";
          Cur.dump(Func);
          Str << "\n";
        }
      }
    }
    dump(Func);
  }
  // Move anything Active or Inactive to Handled for easier handling.
  for (const LiveRangeWrapper &I : Active)
    Handled.push_back(I);
  Active.clear();
  for (const LiveRangeWrapper &I : Inactive)
    Handled.push_back(I);
  Inactive.clear();
  dump(Func);

  // Finish up by assigning RegNumTmp->RegNum for each Variable.
  for (const LiveRangeWrapper &Item : Handled) {
    int32_t RegNum = Item.Var->getRegNumTmp();
    if (Verbose) {
      if (!Item.Var->hasRegTmp()) {
        Str << "Not assigning ";
        Item.Var->dump(Func);
        Str << "\n";
      } else {
        Str << (RegNum == Item.Var->getRegNum() ? "Reassigning " : "Assigning ")
            << Func->getTarget()->getRegName(RegNum, IceType_i32) << "(r"
            << RegNum << ") to ";
        Item.Var->dump(Func);
        Str << "\n";
      }
    }
    Item.Var->setRegNum(Item.Var->getRegNumTmp());
  }

  // TODO: Consider running register allocation one more time, with
  // infinite registers, for two reasons.  First, evicted live ranges
  // get a second chance for a register.  Second, it allows coalescing
  // of stack slots.  If there is no time budget for the second
  // register allocation run, each unallocated variable just gets its
  // own slot.
  //
  // Another idea for coalescing stack slots is to initialize the
  // Unhandled list with just the unallocated variables, saving time
  // but not offering second-chance opportunities.
}

// ======================== Dump routines ======================== //

void LiveRangeWrapper::dump(const Cfg *Func) const {
  Ostream &Str = Func->getContext()->getStrDump();
  const static size_t BufLen = 30;
  char buf[BufLen];
  snprintf(buf, BufLen, "%2d", Var->getRegNumTmp());
  Str << "R=" << buf << "  V=";
  Var->dump(Func);
  Str << "  Range=" << range();
}

void LinearScan::dump(Cfg *Func) const {
  Ostream &Str = Func->getContext()->getStrDump();
  if (!Func->getContext()->isVerbose(IceV_LinearScan))
    return;
  Func->resetCurrentNode();
  Str << "**** Current regalloc state:\n";
  Str << "++++++ Handled:\n";
  for (const LiveRangeWrapper &Item : Handled) {
    Item.dump(Func);
    Str << "\n";
  }
  Str << "++++++ Unhandled:\n";
  for (auto I = Unhandled.rbegin(), E = Unhandled.rend(); I != E; ++I) {
    I->dump(Func);
    Str << "\n";
  }
  Str << "++++++ Active:\n";
  for (const LiveRangeWrapper &Item : Active) {
    Item.dump(Func);
    Str << "\n";
  }
  Str << "++++++ Inactive:\n";
  for (const LiveRangeWrapper &Item : Inactive) {
    Item.dump(Func);
    Str << "\n";
  }
}

} // end of namespace Ice
