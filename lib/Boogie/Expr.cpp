#include "bugle/Expr.h"

using namespace bugle;

ref<Expr> BVConstExpr::create(const llvm::APInt &bv) {
  return new BVConstExpr(bv);
}

ref<Expr> BVConstExpr::createZero(unsigned width) {
  return create(llvm::APInt(width, 0));
}

ref<Expr> BVConstExpr::create(unsigned width, uint64_t val, bool isSigned) {
  return create(llvm::APInt(width, val, isSigned));
}

ref<Expr> BoolConstExpr::create(bool val) {
  return new BoolConstExpr(val);
}

ref<Expr> GlobalArrayRefExpr::create(GlobalArray *global) {
  return new GlobalArrayRefExpr(global);
}

ref<Expr> PointerExpr::create(ref<Expr> array, ref<Expr> offset) {
  assert(array->getType().kind == Type::ArrayId);
  assert(offset->getType().kind == Type::BV);

  return new PointerExpr(array, offset);
}

ref<Expr> LoadExpr::create(ref<Expr> array, ref<Expr> offset) {
  assert(array->getType().kind == Type::ArrayId);
  assert(offset->getType().kind == Type::BV);

  return new LoadExpr(array, offset);
}

ref<Expr> VarRefExpr::create(Var *var) {
  return new VarRefExpr(var);
}

ref<Expr> BVExtractExpr::create(ref<Expr> expr, unsigned offset,
                                unsigned width) {
  if (auto e = dyn_cast<BVConstExpr>(expr))
    return BVConstExpr::create(e->getValue().ashr(offset).zextOrTrunc(width));

  return new BVExtractExpr(expr, offset, width);
}

ref<Expr> NotExpr::create(ref<Expr> op) {
  assert(op->getType().kind == Type::Bool);
  if (auto e = dyn_cast<BoolConstExpr>(op))
    return BoolConstExpr::create(!e->getValue());

  return new NotExpr(Type(Type::Bool), op);
}

ref<Expr> ArrayIdExpr::create(ref<Expr> pointer) {
  assert(pointer->getType().kind == Type::Pointer);
  if (auto e = dyn_cast<PointerExpr>(pointer))
    return e->getArray();

  return new ArrayIdExpr(Type(Type::ArrayId), pointer);
}

ref<Expr> ArrayOffsetExpr::create(ref<Expr> pointer) {
  assert(pointer->getType().kind == Type::Pointer);

  if (auto e = dyn_cast<PointerExpr>(pointer))
    return e->getOffset();

  return new ArrayOffsetExpr(Type(Type::BV, pointer->getType().width), pointer);
}

ref<Expr> BVZExtExpr::create(unsigned width, ref<Expr> bv) {
  const Type &ty = bv->getType();
  assert(ty.kind == Type::BV);

  if (width == ty.width)
    return bv;
  if (width < ty.width)
    return BVExtractExpr::create(bv, 0, width);

  if (auto e = dyn_cast<BVConstExpr>(bv))
    return BVConstExpr::create(e->getValue().zext(width));

  return new BVZExtExpr(width, bv);
}

ref<Expr> BVSExtExpr::create(unsigned width, ref<Expr> bv) {
  const Type &ty = bv->getType();
  assert(ty.kind == Type::BV);

  if (width == ty.width)
    return bv;
  if (width < ty.width)
    return BVExtractExpr::create(bv, 0, width);

  if (auto e = dyn_cast<BVConstExpr>(bv))
    return BVConstExpr::create(e->getValue().sext(width));

  return new BVSExtExpr(width, bv);
}

ref<Expr> BVToFloatExpr::create(ref<Expr> bv) {
  const Type &ty = bv->getType();
  assert(ty.kind == Type::BV);
  assert(ty.width == 32 || ty.width == 64);

  if (auto e = dyn_cast<FloatToBVExpr>(bv))
    return e->getSubExpr();

  return new BVToFloatExpr(Type(Type::Float, ty.width), bv);
}

ref<Expr> FloatToBVExpr::create(ref<Expr> bv) {
  const Type &ty = bv->getType();
  assert(ty.kind == Type::Float);

  if (auto e = dyn_cast<BVToFloatExpr>(bv))
    return e->getSubExpr();

  return new FloatToBVExpr(Type(Type::BV, ty.width), bv);
}

ref<Expr> BVToPtrExpr::create(ref<Expr> bv) {
  const Type &ty = bv->getType();
  assert(ty.kind == Type::BV);

  if (auto e = dyn_cast<PtrToBVExpr>(bv))
    return e->getSubExpr();

  return new BVToPtrExpr(Type(Type::Pointer, ty.width), bv);
}

ref<Expr> PtrToBVExpr::create(ref<Expr> bv) {
  const Type &ty = bv->getType();
  assert(ty.kind == Type::Pointer);

  if (auto e = dyn_cast<BVToPtrExpr>(bv))
    return e->getSubExpr();

  return new PtrToBVExpr(Type(Type::BV, ty.width), bv);
}

ref<Expr> BVToBoolExpr::create(ref<Expr> bv) {
  const Type &ty = bv->getType();
  assert(ty.kind == Type::BV);
  assert(ty.width == 1);

  if (auto e = dyn_cast<BoolToBVExpr>(bv))
    return e->getSubExpr();

  return new BVToBoolExpr(Type(Type::Bool), bv);
}

ref<Expr> BoolToBVExpr::create(ref<Expr> bv) {
  const Type &ty = bv->getType();
  assert(ty.kind == Type::Bool);

  if (auto e = dyn_cast<BVToBoolExpr>(bv))
    return e->getSubExpr();

  return new BoolToBVExpr(Type(Type::BV, 1), bv);
}

ref<Expr> BVAddExpr::create(ref<Expr> lhs, ref<Expr> rhs) {
  auto &lhsTy = lhs->getType(), &rhsTy = rhs->getType();
  assert(lhsTy.kind == Type::BV && rhsTy.kind == Type::BV);
  assert(lhsTy.width == rhsTy.width);

  if (auto e1 = dyn_cast<BVConstExpr>(lhs))
    if (auto e2 = dyn_cast<BVConstExpr>(rhs))
      return BVConstExpr::create(e1->getValue() + e2->getValue());

  return new BVAddExpr(Type(Type::BV, lhsTy.width), lhs, rhs);
}

ref<Expr> BVSubExpr::create(ref<Expr> lhs, ref<Expr> rhs) {
  auto &lhsTy = lhs->getType(), &rhsTy = rhs->getType();
  assert(lhsTy.kind == Type::BV && rhsTy.kind == Type::BV);
  assert(lhsTy.width == rhsTy.width);

  if (auto e1 = dyn_cast<BVConstExpr>(lhs))
    if (auto e2 = dyn_cast<BVConstExpr>(rhs))
      return BVConstExpr::create(e1->getValue() - e2->getValue());

  return new BVSubExpr(Type(Type::BV, lhsTy.width), lhs, rhs);
}

ref<Expr> BVMulExpr::create(ref<Expr> lhs, ref<Expr> rhs) {
  auto &lhsTy = lhs->getType(), &rhsTy = rhs->getType();
  assert(lhsTy.kind == Type::BV && rhsTy.kind == Type::BV);
  assert(lhsTy.width == rhsTy.width);

  if (auto e1 = dyn_cast<BVConstExpr>(lhs))
    if (auto e2 = dyn_cast<BVConstExpr>(rhs))
      return BVConstExpr::create(e1->getValue() * e2->getValue());

  return new BVMulExpr(Type(Type::BV, lhsTy.width), lhs, rhs);
}

ref<Expr> BVSDivExpr::create(ref<Expr> lhs, ref<Expr> rhs) {
  auto &lhsTy = lhs->getType(), &rhsTy = rhs->getType();
  assert(lhsTy.kind == Type::BV && rhsTy.kind == Type::BV);
  assert(lhsTy.width == rhsTy.width);

  if (auto e1 = dyn_cast<BVConstExpr>(lhs))
    if (auto e2 = dyn_cast<BVConstExpr>(rhs))
      return BVConstExpr::create(e1->getValue().sdiv(e2->getValue()));

  return new BVSDivExpr(Type(Type::BV, lhsTy.width), lhs, rhs);
}

ref<Expr> BVUDivExpr::create(ref<Expr> lhs, ref<Expr> rhs) {
  auto &lhsTy = lhs->getType(), &rhsTy = rhs->getType();
  assert(lhsTy.kind == Type::BV && rhsTy.kind == Type::BV);
  assert(lhsTy.width == rhsTy.width);

  if (auto e1 = dyn_cast<BVConstExpr>(lhs))
    if (auto e2 = dyn_cast<BVConstExpr>(rhs))
      return BVConstExpr::create(e1->getValue().udiv(e2->getValue()));

  return new BVUDivExpr(Type(Type::BV, lhsTy.width), lhs, rhs);
}

ref<Expr> BVConcatExpr::create(ref<Expr> lhs, ref<Expr> rhs) {
  auto &lhsTy = lhs->getType(), &rhsTy = rhs->getType();
  assert(lhsTy.kind == Type::BV && rhsTy.kind == Type::BV);

  unsigned resWidth = lhsTy.width + rhsTy.width;
  if (auto e1 = dyn_cast<BVConstExpr>(lhs))
    if (auto e2 = dyn_cast<BVConstExpr>(rhs)) {
      llvm::APInt Tmp = e1->getValue().zext(resWidth);
      Tmp <<= rhsTy.width;
      Tmp |= e2->getValue().zext(resWidth);
      return BVConstExpr::create(Tmp);
    }

  return new BVConcatExpr(Type(Type::BV, resWidth), lhs, rhs);
}

ref<Expr> BVSgtExpr::create(ref<Expr> lhs, ref<Expr> rhs) {
  auto &lhsTy = lhs->getType(), &rhsTy = rhs->getType();
  assert(lhsTy.kind == Type::BV && rhsTy.kind == Type::BV);
  assert(lhsTy.width == rhsTy.width);

  if (auto e1 = dyn_cast<BVConstExpr>(lhs))
    if (auto e2 = dyn_cast<BVConstExpr>(rhs))
      return BoolConstExpr::create(e1->getValue().sgt(e2->getValue()));

  return new BVSgtExpr(Type(Type::Bool), lhs, rhs);
}
