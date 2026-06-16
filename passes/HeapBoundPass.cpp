/*
 * HeapBoundPass.cpp — Static heap analysis pass for the VMMU.
 *
 * Analyses app code at compile time and writes the results as IR
 * named metadata (!vmmu.bounds) so they survive into the final binary.
 * app_load() reads this metadata to set per-app frame budgets and
 * compute the worst-case demand paging fault latency (T9).
 *
 * Metadata format (attached to the module):
 *
 *   !vmmu.bounds = !{!0}
 *   !0 = !{
 *       i64  peak_live_bytes,   -- conservative peak simultaneous live bytes
 *       i64  total_alloc_bytes, -- sum of all constant malloc sizes seen
 *       i1   has_unknown_sizes, -- true if any malloc has a non-constant size
 *       i1   has_recursion,     -- true if the call graph contains a cycle
 *       i1   alloc_in_loop      -- true if any malloc is inside a loop
 *   }
 *
 * Conservative approximation:
 *   peak_live_bytes = total_alloc_bytes (assumes all allocs live at once).
 *   If has_unknown_sizes or has_recursion is true, the bound is unbounded
 *   and T9 will reject app_load at runtime.
 *
 * Built as a shared plugin:
 *   clang++ -shared -fPIC -o libHeapBoundPass.so HeapBoundPass.cpp \
 *     $(llvm-config --cxxflags --ldflags)
 *
 * Injected via:
 *   clang -fpass-plugin=libHeapBoundPass.so app.c ...
 */

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <map>
#include <set>
#include <string>

using namespace llvm;

/* ------------------------------------------------------------------ */
/* Call-graph recursion detection                                        */
/* ------------------------------------------------------------------ */

/*
 * Adjacency map: function name -> set of directly called function names.
 * Built by scanning all CallInst in each function body.
 */
using CGraph = std::map<std::string, std::set<std::string>>;

/*
 * Iterative DFS — detects back edges (cycles = recursion).
 * Returns true if fn is part of a cycle reachable from itself.
 */
static bool has_cycle(const std::string &fn,
                      const CGraph &cg,
                      std::set<std::string> &visited,
                      std::set<std::string> &in_stack)
{
    if(in_stack.count(fn))
    {
        return true;
    }
    if(visited.count(fn))
    {
        return false;
    }

    visited.insert(fn);
    in_stack.insert(fn);

    auto it = cg.find(fn);
    if(it != cg.end())
    {
        for(const auto &callee : it->second)
        {
            if(has_cycle(callee, cg, visited, in_stack))
            {
                return true;
            }
        }
    }

    in_stack.erase(fn);
    return false;
}

static bool detect_recursion(const CGraph &cg)
{
    std::set<std::string> visited, in_stack;
    for(const auto &[fn, _] : cg)
    {
        if(has_cycle(fn, cg, visited, in_stack))
        {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Pass                                                                 */
/* ------------------------------------------------------------------ */

namespace
{

struct HeapBoundPass : PassInfoMixin<HeapBoundPass>
{
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM)
    {
        LLVMContext &Ctx = M.getContext();

        /*
         * Get the per-function analysis manager so we can query
         * LoopInfo for each non-declaration function.
         */
        auto &FAM =
            MAM.getResult<FunctionAnalysisManagerModuleProxy>(M)
                .getManager();

        /* ---- build call graph ---- */
        CGraph cg;
        for(Function &F : M)
        {
            if(F.isDeclaration())
            {
                continue;
            }

            std::string fname = F.getName().str();
            cg.emplace(fname, std::set<std::string>{});

            for(BasicBlock &BB : F)
            {
                for(Instruction &I : BB)
                {
                    if(auto *CI = dyn_cast<CallInst>(&I))
                    {
                        if(Function *callee = CI->getCalledFunction())
                        {
                            cg[fname].insert(callee->getName().str());
                        }
                    }
                }
            }
        }

        bool has_recursion = detect_recursion(cg);

        /* ---- scan malloc call sites ---- */
        uint64_t total_alloc_bytes = 0;
        bool has_unknown_sizes = false;
        bool alloc_in_loop = false;

        /*
         * Recognised allocation functions.
         * vmmu_malloc is what malloc redirects to after --wrap=malloc.
         * k_malloc is Zephyr's kernel allocator (not used by apps, but
         * included for completeness in case a lib calls it).
         */
        auto is_alloc = [](StringRef name)
        {
            return name == "malloc" ||
                   name == "vmmu_malloc" ||
                   name == "k_malloc";
        };

        for(Function &F : M)
        {
            if(F.isDeclaration())
            {
                continue;
            }

            LoopInfo &LI = FAM.getResult<LoopAnalysis>(F);

            for(BasicBlock &BB : F)
            {
                for(Instruction &I : BB)
                {
                    auto *CI = dyn_cast<CallInst>(&I);
                    if(!CI)
                    {
                        continue;
                    }

                    Function *callee = CI->getCalledFunction();
                    if(!callee || !is_alloc(callee->getName()))
                    {
                        continue;
                    }

                    /* check if this malloc is inside a loop */
                    if(LI.getLoopFor(&BB))
                    {
                        alloc_in_loop = true;
                    }

                    /* size is argument 0 */
                    Value *size_arg = CI->getArgOperand(0);
                    if(auto *C = dyn_cast<ConstantInt>(size_arg))
                    {
                        total_alloc_bytes += C->getZExtValue();
                    }
                    else
                    {
                        has_unknown_sizes = true;
                    }
                }
            }
        }

        /*
         * Conservative peak estimate: assume all allocations are live
         * simultaneously. This over-approximates but is always safe.
         * T9 uses this as the max_frames bound for app_load().
         */
        uint64_t peak_live_bytes = total_alloc_bytes;

        /* ---- emit IR metadata ---- */
        Type *I64 = Type::getInt64Ty(Ctx);
        Type *I1 = Type::getInt1Ty(Ctx);

        Metadata *fields[] = {
            ConstantAsMetadata::get(
                ConstantInt::get(I64, peak_live_bytes)),
            ConstantAsMetadata::get(
                ConstantInt::get(I64, total_alloc_bytes)),
            ConstantAsMetadata::get(
                ConstantInt::get(I1, has_unknown_sizes ? 1 : 0)),
            ConstantAsMetadata::get(
                ConstantInt::get(I1, has_recursion ? 1 : 0)),
            ConstantAsMetadata::get(
                ConstantInt::get(I1, alloc_in_loop ? 1 : 0)),
        };

        MDNode *node = MDNode::get(Ctx, fields);
        NamedMDNode *named = M.getOrInsertNamedMetadata("vmmu.bounds");
        named->addOperand(node);

        errs() << "[HeapBoundPass] "
               << M.getName()
               << ": peak=" << peak_live_bytes
               << " total=" << total_alloc_bytes
               << " unk=" << has_unknown_sizes
               << " rec=" << has_recursion
               << " loop=" << alloc_in_loop
               << "\n";

        return PreservedAnalyses::none();
    }
};

} // anonymous namespace

/* ------------------------------------------------------------------ */
/* Plugin registration                                                  */
/* ------------------------------------------------------------------ */

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo()
{
    return {
        LLVM_PLUGIN_API_VERSION, "HeapBoundPass", "v1.0",
        [](PassBuilder &PB)
        {
            /*
             * Run before OptimizerLast so the metadata is in place
             * before VMMUPass instruments the code.
             * Register at PipelineStart to run as a module pass first.
             */
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel)
                {
                    MPM.addPass(HeapBoundPass{});
                });
            /* Note: PipelineStartEP uses 2-param callback (no LTO phase) */
        }};
}
