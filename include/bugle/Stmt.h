#include "bugle/Expr.h"
#include "bugle/Ref.h"

#ifndef BUGLE_STMT_H
#define BUGLE_STMT_H

namespace bugle {

class BasicBlock;
class Expr;
class Var;

class Stmt {
public:
  enum Kind {
    Eval,
    Store,
    VarAssign,
    Goto,
    Return,
    Assume,
    Call
  };

  virtual ~Stmt() {}
  virtual Kind getKind() const = 0;
};

#define STMT_KIND(kind) \
  Kind getKind() const { return kind; } \
  static bool classof(const Stmt *S) { return S->getKind() == kind; } \
  static bool classof(const kind##Stmt *) { return true; }

class EvalStmt : public Stmt {
  ref<Expr> expr;

public:
  EvalStmt(ref<Expr> expr) : expr(expr) {}

  STMT_KIND(Eval)
  ref<Expr> getExpr() const { return expr; }
};

class StoreStmt : public Stmt {
  ref<Expr> array;
  ref<Expr> offset;
  ref<Expr> value;

public:
  StoreStmt(ref<Expr> array, ref<Expr> offset, ref<Expr> value) :
    array(array), offset(offset), value(value) {}

  STMT_KIND(Store)
  ref<Expr> getArray() const { return array; }
  ref<Expr> getOffset() const { return offset; }
  ref<Expr> getValue() const { return value; }
};

class VarAssignStmt : public Stmt {
  Var *var;
  ref<Expr> value;

public:
  VarAssignStmt(Var *var, ref<Expr> value) : var(var), value(value) {}

  STMT_KIND(VarAssign)
  Var *getVar() const { return var; }
  ref<Expr> getValue() const { return value; }
};

class GotoStmt : public Stmt {
  std::vector<BasicBlock *> blocks;

public:
  GotoStmt(const std::vector<BasicBlock *> &blocks) : blocks(blocks) {}
  GotoStmt(BasicBlock *block) : blocks(1, block) {}
  STMT_KIND(Goto)
  const std::vector<BasicBlock *> &getBlocks() { return blocks; }
};

class ReturnStmt : public Stmt {
public:
  ReturnStmt() {}
  STMT_KIND(Return)
};

class AssumeStmt : public Stmt {
  ref<Expr> pred;

public:
  AssumeStmt(ref<Expr> pred) : pred(pred) {}

  STMT_KIND(Assume)
  ref<Expr> getPredicate() const { return pred; }
};

class CallStmt : public Stmt {
  Function *callee;
  std::vector<ref<Expr>> args;

public:
  CallStmt(Function *callee, const std::vector<ref<Expr>> &args) :
    callee(callee), args(args) {}

  STMT_KIND(Call)
  Function *getCallee() const { return callee; }
  const std::vector<ref<Expr>> &getArgs() const { return args; }
};

}

#endif
