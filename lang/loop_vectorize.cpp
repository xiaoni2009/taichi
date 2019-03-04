#include "ir.h"

TLANG_NAMESPACE_BEGIN

// Lower Expr tree to a bunch of binary/unary(binary/unary) statements
// Goal: eliminate Expression, and mutable local variables. Make AST SSA.
class LoopVectorize : public IRVisitor {
 public:
  int vectorize;
  Stmt *loop_var;  // an alloca...

  LoopVectorize() {
    allow_undefined_visitor = true;
    invoke_default_visitor = true;
    loop_var = nullptr;
    vectorize = 1;
  }

  void visit(Statement *stmt) {
    stmt->ret_type.width *= vectorize;
  }

  void visit(ConstStmt *stmt) {
    stmt->val.repeat(vectorize);
    stmt->ret_type.width *= vectorize;
  }

  void visit(Block *stmt_list) {
    std::vector<Stmt *> statements;
    for (auto &stmt : stmt_list->statements) {
      statements.push_back(stmt.get());
    }
    for (auto stmt : statements) {
      stmt->accept(this);
    }
  }

  void visit(GlobalPtrStmt *ptr) {
    ptr->snode.repeat(vectorize);
    ptr->width() *= vectorize;
  }

  void visit(AllocaStmt *alloca) {
    alloca->ret_type.width *= vectorize;
  }

  void visit(LocalLoadStmt *stmt) {
    if (vectorize == 1)
      return;
    int original_width = stmt->width();
    stmt->ret_type.width *= vectorize;
    stmt->ptr.repeat(vectorize);
    if (stmt->ptr[0].var->width() != 1) {
      for (int j = 0; j < original_width; j++) {
        TC_ASSERT(stmt->ptr[j].offset == 0);
      }
      for (int i = 1; i < vectorize; i++) {
        for (int j = 0; j < original_width; j++) {
          stmt->ptr[i * original_width + j].offset = i;
        }
      }
    }
    if (loop_var && stmt->same_source() && stmt->ptr[0].var == loop_var) {
      // insert_before_me
      LaneAttribute<TypedConstant> const_offsets;
      const_offsets.resize(vectorize * original_width);
      for (int i = 0; i < vectorize * original_width; i++) {
        const_offsets[i] = TypedConstant(i / original_width);
      }
      auto offsets = std::make_unique<ConstStmt>(const_offsets);
      auto add_op =
          std::make_unique<BinaryOpStmt>(BinaryType::add, stmt, offsets.get());
      irpass::typecheck(add_op.get());
      auto offsets_p = offsets.get();
      stmt->replace_with(add_op.get());
      stmt->insert_after_me(std::move(offsets));
      offsets_p->insert_after_me(std::move(add_op));
    }
  }

  void visit(IfStmt *if_stmt) override {
    if (if_stmt->true_statements)
      if_stmt->true_statements->accept(this);
    if (if_stmt->false_statements) {
      if_stmt->false_statements->accept(this);
    }
  }

  void visit(RangeForStmt *for_stmt) {
    auto old_vectorize = for_stmt->vectorize;
    vectorize = for_stmt->vectorize;
    loop_var = for_stmt->loop_var;
    for_stmt->body->accept(this);
    loop_var = nullptr;
    vectorize = old_vectorize;
  }

  void visit(WhileStmt *stmt) {
    stmt->body->accept(this);
  }

  static void run(IRNode *node) {
    LoopVectorize inst;
    node->accept(&inst);
  }
};

namespace irpass {

void loop_vectorize(IRNode *root) {
  return LoopVectorize::run(root);
}

}  // namespace irpass

TLANG_NAMESPACE_END