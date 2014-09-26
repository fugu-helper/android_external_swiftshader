// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.
//
// Modified by the Subzero authors.
//
//===- subzero/src/assembler_ia32.cpp - Assembler for x86-32  -------------===//
//
//                        The Subzero Code Generator
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the Assembler class for x86-32.
//
//===----------------------------------------------------------------------===//

#include "assembler_ia32.h"
#include "IceCfg.h"
#include "IceMemoryRegion.h"
#include "IceOperand.h"

namespace Ice {
namespace x86 {

class DirectCallRelocation : public AssemblerFixup {
public:
  static DirectCallRelocation *create(Assembler *Asm, FixupKind Kind,
                                      const ConstantRelocatable *Sym) {
    return new (Asm->Allocate<DirectCallRelocation>())
        DirectCallRelocation(Kind, Sym);
  }

  void Process(const MemoryRegion &region, intptr_t position) {
    // Direct calls are relative to the following instruction on x86.
    int32_t pointer = region.Load<int32_t>(position);
    int32_t delta = region.start() + position + sizeof(int32_t);
    region.Store<int32_t>(position, pointer - delta);
  }

private:
  DirectCallRelocation(FixupKind Kind, const ConstantRelocatable *Sym)
      : AssemblerFixup(Kind, Sym) {}
};

Address Address::ofConstPool(GlobalContext *Ctx, Assembler *Asm,
                             const Constant *Imm) {
  // We should make this much lighter-weight. E.g., just record the const pool
  // entry ID.
  std::string Buffer;
  llvm::raw_string_ostream StrBuf(Buffer);
  Type Ty = Imm->getType();
  assert(llvm::isa<ConstantFloat>(Imm) || llvm::isa<ConstantDouble>(Imm));
  StrBuf << "L$" << Ty << "$" << Imm->getPoolEntryID();
  const int64_t Offset = 0;
  const bool SuppressMangling = true;
  Constant *Sym =
      Ctx->getConstantSym(Ty, Offset, StrBuf.str(), SuppressMangling);
  AssemblerFixup *Fixup = x86::DisplacementRelocation::create(
      Asm, FK_Abs_4, llvm::cast<ConstantRelocatable>(Sym));
  return x86::Address::Absolute(Offset, Fixup);
}

void AssemblerX86::call(GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xFF);
  EmitRegisterOperand(2, reg);
}

void AssemblerX86::call(const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xFF);
  EmitOperand(2, address);
}

void AssemblerX86::call(Label *label) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xE8);
  static const int kSize = 5;
  EmitLabel(label, kSize);
}

void AssemblerX86::call(const ConstantRelocatable *label) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  intptr_t call_start = buffer_.GetPosition();
  EmitUint8(0xE8);
  EmitFixup(DirectCallRelocation::create(this, FK_PcRel_4, label));
  EmitInt32(-4);
  assert((buffer_.GetPosition() - call_start) == kCallExternalLabelSize);
}

void AssemblerX86::pushl(GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x50 + reg);
}

void AssemblerX86::pushl(const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xFF);
  EmitOperand(6, address);
}

void AssemblerX86::pushl(const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x68);
  EmitImmediate(imm);
}

void AssemblerX86::popl(GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x58 + reg);
}

void AssemblerX86::popl(const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x8F);
  EmitOperand(0, address);
}

void AssemblerX86::pushal() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x60);
}

void AssemblerX86::popal() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x61);
}

void AssemblerX86::setcc(CondX86::BrCond condition, ByteRegister dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x90 + condition);
  EmitUint8(0xC0 + dst);
}

void AssemblerX86::movl(GPRRegister dst, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xB8 + dst);
  EmitImmediate(imm);
}

void AssemblerX86::movl(GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x89);
  EmitRegisterOperand(src, dst);
}

void AssemblerX86::movl(GPRRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x8B);
  EmitOperand(dst, src);
}

void AssemblerX86::movl(const Address &dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x89);
  EmitOperand(src, dst);
}

void AssemblerX86::movl(const Address &dst, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xC7);
  EmitOperand(0, dst);
  EmitImmediate(imm);
}

void AssemblerX86::movzxb(GPRRegister dst, ByteRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xB6);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::movzxb(GPRRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xB6);
  EmitOperand(dst, src);
}

void AssemblerX86::movsxb(GPRRegister dst, ByteRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xBE);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::movsxb(GPRRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xBE);
  EmitOperand(dst, src);
}

void AssemblerX86::movb(ByteRegister dst, const Address &src) {
  (void)dst;
  (void)src;
  // FATAL
  llvm_unreachable("Use movzxb or movsxb instead.");
}

void AssemblerX86::movb(const Address &dst, ByteRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x88);
  EmitOperand(src, dst);
}

void AssemblerX86::movb(const Address &dst, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xC6);
  EmitOperand(RegX8632::Encoded_Reg_eax, dst);
  assert(imm.is_int8());
  EmitUint8(imm.value() & 0xFF);
}

void AssemblerX86::movzxw(GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xB7);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::movzxw(GPRRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xB7);
  EmitOperand(dst, src);
}

void AssemblerX86::movsxw(GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xBF);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::movsxw(GPRRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xBF);
  EmitOperand(dst, src);
}

void AssemblerX86::movw(GPRRegister dst, const Address &src) {
  (void)dst;
  (void)src;
  // FATAL
  llvm_unreachable("Use movzxw or movsxw instead.");
}

void AssemblerX86::movw(const Address &dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  EmitUint8(0x89);
  EmitOperand(src, dst);
}

void AssemblerX86::lea(Type Ty, GPRRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  assert(Ty == IceType_i16 || Ty == IceType_i32);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitUint8(0x8D);
  EmitOperand(dst, src);
}

void AssemblerX86::cmov(CondX86::BrCond cond, GPRRegister dst,
                        GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x40 + cond);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::rep_movsb() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0xA4);
}

void AssemblerX86::movss(XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0x0F);
  EmitUint8(0x10);
  EmitOperand(dst, src);
}

void AssemblerX86::movss(const Address &dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitOperand(src, dst);
}

void AssemblerX86::movss(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitXmmRegisterOperand(src, dst);
}

void AssemblerX86::movd(XmmRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x6E);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::movd(XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x6E);
  EmitOperand(dst, src);
}

void AssemblerX86::movd(GPRRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x7E);
  EmitRegisterOperand(src, dst);
}

void AssemblerX86::movd(const Address &dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x7E);
  EmitOperand(src, dst);
}

void AssemblerX86::movq(const Address &dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xD6);
  EmitOperand(src, Operand(dst));
}

void AssemblerX86::movq(XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0x0F);
  EmitUint8(0x7E);
  EmitOperand(dst, Operand(src));
}

void AssemblerX86::addss(Type Ty, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(Ty == IceType_f32 ? 0xF3 : 0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x58);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::addss(Type Ty, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(Ty == IceType_f32 ? 0xF3 : 0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x58);
  EmitOperand(dst, src);
}

void AssemblerX86::subss(Type Ty, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(Ty == IceType_f32 ? 0xF3 : 0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x5C);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::subss(Type Ty, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(Ty == IceType_f32 ? 0xF3 : 0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x5C);
  EmitOperand(dst, src);
}

void AssemblerX86::mulss(Type Ty, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(Ty == IceType_f32 ? 0xF3 : 0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x59);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::mulss(Type Ty, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(Ty == IceType_f32 ? 0xF3 : 0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x59);
  EmitOperand(dst, src);
}

void AssemblerX86::divss(Type Ty, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(Ty == IceType_f32 ? 0xF3 : 0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x5E);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::divss(Type Ty, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(Ty == IceType_f32 ? 0xF3 : 0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x5E);
  EmitOperand(dst, src);
}

void AssemblerX86::flds(const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitOperand(0, src);
}

void AssemblerX86::fstps(const Address &dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitOperand(3, dst);
}

void AssemblerX86::movsd(XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x10);
  EmitOperand(dst, src);
}

void AssemblerX86::movsd(const Address &dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitOperand(src, dst);
}

void AssemblerX86::movsd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitXmmRegisterOperand(src, dst);
}

void AssemblerX86::movaps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x28);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::movups(XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x10);
  EmitOperand(dst, src);
}

void AssemblerX86::movups(const Address &dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x11);
  EmitOperand(src, dst);
}

void AssemblerX86::padd(Type Ty, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitUint8(0xFC);
  } else if (Ty == IceType_i16) {
    EmitUint8(0xFD);
  } else {
    EmitUint8(0xFE);
  }
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::padd(Type Ty, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitUint8(0xFC);
  } else if (Ty == IceType_i16) {
    EmitUint8(0xFD);
  } else {
    EmitUint8(0xFE);
  }
  EmitOperand(dst, src);
}

void AssemblerX86::pand(Type /* Ty */, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xDB);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::pand(Type /* Ty */, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xDB);
  EmitOperand(dst, src);
}

void AssemblerX86::pandn(Type /* Ty */, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xDF);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::pandn(Type /* Ty */, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xDF);
  EmitOperand(dst, src);
}

void AssemblerX86::pmuludq(Type /* Ty */, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xF4);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::pmuludq(Type /* Ty */, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xF4);
  EmitOperand(dst, src);
}

void AssemblerX86::por(Type /* Ty */, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xEB);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::por(Type /* Ty */, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xEB);
  EmitOperand(dst, src);
}

void AssemblerX86::psub(Type Ty, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitUint8(0xF8);
  } else if (Ty == IceType_i16) {
    EmitUint8(0xF9);
  } else {
    EmitUint8(0xFA);
  }
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::psub(Type Ty, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitUint8(0xF8);
  } else if (Ty == IceType_i16) {
    EmitUint8(0xF9);
  } else {
    EmitUint8(0xFA);
  }
  EmitOperand(dst, src);
}

void AssemblerX86::pxor(Type /* Ty */, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xEF);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::pxor(Type /* Ty */, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xEF);
  EmitOperand(dst, src);
}

// {add,sub,mul,div}ps are given a Ty parameter for consistency with
// {add,sub,mul,div}ss. In the future, when the PNaCl ABI allows
// addpd, etc., we can use the Ty parameter to decide on adding
// a 0x66 prefix.
void AssemblerX86::addps(Type /* Ty */, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x58);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::addps(Type /* Ty */, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x58);
  EmitOperand(dst, src);
}

void AssemblerX86::subps(Type /* Ty */, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x5C);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::subps(Type /* Ty */, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x5C);
  EmitOperand(dst, src);
}

void AssemblerX86::divps(Type /* Ty */, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x5E);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::divps(Type /* Ty */, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x5E);
  EmitOperand(dst, src);
}

void AssemblerX86::mulps(Type /* Ty */, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x59);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::mulps(Type /* Ty */, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x59);
  EmitOperand(dst, src);
}

void AssemblerX86::minps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x5D);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::maxps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x5F);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::andps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x54);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::andps(XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x54);
  EmitOperand(dst, src);
}

void AssemblerX86::orps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x56);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::cmpps(XmmRegister dst, XmmRegister src,
                         CondX86::CmppsCond CmpCondition) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xC2);
  EmitXmmRegisterOperand(dst, src);
  EmitUint8(CmpCondition);
}

void AssemblerX86::cmpps(XmmRegister dst, const Address &src,
                         CondX86::CmppsCond CmpCondition) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xC2);
  EmitOperand(dst, src);
  EmitUint8(CmpCondition);
}

void AssemblerX86::sqrtps(XmmRegister dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x51);
  EmitXmmRegisterOperand(dst, dst);
}

void AssemblerX86::rsqrtps(XmmRegister dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x52);
  EmitXmmRegisterOperand(dst, dst);
}

void AssemblerX86::reciprocalps(XmmRegister dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x53);
  EmitXmmRegisterOperand(dst, dst);
}

void AssemblerX86::movhlps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x12);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::movlhps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x16);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::unpcklps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x14);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::unpckhps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x15);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::unpcklpd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x14);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::unpckhpd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x15);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::set1ps(XmmRegister dst, GPRRegister tmp1,
                          const Immediate &imm) {
  // Load 32-bit immediate value into tmp1.
  movl(tmp1, imm);
  // Move value from tmp1 into dst.
  movd(dst, tmp1);
  // Broadcast low lane into other three lanes.
  shufps(dst, dst, Immediate(0x0));
}

void AssemblerX86::shufps(XmmRegister dst, XmmRegister src,
                          const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xC6);
  EmitXmmRegisterOperand(dst, src);
  assert(imm.is_uint8());
  EmitUint8(imm.value());
}

void AssemblerX86::minpd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x5D);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::maxpd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x5F);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::sqrtpd(XmmRegister dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x51);
  EmitXmmRegisterOperand(dst, dst);
}

void AssemblerX86::cvtps2pd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x5A);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::cvtpd2ps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x5A);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::shufpd(XmmRegister dst, XmmRegister src,
                          const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0xC6);
  EmitXmmRegisterOperand(dst, src);
  assert(imm.is_uint8());
  EmitUint8(imm.value());
}

void AssemblerX86::cvtsi2ss(XmmRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0x0F);
  EmitUint8(0x2A);
  EmitOperand(dst, Operand(src));
}

void AssemblerX86::cvtsi2sd(XmmRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x2A);
  EmitOperand(dst, Operand(src));
}

void AssemblerX86::cvtss2si(GPRRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0x0F);
  EmitUint8(0x2D);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::cvtss2sd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0x0F);
  EmitUint8(0x5A);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::cvtsd2si(GPRRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x2D);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::cvttss2si(GPRRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0x0F);
  EmitUint8(0x2C);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::cvttsd2si(GPRRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x2C);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::cvtsd2ss(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x5A);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::cvtdq2pd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF3);
  EmitUint8(0x0F);
  EmitUint8(0xE6);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::ucomiss(Type Ty, XmmRegister a, XmmRegister b) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_f64)
    EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x2E);
  EmitXmmRegisterOperand(a, b);
}

void AssemblerX86::ucomiss(Type Ty, XmmRegister a, const Address &b) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_f64)
    EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x2E);
  EmitOperand(a, b);
}

void AssemblerX86::movmskpd(GPRRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x50);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::movmskps(GPRRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x50);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::sqrtss(Type Ty, XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(Ty == IceType_f32 ? 0xF3 : 0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x51);
  EmitOperand(dst, src);
}

void AssemblerX86::sqrtss(Type Ty, XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(Ty == IceType_f32 ? 0xF3 : 0xF2);
  EmitUint8(0x0F);
  EmitUint8(0x51);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::xorpd(XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x57);
  EmitOperand(dst, src);
}

void AssemblerX86::xorpd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x57);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::orpd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x56);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::xorps(XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x57);
  EmitOperand(dst, src);
}

void AssemblerX86::xorps(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x57);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::andpd(XmmRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x54);
  EmitOperand(dst, src);
}

void AssemblerX86::andpd(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x54);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::pextrd(GPRRegister dst, XmmRegister src,
                          const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x3A);
  EmitUint8(0x16);
  EmitOperand(src, Operand(dst));
  assert(imm.is_uint8());
  EmitUint8(imm.value());
}

void AssemblerX86::pmovsxdq(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x25);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::pcmpeqq(XmmRegister dst, XmmRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x38);
  EmitUint8(0x29);
  EmitXmmRegisterOperand(dst, src);
}

void AssemblerX86::roundsd(XmmRegister dst, XmmRegister src,
                           RoundingMode mode) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x66);
  EmitUint8(0x0F);
  EmitUint8(0x3A);
  EmitUint8(0x0B);
  EmitXmmRegisterOperand(dst, src);
  // Mask precision exeption.
  EmitUint8(static_cast<uint8_t>(mode) | 0x8);
}

void AssemblerX86::fldl(const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDD);
  EmitOperand(0, src);
}

void AssemblerX86::fstpl(const Address &dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDD);
  EmitOperand(3, dst);
}

void AssemblerX86::fnstcw(const Address &dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitOperand(7, dst);
}

void AssemblerX86::fldcw(const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitOperand(5, src);
}

void AssemblerX86::fistpl(const Address &dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDF);
  EmitOperand(7, dst);
}

void AssemblerX86::fistps(const Address &dst) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDB);
  EmitOperand(3, dst);
}

void AssemblerX86::fildl(const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDF);
  EmitOperand(5, src);
}

void AssemblerX86::filds(const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xDB);
  EmitOperand(0, src);
}

void AssemblerX86::fincstp() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xD9);
  EmitUint8(0xF7);
}

void AssemblerX86::cmpl(GPRRegister reg, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitComplex(7, Operand(reg), imm);
}

void AssemblerX86::cmpl(GPRRegister reg0, GPRRegister reg1) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x3B);
  EmitOperand(reg0, Operand(reg1));
}

void AssemblerX86::cmpl(GPRRegister reg, const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x3B);
  EmitOperand(reg, address);
}

void AssemblerX86::cmpl(const Address &address, GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x39);
  EmitOperand(reg, address);
}

void AssemblerX86::cmpl(const Address &address, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitComplex(7, address, imm);
}

void AssemblerX86::cmpb(const Address &address, const Immediate &imm) {
  assert(imm.is_int8());
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x80);
  EmitOperand(7, address);
  EmitUint8(imm.value() & 0xFF);
}

void AssemblerX86::testl(GPRRegister reg1, GPRRegister reg2) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x85);
  EmitRegisterOperand(reg1, reg2);
}

void AssemblerX86::testl(GPRRegister reg, const Immediate &immediate) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // For registers that have a byte variant (EAX, EBX, ECX, and EDX)
  // we only test the byte register to keep the encoding short.
  if (immediate.is_uint8() && reg < 4) {
    // Use zero-extended 8-bit immediate.
    if (reg == RegX8632::Encoded_Reg_eax) {
      EmitUint8(0xA8);
    } else {
      EmitUint8(0xF6);
      EmitUint8(0xC0 + reg);
    }
    EmitUint8(immediate.value() & 0xFF);
  } else if (reg == RegX8632::Encoded_Reg_eax) {
    // Use short form if the destination is EAX.
    EmitUint8(0xA9);
    EmitImmediate(immediate);
  } else {
    EmitUint8(0xF7);
    EmitOperand(0, Operand(reg));
    EmitImmediate(immediate);
  }
}

void AssemblerX86::And(Type Ty, GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x22);
  else
    EmitUint8(0x23);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::And(Type Ty, GPRRegister dst, const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x22);
  else
    EmitUint8(0x23);
  EmitOperand(dst, address);
}

void AssemblerX86::And(Type Ty, GPRRegister dst, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitComplexI8(4, Operand(dst), imm);
    return;
  }
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitComplex(4, Operand(dst), imm);
}

void AssemblerX86::Or(Type Ty, GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x0A);
  else
    EmitUint8(0x0B);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::Or(Type Ty, GPRRegister dst, const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x0A);
  else
    EmitUint8(0x0B);
  EmitOperand(dst, address);
}

void AssemblerX86::Or(Type Ty, GPRRegister dst, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitComplexI8(1, Operand(dst), imm);
    return;
  }
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitComplex(1, Operand(dst), imm);
}

void AssemblerX86::Xor(Type Ty, GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x32);
  else
    EmitUint8(0x33);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::Xor(Type Ty, GPRRegister dst, const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x32);
  else
    EmitUint8(0x33);
  EmitOperand(dst, address);
}

void AssemblerX86::Xor(Type Ty, GPRRegister dst, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitComplexI8(6, Operand(dst), imm);
    return;
  }
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitComplex(6, Operand(dst), imm);
}

void AssemblerX86::add(Type Ty, GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x02);
  else
    EmitUint8(0x03);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::add(Type Ty, GPRRegister reg, const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x02);
  else
    EmitUint8(0x03);
  EmitOperand(reg, address);
}

void AssemblerX86::add(Type Ty, GPRRegister reg, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitComplexI8(0, Operand(reg), imm);
    return;
  }
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitComplex(0, Operand(reg), imm);
}

void AssemblerX86::adc(Type Ty, GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x12);
  else
    EmitUint8(0x13);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::adc(Type Ty, GPRRegister dst, const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x12);
  else
    EmitUint8(0x13);
  EmitOperand(dst, address);
}

void AssemblerX86::adc(Type Ty, GPRRegister reg, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitComplexI8(2, Operand(reg), imm);
    return;
  }
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitComplex(2, Operand(reg), imm);
}

void AssemblerX86::sub(Type Ty, GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x2A);
  else
    EmitUint8(0x2B);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::sub(Type Ty, GPRRegister reg, const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x2A);
  else
    EmitUint8(0x2B);
  EmitOperand(reg, address);
}

void AssemblerX86::sub(Type Ty, GPRRegister reg, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitComplexI8(5, Operand(reg), imm);
    return;
  }
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitComplex(5, Operand(reg), imm);
}

void AssemblerX86::sbb(Type Ty, GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x1A);
  else
    EmitUint8(0x1B);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::sbb(Type Ty, GPRRegister dst, const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x1A);
  else
    EmitUint8(0x1B);
  EmitOperand(dst, address);
}

void AssemblerX86::sbb(Type Ty, GPRRegister reg, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i8 || Ty == IceType_i1) {
    EmitComplexI8(3, Operand(reg), imm);
    return;
  }
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitComplex(3, Operand(reg), imm);
}

void AssemblerX86::cbw() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  EmitUint8(0x98);
}

void AssemblerX86::cwd() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitOperandSizeOverride();
  EmitUint8(0x99);
}

void AssemblerX86::cdq() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x99);
}

void AssemblerX86::div(Type Ty, GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0xF6);
  else
    EmitUint8(0xF7);
  EmitRegisterOperand(6, reg);
}

void AssemblerX86::div(Type Ty, const Address &addr) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0xF6);
  else
    EmitUint8(0xF7);
  EmitOperand(6, addr);
}

void AssemblerX86::idiv(Type Ty, GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0xF6);
  else
    EmitUint8(0xF7);
  EmitRegisterOperand(7, reg);
}

void AssemblerX86::idiv(Type Ty, const Address &addr) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0xF6);
  else
    EmitUint8(0xF7);
  EmitOperand(7, addr);
}

void AssemblerX86::imull(GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xAF);
  EmitOperand(dst, Operand(src));
}

void AssemblerX86::imull(GPRRegister reg, const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x69);
  EmitOperand(reg, Operand(reg));
  EmitImmediate(imm);
}

void AssemblerX86::imull(GPRRegister reg, const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xAF);
  EmitOperand(reg, address);
}

void AssemblerX86::imull(GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF7);
  EmitOperand(5, Operand(reg));
}

void AssemblerX86::imull(const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF7);
  EmitOperand(5, address);
}

void AssemblerX86::mul(Type Ty, GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0xF6);
  else
    EmitUint8(0xF7);
  EmitRegisterOperand(4, reg);
}

void AssemblerX86::mul(Type Ty, const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0xF6);
  else
    EmitUint8(0xF7);
  EmitOperand(4, address);
}

void AssemblerX86::incl(GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x40 + reg);
}

void AssemblerX86::incl(const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xFF);
  EmitOperand(0, address);
}

void AssemblerX86::decl(GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x48 + reg);
}

void AssemblerX86::decl(const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xFF);
  EmitOperand(1, address);
}

void AssemblerX86::shll(GPRRegister reg, const Immediate &imm) {
  EmitGenericShift(4, reg, imm);
}

void AssemblerX86::shll(GPRRegister operand, GPRRegister shifter) {
  EmitGenericShift(4, Operand(operand), shifter);
}

void AssemblerX86::shll(const Address &operand, GPRRegister shifter) {
  EmitGenericShift(4, Operand(operand), shifter);
}

void AssemblerX86::shrl(GPRRegister reg, const Immediate &imm) {
  EmitGenericShift(5, reg, imm);
}

void AssemblerX86::shrl(GPRRegister operand, GPRRegister shifter) {
  EmitGenericShift(5, Operand(operand), shifter);
}

void AssemblerX86::sarl(GPRRegister reg, const Immediate &imm) {
  EmitGenericShift(7, reg, imm);
}

void AssemblerX86::sarl(GPRRegister operand, GPRRegister shifter) {
  EmitGenericShift(7, Operand(operand), shifter);
}

void AssemblerX86::sarl(const Address &address, GPRRegister shifter) {
  EmitGenericShift(7, Operand(address), shifter);
}

void AssemblerX86::shld(GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xA5);
  EmitRegisterOperand(src, dst);
}

void AssemblerX86::shld(GPRRegister dst, GPRRegister src,
                        const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  assert(imm.is_int8());
  EmitUint8(0x0F);
  EmitUint8(0xA4);
  EmitRegisterOperand(src, dst);
  EmitUint8(imm.value() & 0xFF);
}

void AssemblerX86::shld(const Address &operand, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xA5);
  EmitOperand(src, Operand(operand));
}

void AssemblerX86::shrd(GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xAD);
  EmitRegisterOperand(src, dst);
}

void AssemblerX86::shrd(GPRRegister dst, GPRRegister src,
                        const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  assert(imm.is_int8());
  EmitUint8(0x0F);
  EmitUint8(0xAC);
  EmitRegisterOperand(src, dst);
  EmitUint8(imm.value() & 0xFF);
}

void AssemblerX86::shrd(const Address &dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xAD);
  EmitOperand(src, Operand(dst));
}

void AssemblerX86::neg(Type Ty, GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0xF6);
  else
    EmitUint8(0xF7);
  EmitRegisterOperand(3, reg);
}

void AssemblerX86::neg(Type Ty, const Address &addr) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0xF6);
  else
    EmitUint8(0xF7);
  EmitOperand(3, addr);
}

void AssemblerX86::notl(GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF7);
  EmitUint8(0xD0 | reg);
}

void AssemblerX86::bswap(Type Ty, GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  assert(Ty == IceType_i32);
  EmitUint8(0x0F);
  EmitUint8(0xC8 | reg);
}

void AssemblerX86::bsf(Type Ty, GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  assert(Ty == IceType_i16 || Ty == IceType_i32);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitUint8(0x0F);
  EmitUint8(0xBC);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::bsf(Type Ty, GPRRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  assert(Ty == IceType_i16 || Ty == IceType_i32);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitUint8(0x0F);
  EmitUint8(0xBC);
  EmitOperand(dst, src);
}

void AssemblerX86::bsr(Type Ty, GPRRegister dst, GPRRegister src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  assert(Ty == IceType_i16 || Ty == IceType_i32);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitUint8(0x0F);
  EmitUint8(0xBD);
  EmitRegisterOperand(dst, src);
}

void AssemblerX86::bsr(Type Ty, GPRRegister dst, const Address &src) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  assert(Ty == IceType_i16 || Ty == IceType_i32);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitUint8(0x0F);
  EmitUint8(0xBD);
  EmitOperand(dst, src);
}

void AssemblerX86::bt(GPRRegister base, GPRRegister offset) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xA3);
  EmitRegisterOperand(offset, base);
}

void AssemblerX86::ret() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xC3);
}

void AssemblerX86::ret(const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xC2);
  assert(imm.is_uint16());
  EmitUint8(imm.value() & 0xFF);
  EmitUint8((imm.value() >> 8) & 0xFF);
}

void AssemblerX86::nop(int size) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  // There are nops up to size 15, but for now just provide up to size 8.
  assert(0 < size && size <= MAX_NOP_SIZE);
  switch (size) {
  case 1:
    EmitUint8(0x90);
    break;
  case 2:
    EmitUint8(0x66);
    EmitUint8(0x90);
    break;
  case 3:
    EmitUint8(0x0F);
    EmitUint8(0x1F);
    EmitUint8(0x00);
    break;
  case 4:
    EmitUint8(0x0F);
    EmitUint8(0x1F);
    EmitUint8(0x40);
    EmitUint8(0x00);
    break;
  case 5:
    EmitUint8(0x0F);
    EmitUint8(0x1F);
    EmitUint8(0x44);
    EmitUint8(0x00);
    EmitUint8(0x00);
    break;
  case 6:
    EmitUint8(0x66);
    EmitUint8(0x0F);
    EmitUint8(0x1F);
    EmitUint8(0x44);
    EmitUint8(0x00);
    EmitUint8(0x00);
    break;
  case 7:
    EmitUint8(0x0F);
    EmitUint8(0x1F);
    EmitUint8(0x80);
    EmitUint8(0x00);
    EmitUint8(0x00);
    EmitUint8(0x00);
    EmitUint8(0x00);
    break;
  case 8:
    EmitUint8(0x0F);
    EmitUint8(0x1F);
    EmitUint8(0x84);
    EmitUint8(0x00);
    EmitUint8(0x00);
    EmitUint8(0x00);
    EmitUint8(0x00);
    EmitUint8(0x00);
    break;
  default:
    llvm_unreachable("Unimplemented");
  }
}

void AssemblerX86::int3() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xCC);
}

void AssemblerX86::hlt() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF4);
}

void AssemblerX86::j(CondX86::BrCond condition, Label *label, bool near) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (label->IsBound()) {
    static const int kShortSize = 2;
    static const int kLongSize = 6;
    intptr_t offset = label->Position() - buffer_.Size();
    assert(offset <= 0);
    if (Utils::IsInt(8, offset - kShortSize)) {
      EmitUint8(0x70 + condition);
      EmitUint8((offset - kShortSize) & 0xFF);
    } else {
      EmitUint8(0x0F);
      EmitUint8(0x80 + condition);
      EmitInt32(offset - kLongSize);
    }
  } else if (near) {
    EmitUint8(0x70 + condition);
    EmitNearLabelLink(label);
  } else {
    EmitUint8(0x0F);
    EmitUint8(0x80 + condition);
    EmitLabelLink(label);
  }
}

void AssemblerX86::j(CondX86::BrCond condition,
                     const ConstantRelocatable *label) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0x80 + condition);
  EmitFixup(DirectCallRelocation::create(this, FK_PcRel_4, label));
  EmitInt32(-4);
}

void AssemblerX86::jmp(GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xFF);
  EmitRegisterOperand(4, reg);
}

void AssemblerX86::jmp(Label *label, bool near) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (label->IsBound()) {
    static const int kShortSize = 2;
    static const int kLongSize = 5;
    intptr_t offset = label->Position() - buffer_.Size();
    assert(offset <= 0);
    if (Utils::IsInt(8, offset - kShortSize)) {
      EmitUint8(0xEB);
      EmitUint8((offset - kShortSize) & 0xFF);
    } else {
      EmitUint8(0xE9);
      EmitInt32(offset - kLongSize);
    }
  } else if (near) {
    EmitUint8(0xEB);
    EmitNearLabelLink(label);
  } else {
    EmitUint8(0xE9);
    EmitLabelLink(label);
  }
}

void AssemblerX86::jmp(const ConstantRelocatable *label) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xE9);
  EmitFixup(DirectCallRelocation::create(this, FK_PcRel_4, label));
  EmitInt32(-4);
}

void AssemblerX86::mfence() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xAE);
  EmitUint8(0xF0);
}

void AssemblerX86::lock() {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0xF0);
}

void AssemblerX86::cmpxchg(Type Ty, const Address &address, GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitUint8(0x0F);
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0xB0);
  else
    EmitUint8(0xB1);
  EmitOperand(reg, address);
}

void AssemblerX86::cmpxchg8b(const Address &address) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  EmitUint8(0x0F);
  EmitUint8(0xC7);
  EmitOperand(1, address);
}

void AssemblerX86::xadd(Type Ty, const Address &addr, GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  EmitUint8(0x0F);
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0xC0);
  else
    EmitUint8(0xC1);
  EmitOperand(reg, addr);
}

void AssemblerX86::xchg(Type Ty, const Address &addr, GPRRegister reg) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  if (Ty == IceType_i16)
    EmitOperandSizeOverride();
  if (Ty == IceType_i8 || Ty == IceType_i1)
    EmitUint8(0x86);
  else
    EmitUint8(0x87);
  EmitOperand(reg, addr);
}

void AssemblerX86::Align(intptr_t alignment, intptr_t offset) {
  assert(llvm::isPowerOf2_32(alignment));
  intptr_t pos = offset + buffer_.GetPosition();
  intptr_t mod = pos & (alignment - 1);
  if (mod == 0) {
    return;
  }
  intptr_t bytes_needed = alignment - mod;
  while (bytes_needed > MAX_NOP_SIZE) {
    nop(MAX_NOP_SIZE);
    bytes_needed -= MAX_NOP_SIZE;
  }
  if (bytes_needed) {
    nop(bytes_needed);
  }
  assert(((offset + buffer_.GetPosition()) & (alignment - 1)) == 0);
}

void AssemblerX86::Bind(Label *label) {
  intptr_t bound = buffer_.Size();
  assert(!label->IsBound()); // Labels can only be bound once.
  while (label->IsLinked()) {
    intptr_t position = label->LinkPosition();
    intptr_t next = buffer_.Load<int32_t>(position);
    buffer_.Store<int32_t>(position, bound - (position + 4));
    label->position_ = next;
  }
  while (label->HasNear()) {
    intptr_t position = label->NearPosition();
    intptr_t offset = bound - (position + 1);
    assert(Utils::IsInt(8, offset));
    buffer_.Store<int8_t>(position, offset);
  }
  label->BindTo(bound);
}

void AssemblerX86::EmitOperand(int rm, const Operand &operand) {
  assert(rm >= 0 && rm < 8);
  const intptr_t length = operand.length_;
  assert(length > 0);
  // Emit the ModRM byte updated with the given RM value.
  assert((operand.encoding_[0] & 0x38) == 0);
  EmitUint8(operand.encoding_[0] + (rm << 3));
  if (operand.fixup()) {
    EmitFixup(operand.fixup());
  }
  // Emit the rest of the encoded operand.
  for (intptr_t i = 1; i < length; i++) {
    EmitUint8(operand.encoding_[i]);
  }
}

void AssemblerX86::EmitImmediate(const Immediate &imm) {
  EmitInt32(imm.value());
}

void AssemblerX86::EmitComplexI8(int rm, const Operand &operand,
                                 const Immediate &immediate) {
  assert(rm >= 0 && rm < 8);
  assert(immediate.is_int8());
  if (operand.IsRegister(RegX8632::Encoded_Reg_eax)) {
    // Use short form if the destination is al.
    EmitUint8(0x04 + (rm << 3));
    EmitUint8(immediate.value() & 0xFF);
  } else {
    // Use sign-extended 8-bit immediate.
    EmitUint8(0x80);
    EmitOperand(rm, operand);
    EmitUint8(immediate.value() & 0xFF);
  }
}

void AssemblerX86::EmitComplex(int rm, const Operand &operand,
                               const Immediate &immediate) {
  assert(rm >= 0 && rm < 8);
  if (immediate.is_int8()) {
    // Use sign-extended 8-bit immediate.
    EmitUint8(0x83);
    EmitOperand(rm, operand);
    EmitUint8(immediate.value() & 0xFF);
  } else if (operand.IsRegister(RegX8632::Encoded_Reg_eax)) {
    // Use short form if the destination is eax.
    EmitUint8(0x05 + (rm << 3));
    EmitImmediate(immediate);
  } else {
    EmitUint8(0x81);
    EmitOperand(rm, operand);
    EmitImmediate(immediate);
  }
}

void AssemblerX86::EmitLabel(Label *label, intptr_t instruction_size) {
  if (label->IsBound()) {
    intptr_t offset = label->Position() - buffer_.Size();
    assert(offset <= 0);
    EmitInt32(offset - instruction_size);
  } else {
    EmitLabelLink(label);
  }
}

void AssemblerX86::EmitLabelLink(Label *label) {
  assert(!label->IsBound());
  intptr_t position = buffer_.Size();
  EmitInt32(label->position_);
  label->LinkTo(position);
}

void AssemblerX86::EmitNearLabelLink(Label *label) {
  assert(!label->IsBound());
  intptr_t position = buffer_.Size();
  EmitUint8(0);
  label->NearLinkTo(position);
}

void AssemblerX86::EmitGenericShift(int rm, GPRRegister reg,
                                    const Immediate &imm) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  assert(imm.is_int8());
  if (imm.value() == 1) {
    EmitUint8(0xD1);
    EmitOperand(rm, Operand(reg));
  } else {
    EmitUint8(0xC1);
    EmitOperand(rm, Operand(reg));
    EmitUint8(imm.value() & 0xFF);
  }
}

void AssemblerX86::EmitGenericShift(int rm, const Operand &operand,
                                    GPRRegister shifter) {
  AssemblerBuffer::EnsureCapacity ensured(&buffer_);
  assert(shifter == RegX8632::Encoded_Reg_ecx);
  EmitUint8(0xD3);
  EmitOperand(rm, Operand(operand));
}

} // end of namespace x86
} // end of namespace Ice
