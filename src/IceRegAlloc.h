//===- subzero/src/IceRegAlloc.h - Linear-scan reg. allocation --*- C++ -*-===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file declares the data structures used during linear-scan
// register allocation.  This includes LiveRangeWrapper which
// encapsulates a variable and its live range, and LinearScan which
// holds the various work queues for the linear-scan algorithm.
//
//===----------------------------------------------------------------------===//

#ifndef SUBZERO_SRC_ICEREGALLOC_H
#define SUBZERO_SRC_ICEREGALLOC_H

#include "IceDefs.h"
#include "IceTypes.h"

namespace Ice {

// Currently this just wraps a Variable pointer, so in principle we
// could use containers of Variable* instead of LiveRangeWrapper.  But
// in the future, we may want to do more complex things such as live
// range splitting, and keeping a wrapper should make that simpler.
class LiveRangeWrapper {
public:
  LiveRangeWrapper(Variable *Var) : Var(Var) {}
  const LiveRange &range() const { return Var->getLiveRange(); }
  bool endsBefore(const LiveRangeWrapper &Other) const {
    return range().endsBefore(Other.range());
  }
  bool overlaps(const LiveRangeWrapper &Other) const {
    const bool UseTrimmed = true;
    return range().overlaps(Other.range(), UseTrimmed);
  }
  bool overlapsStart(const LiveRangeWrapper &Other) const {
    const bool UseTrimmed = true;
    return range().overlapsInst(Other.range().getStart(), UseTrimmed);
  }
  Variable *Var;
  void dump(const Cfg *Func) const;

private:
  // LiveRangeWrapper(const LiveRangeWrapper &) = delete;
  // LiveRangeWrapper &operator=(const LiveRangeWrapper &) = delete;
};

class LinearScan {
public:
  LinearScan(Cfg *Func) : Func(Func) {}
  void scan(const llvm::SmallBitVector &RegMask);
  void dump(Cfg *Func) const;

private:
  Cfg *const Func;
  typedef std::vector<LiveRangeWrapper> OrderedRanges;
  typedef std::list<LiveRangeWrapper> UnorderedRanges;
  OrderedRanges Unhandled;
  // UnhandledPrecolored is a subset of Unhandled, specially collected
  // for faster processing.
  OrderedRanges UnhandledPrecolored;
  UnorderedRanges Active, Inactive, Handled;
  LinearScan(const LinearScan &) = delete;
  LinearScan &operator=(const LinearScan &) = delete;
};

} // end of namespace Ice

#endif // SUBZERO_SRC_ICEREGALLOC_H
