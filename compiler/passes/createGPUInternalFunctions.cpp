/*
 * Copyright 2004-2015 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "astutil.h"
#include "build.h"
#include "passes.h"
#include "scopeResolve.h"
#include "stlUtil.h"
#include "stmt.h"
#include "stringutil.h"

static void createInternalFunctions(FnSymbol * fn,
                                    std::map<FnSymbol*, FnSymbol*>& newFnMap)
{
  std::vector<BaseAST*> asts;
  collect_top_asts(fn, asts);
  for_vector(BaseAST, ast, asts) {
    if (CallExpr* call = toCallExpr(ast)) {
      if (call->isResolved()) {
        if (FnSymbol* f = call->findFnSymbol()) {
          if (newFnMap.find(f) == newFnMap.end()) {
            createInternalFunctions(f, newFnMap);
            ModuleSymbol* mod = f->getModule();
            SET_LINENO(mod->block);
            FnSymbol *newFn = f->copy();
            newFn->addFlag(FLAG_INTERNAL_GPU_FN);
            mod->block->insertAtTail(new DefExpr(newFn));
            newFnMap[f] = newFn;
          }
          SET_LINENO(call);
          call->baseExpr->replace(new SymExpr(newFnMap[f]));
        }
      }
    }
  }
}

// The createGPUInternalFunction recursively creates copies of any function
// bodies that is calld by the top-level GPU kernels. These copies will be
// codegened into the gpu source file. This needs to be done since the opencl
// kernels cannot directly link to C files.
void createGPUInternalFunctions(void)
{
  std::vector<FnSymbol*> gpuFunctions;
  forv_Vec(FnSymbol, fn, gFnSymbols) {
    if (fn->hasFlag(FLAG_OFFLOAD_TO_GPU))
      gpuFunctions.push_back(fn);
  }
  std::map<FnSymbol*, FnSymbol*> newFunctionMap;
  for_vector(FnSymbol, fn, gpuFunctions) {
    createInternalFunctions(fn, newFunctionMap);
  }
}
