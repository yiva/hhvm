/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010-2016 Facebook, Inc. (http://www.facebook.com)     |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#ifndef incl_HPHP_JIT_VASM_VISIT_H_
#define incl_HPHP_JIT_VASM_VISIT_H_

#include "hphp/runtime/vm/jit/containers.h"
#include "hphp/runtime/vm/jit/phys-reg.h"
#include "hphp/runtime/vm/jit/vasm.h"
#include "hphp/runtime/vm/jit/vasm-instr.h"
#include "hphp/runtime/vm/jit/vasm-reg.h"
#include "hphp/runtime/vm/jit/vasm-unit.h"

#include "hphp/util/type-traits.h"

#include <boost/dynamic_bitset.hpp>

#include <utility>

namespace HPHP { namespace jit {

///////////////////////////////////////////////////////////////////////////////

namespace detail {

template<class F, class A1, class A2>
auto invoke(F&& f, A1&& a1, A2&& a2)
  -> decltype(std::forward<F>(f)(std::forward<A1>(a1))) {
  return std::forward<F>(f)(std::forward<A1>(a1));
}

template<class F, class A1, class A2>
auto invoke(F&& f, A1&& a1, A2&& a2)
  -> decltype(std::forward<F>(f)(std::forward<A1>(a1), std::forward<A2>(a2))) {
  return std::forward<F>(f)(std::forward<A1>(a1), std::forward<A2>(a2));
}

}

///////////////////////////////////////////////////////////////////////////////

template<class R, class F>
typename std::enable_if<
  is_any<
    R,
    Vreg, VregSF, Vreg8, Vreg16, Vreg32, Vreg64, Vreg128, VregDbl, PhysReg
  >::value
>::type
visit(const Vunit&, R v, F f) {
  detail::invoke(f, v, width(v));
}

template<class F>
void visit(const Vunit&, Vptr p, F f) {
  if (p.base.isValid()) detail::invoke(f, p.base, width(p.base));
  if (p.index.isValid()) detail::invoke(f, p.index, width(p.index));
}

template<class F>
void visit(const Vunit& unit, Vtuple t, F f) {
  for (auto r : unit.tuples[t]) detail::invoke(f, r, width(r));
}

template<class F>
void visit(const Vunit& unit, VcallArgsId a, F f) {
  auto& args = unit.vcallArgs[a];
  for (auto r : args.args) detail::invoke(f, r, width(r));
  for (auto r : args.simdArgs) detail::invoke(f, r, width(r));
  for (auto r : args.stkArgs) detail::invoke(f, r, width(r));
}

template<class F>
void visit(const Vunit& unit, RegSet regs, F f) {
  regs.forEach([&](Vreg r) { detail::invoke(f, r, width(r)); });
}

///////////////////////////////////////////////////////////////////////////////

template<class Use>
void visitUses(const Vunit& unit, const Vinstr& inst, Use use) {
  switch (inst.op) {
#define O(name, imms, uses, defs) \
    case Vinstr::name: { \
      auto& i = inst.name##_; (void)i; \
      uses \
      break; \
    }
#define U(s) visit(unit, i.s, use);
#define UA(s) visit(unit, i.s, use);
#define UH(s,h) visit(unit, i.s, use);
#define Un
    VASM_OPCODES
#undef Un
#undef UH
#undef UA
#undef U
#undef O
  }
}

template<class Def>
void visitDefs(const Vunit& unit, const Vinstr& inst, Def def) {
  switch (inst.op) {
#define O(name, imms, uses, defs) \
    case Vinstr::name: { \
      auto& i = inst.name##_; (void)i; \
      defs \
      break; \
    }
#define D(d) visit(unit, i.d, def);
#define DH(d,h) visit(unit, i.d, def);
#define Dn
    VASM_OPCODES
#undef Dn
#undef DH
#undef D
#undef O
  }
}

/*
 * visitOperands visits all operands of the given instruction, calling
 * visitor.imm(), visitor.use(), visitor.across(), and visitor.def() as defined
 * in the VASM_OPCODES macro.
 *
 * The template spew is necessary to support callers that only have a const
 * Vinstr& as well as callers with a Vinstr& that wish to mutate the
 * instruction in the visitor.
 */
template<class Tinstr, class Visitor>
typename maybe_const<Tinstr, Vinstr>::type
visitOperands(Tinstr& inst, Visitor& visitor) {
  switch (inst.op) {
#define O(name, imms, uses, defs) \
    case Vinstr::name: { \
      auto& i = inst.name##_; (void)i; \
      imms \
      uses \
      defs \
      break; \
    }
#define I(f) visitor.imm(i.f);
#define U(s) visitor.use(i.s);
#define UA(s) visitor.across(i.s);
#define UH(s,h) visitor.useHint(i.s, i.h);
#define D(d) visitor.def(i.d);
#define DH(d,h) visitor.defHint(i.d, i.h);
#define Inone
#define Un
#define Dn
    VASM_OPCODES
#undef Dn
#undef Un
#undef Inone
#undef DH
#undef D
#undef UH
#undef UA
#undef U
#undef I
#undef O
  }
}

///////////////////////////////////////////////////////////////////////////////

/*
 * Visit reachable blocks, calling `pre' and `post' on each one.
 */
struct DfsWalker {
  explicit DfsWalker(const Vunit& u)
    : unit(u)
    , visited(u.blocks.size())
  {}

  template<class Pre, class Post>
  void dfs(Vlabel b, Pre pre, Post post) {
    if (visited.test(b)) return;
    visited.set(b);

    pre(b);
    for (auto s : succs(unit.blocks[b])) {
      dfs(s, pre, post);
    }
    post(b);
  }

  template<class Pre, class Post>
  void dfs(Pre pre, Post post) {
    dfs(unit.entry, pre, post);
  }

private:
  const Vunit& unit;
  boost::dynamic_bitset<> visited;
};

/*
 * Visit reachable blocks in postorder, calling `fn' on each one.
 *
 * Guaranteed not to use standard iterators on u.blocks, because several passes
 * (e.g., vlower) forbid it.
 */
struct PostorderWalker {
  explicit PostorderWalker(const Vunit& u) : m_dfs{u} {}

  template<class Post>
  void dfs(Post post) {
    m_dfs.dfs([](Vlabel){}, post);
  }

private:
  DfsWalker m_dfs;
};

using PredVector = jit::vector<jit::vector<Vlabel>>;
PredVector computePreds(const Vunit& unit);

///////////////////////////////////////////////////////////////////////////////

}}

#endif
