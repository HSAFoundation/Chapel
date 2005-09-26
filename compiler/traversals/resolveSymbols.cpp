#include <typeinfo>
#include "resolveSymbols.h"
#include "analysis.h"
#include "chplalloc.h"
#include "expr.h"
#include "stmt.h"
#include "symbol.h"
#include "symtab.h"
#include "type.h"
#include "stringutil.h"


static AList<Expr>* copy_argument_list(CallExpr* expr) {
  AList<Expr>* args = new AList<Expr>();
  MemberAccess* member_access = dynamic_cast<MemberAccess*>(expr->baseExpr);
  if (member_access) {
    args->insertAtTail(member_access->base->copy());
  }
  args->insertAtTail(expr->argList->copy());
  return args;
}


ResolveSymbols::ResolveSymbols() {
  //  whichModules = MODULES_CODEGEN;
}


OpTag gets_to_op(OpTag i) {
  switch (i) {
    case OP_GETSPLUS: return OP_PLUS;
    case OP_GETSMINUS: return OP_MINUS;
    case OP_GETSMULT: return OP_MULT;
    case OP_GETSDIV: return OP_DIV;
    case OP_GETSBITAND: return OP_BITAND;
    case OP_GETSBITOR: return OP_BITOR;
    case OP_GETSBITXOR: return OP_BITXOR;
    default: 
      INT_FATAL("Unable to convert OpTag");
  }
  return OP_PLUS;
}


static int
is_builtin(FnSymbol *fn) {
  if (fn->hasPragma("builtin")) {
    return 1;
  }
  return 0;
}


static void mangle_overloaded_operator_function_names(FnSymbol* fn) {
  static int uid = 0;

  if (is_builtin(fn)) {
    return;
  }

  if (!strcmp(fn->name, "=")) {
    fn->cname = stringcat("_assign", intstring(uid++));
  }
  else if (!strcmp(fn->name, "+")) {
    fn->cname = stringcat("_plus", intstring(uid++));
  }
  else if (!strcmp(fn->name, "#")) {
    fn->cname = stringcat("_pound", intstring(uid++));
  }
  else if (!strcmp(fn->name, "-")) {
    fn->cname = stringcat("_minus", intstring(uid++));
  }
  else if (!strcmp(fn->name, "*")) {
    fn->cname = stringcat("_times", intstring(uid++));
  }
  else if (!strcmp(fn->name, "/")) {
    fn->cname = stringcat("_div", intstring(uid++));
  }
  else if (!strcmp(fn->name, "mod")) {
    fn->cname = stringcat("_mod", intstring(uid++));
  }
  else if (!strcmp(fn->name, "==")) {
    fn->cname = stringcat("_eq", intstring(uid++));
  }
  else if (!strcmp(fn->name, "!=")) {
    fn->cname = stringcat("_ne", intstring(uid++));
  }
  else if (!strcmp(fn->name, "<=")) {
    fn->cname = stringcat("_le", intstring(uid++));
  }
  else if (!strcmp(fn->name, ">=")) {
    fn->cname = stringcat("_ge", intstring(uid++));
  }
  else if (!strcmp(fn->name, "<")) {
    fn->cname = stringcat("_lt", intstring(uid++));
  }
  else if (!strcmp(fn->name, ">")) {
    fn->cname = stringcat("_gt", intstring(uid++));
  }
  else if (!strcmp(fn->name, "&")) {
    fn->cname = stringcat("_bitand", intstring(uid++));
  }
  else if (!strcmp(fn->name, "|")) {
    fn->cname = stringcat("_bitor", intstring(uid++));
  }
  else if (!strcmp(fn->name, "^")) {
    fn->cname = stringcat("_xor", intstring(uid++));
  }
  else if (!strcmp(fn->name, "and")) {
    fn->cname = stringcat("_and", intstring(uid++));
  }
  else if (!strcmp(fn->name, "or")) {
    fn->cname = stringcat("_or", intstring(uid++));
  }
  else if (!strcmp(fn->name, "**")) {
    fn->cname = stringcat("_exponent", intstring(uid++));
  }
  else if (!strcmp(fn->name, "by")) {
    fn->cname = stringcat("_by", intstring(uid++));
  } else if (*fn->name == '=') {
    fn->cname = stringcat("_assign", intstring(uid++), "_", fn->name+1);
  }
}


static void
mangle_overloaded_operator_function_names(Expr *expr) {
  if (DefExpr* def_expr = dynamic_cast<DefExpr*>(expr)) {
    if (FnSymbol* fn = dynamic_cast<FnSymbol*>(def_expr->sym)) {
      mangle_overloaded_operator_function_names(fn);
    }
  }
}


static Expr *
resolve_binary_operator(CallExpr *op, FnSymbol *resolved = 0) {
  Expr *expr = op;
  Vec<FnSymbol*> fns;
  if (resolved)
    fns.add(resolved);
  else
    call_info(expr, fns);
  if (fns.n != 1) {
    if (fns.n == 0) {
      return expr;
    } else {
      INT_FATAL(expr, "Trouble resolving operator");
    }
  } else {
    if (fns.v[0]->hasPragma("builtin")) {
      return expr;
    }
    CallExpr *new_expr = new CallExpr(fns.v[0], op->copy());
    expr->replace(new_expr);
    expr = new_expr;
  }
  return expr;
}

void ResolveSymbols::postProcessExpr(Expr* expr) {

  // Resolve CallExprs
  if (CallExpr* paren = dynamic_cast<CallExpr*>(expr)) {
    if (paren->opTag < OP_GETSNORM) {
      if (SymExpr* variable = dynamic_cast<SymExpr*>(paren->baseExpr)) {
        if (!strcmp(variable->var->name, "__primitive")) {
          return;
        }
      }
      CallExpr *assign = dynamic_cast<CallExpr*>(paren->parentExpr);
      if (!assign || assign->opTag < OP_GETSNORM ||  assign->get(1) != expr) {
        Vec<FnSymbol*> fns;
        call_info(paren, fns);
        if (fns.n == 0) { // for 0-ary (CallExpr(MemberAccess))
          call_info(paren->baseExpr, fns);
        }
        if (fns.n != 1) {
          // HACK: Special case where write(:nilType) requires dynamic
          // dispatch; Take the other one.
          if (fns.n == 2 && !strcmp(fns.v[1]->name, "write") &&
              fns.v[1]->formals->only()->sym->type == dtNil) {
          } else if (fns.n == 2 && !strcmp(fns.v[0]->name, "write") &&
                     fns.v[0]->formals->only()->sym->type == dtNil) {
            fns.v[0] = fns.v[1];
          } else {
            if (OP_ISUNARYOP(paren->opTag)) {
              return;
            }
            if (OP_ISBINARYOP(paren->opTag)) {
              return;
            }
            if (paren->partialTag != PARTIAL_NEVER)
              return;
            USR_WARNING(expr, "It looks like this program requires dynamic"
                        "dispatch, which is not yet supported");
            INT_FATAL(expr, "Unable to resolve function");
            return;
          }
        }

        AList<Expr>* arguments = copy_argument_list(paren);
        // HACK: to handle special case for a.x(1) translation
        Expr *baseExpr = paren->baseExpr;
        if (CallExpr* basecall = dynamic_cast<CallExpr*>(paren->baseExpr)) {
          if (basecall->partialTag != PARTIAL_NEVER) {
            Vec<FnSymbol*> fns;
            call_info(basecall, fns);
            if (fns.n == 0) { // for 0-ary (CallExpr(MemberAccess))
              call_info(paren->baseExpr, fns);
            }
            if (fns.n == 0) {
              baseExpr = basecall->baseExpr;
              arguments->insertAtHead(basecall->argList->copy());
            }
          }
        }
        if (!strcmp("this", fns.v[0]->name)) {
          arguments->insertAtHead(baseExpr->copy());
        }
        CallExpr *new_expr = new CallExpr(fns.v[0], arguments);
        if (fns.v[0]->hasPragma("builtin")) {
          new_expr->opTag = paren->opTag;
        }
        expr->replace(new_expr);
        expr = new_expr;
      }
    }
  }

  // Resolve AssignOp to members or setter functions
  if (CallExpr* aop = dynamic_cast<CallExpr*>(expr)) {
    if (aop->opTag >= OP_GETSNORM) {
      if (SymExpr* var = dynamic_cast<SymExpr*>(aop->get(1))) {
        Vec<FnSymbol*> fns;
        call_info(aop, fns);
        int notbuiltin = 0;
        forv_Vec(FnSymbol, f, fns) {
          if (!is_builtin(fns.v[0])) {
            notbuiltin = 1;
          }
        }
        if (!notbuiltin) {
          return;
        }
        if (fns.n == 1) {
          CallExpr *new_expr = new CallExpr(fns.v[0], var->copy(), aop->get(2)->copy());
          aop->replace(new_expr);
          expr = new_expr;
        } else {
          if (!fns.n) {
            return;
          }
          INT_FATAL(expr, "Unable to resolve setter function");
        }
      } else if (CallExpr* paren = dynamic_cast<CallExpr*>(aop->get(1))) {
        Vec<FnSymbol*> fns;
        call_info(aop, fns);
        if (fns.n == 1) {
          AList<Expr>* arguments = new AList<Expr>();
          if (!strcmp("=this", fns.v[0]->name))
            arguments->insertAtTail(paren->baseExpr->copy());
          arguments->insertAtTail(copy_argument_list(paren));
          arguments->insertAtTail(aop->get(2)->copy());
          CallExpr *new_expr = new CallExpr(fns.v[0], arguments);
          new_expr->opTag = paren->opTag;
          aop->replace(new_expr);
          expr = new_expr;
        } else {
          INT_FATAL(expr, "Unable to resolve setter function");
        }
      } else if (MemberAccess* member_access = dynamic_cast<MemberAccess*>(aop->get(1))) {
        resolve_member_access(aop, &member_access->member_offset, 
                              &member_access->member_type);
        if (member_access->member_offset < 0) {
          Vec<FnSymbol *> op_fns, assign_fns;
          call_info(aop, op_fns, CALL_INFO_FIND_OPERATOR);
          call_info(aop, assign_fns, CALL_INFO_FIND_FUNCTION);
          if (op_fns.n > 1 || assign_fns.n != 1) 
            INT_FATAL(expr, "Unable to resolve member access");
          FnSymbol *f_op = op_fns.n ? op_fns.v[0] : 0;
          FnSymbol *f_assign = assign_fns.v[0];
          Expr *rhs = aop->get(2)->copy();
          if (f_op) {
            if (!is_builtin(f_op)) {
              rhs = new CallExpr(f_op, member_access->copy(), rhs);
            } else {
              rhs = resolve_binary_operator(new CallExpr(gets_to_op(aop->opTag), aop->get(1)->copy(), aop->get(2)->copy()), f_op);
            }
          }
          AList<Expr>* assign_arguments = new AList<Expr>(member_access->base->copy());
          assign_arguments->insertAtTail(rhs);
          Expr* assign_function = new SymExpr(f_assign);
          CallExpr *new_expr = new CallExpr(assign_function, assign_arguments);
          expr->replace(new_expr);
          if (aop->get(1)->astType == EXPR_MEMBERACCESS) {
            expr = aop->get(1);
          }
        } else {
          if (ClassType* struct_scope =
              dynamic_cast<ClassType*>(member_access->base->typeInfo())) {
            member_access->member = 
              Symboltable::lookupInScope(member_access->member->name,
                                         struct_scope->structScope);
          }
        }
      }
    }
  }

  // Resolve MemberAccesses
  if (MemberAccess* member_access = dynamic_cast<MemberAccess*>(expr)) {
    /***
     *** Resolve methods with arguments at CallExpr
     ***/
    if (CallExpr* paren_op = dynamic_cast<CallExpr*>(expr->parentExpr)) {
      if (paren_op->baseExpr == expr) {
        return;
      }
    }
    if (CallExpr* aop = dynamic_cast<CallExpr*>(expr->parentExpr))
      if (aop->opTag >= OP_GETSNORM && aop->get(1) == expr)
        return;

    resolve_member_access(member_access, &member_access->member_offset,
                          &member_access->member_type);
    if (member_access->member_offset < 0) {
      Vec<FnSymbol *> fns;
      call_info(member_access, fns);
      if (fns.n == 1) {
        Expr *new_expr = new CallExpr(fns.v[0], member_access->base->copy());
        expr->replace(new CallExpr(fns.v[0], member_access->base->copy()));
        expr = new_expr;
      } else
        INT_FATAL(expr, "Unable to resolve member access");
    } else {
      if (ClassType* struct_scope =
          dynamic_cast<ClassType*>(member_access->base->typeInfo())) {
        member_access->member = 
          Symboltable::lookupInScope(member_access->member->name,
                                     struct_scope->structScope);
      }
    }
  }

  // Resolve default constructors
  if (DefExpr* defExpr = dynamic_cast<DefExpr*>(expr)) {
    Vec<FnSymbol*> fns;
    call_info(defExpr, defExpr->initAssign, CALL_INFO_FIND_ASSIGN);
    call_info(defExpr, fns, CALL_INFO_FIND_NON_ASSIGN);
    if (fns.n == 1) {
      defExpr->sym->type->defaultConstructor = fns.v[0];
    } if (fns.n > 1) {
      INT_FATAL(expr, "Unable to resolve default constructor");
    } else if (defExpr->exprType) {
      if (CallExpr *fc = dynamic_cast<CallExpr*>(defExpr->exprType))
        if (SymExpr* v = dynamic_cast<SymExpr*>(fc->baseExpr))
          if (FnSymbol *fn = dynamic_cast<FnSymbol*>(v->var))
            defExpr->sym->type->defaultConstructor = fn;
    }
  }

  mangle_overloaded_operator_function_names(expr);

  // Resolve overloaded binary operators 
//   if (dynamic_cast<BinOp*>(expr)) {
//     if (typeid(BinOp) == typeid(*expr)) { // SJD: WANT TO REMOVE
//       expr = resolve_binary_operator(dynamic_cast<BinOp*>(expr));
//     }
//   }
}
