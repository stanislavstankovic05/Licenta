/*
 * VMMUPass.cpp — LLVM instrumentation pass for the VMMU.
 *
 * Injects vmmu_check(ptr, len, access) before every IR load/store in app
 * code.  vmmu_check validates the access against the current app's address
 * space and calls app_abort() on violation.
 *
 * Optimisation — loop range hoisting:
 *   For loops that access an array with a loop-invariant base pointer and
 *   a simple induction variable (i = 0..N), we hoist ONE range check into
 *   the loop preheader that covers the entire array slice [base, base+N*sz).
 *   Per-element checks inside the loop body are then skipped for that base.
 *
 *   This reduces vmmu_check calls from O(N) to O(1) per array per loop,
 *   which is the dominant cost for workloads like wl_array_fill_sum and
 *   wl_xor_sweep.
 *
 * Skipped functions (kernel-side, trusted):
 *   vmmu_*, app_*, frame_*, heap_*, addrspace_*
 *
 * Built as a shared plugin:
 *   clang++ -shared -fPIC -o libVMMUPass.so VMMUPass.cpp \
 *     $(llvm-config --cxxflags --ldflags)
 *
 * Injected via:
 *   clang -fpass-plugin=libVMMUPass.so app.c ...
 */

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/Mem2Reg.h"
#include "llvm/Transforms/Utils/ScalarEvolutionExpander.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#include <map>

using namespace llvm;

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/*
 * should_skip — decide whether to skip instrumentation for this function.
 *
 * Path-based: skip only functions whose source file lives in trusted
 * locations (VMMU runtime, Zephyr kernel, libc, toolchain headers).
 * Everything else is app code and gets instrumented.
 *
 * The old name-prefix heuristic ("app_*", "vmmu_*", etc.) was a bypass —
 * any app could declare a function named "app_attack" and evade the pass.
 * Path-based exclusion cannot be forged by the app.
 *
 * Requires the build to use `-g` so debug info is present. If a function
 * has no DISubprogram, we conservatively instrument it (safe default).
 */
static bool should_skip(const Function &F)
{
    if(F.isDeclaration())
    {
        return true;
    }

    DISubprogram *SP = F.getSubprogram();
    if(!SP)
    {
        /* No debug info — can't determine origin. Conservative: instrument. */
        return false;
    }

    StringRef filename = SP->getFilename();
    StringRef directory = SP->getDirectory();

    /* VMMU runtime — never instrument (would recurse infinitely). */
    if(directory.contains("/vmmu") || directory.ends_with("vmmu") ||
       filename.contains("vmmu/"))
    {
        return true;
    }

    /* Zephyr kernel and HAL — trusted, not app code. */
    if(directory.contains("/zephyr") || directory.contains("zephyrproject"))
    {
        return true;
    }

    /* Toolchain / libc / SDK headers. */
    if(directory.contains("/zephyr-sdk") ||
       directory.contains("/picolibc") ||
       directory.contains("/newlib") ||
       directory.contains("/llvm") ||
       directory.contains("/usr/include") ||
       directory.contains("/usr/lib"))
    {
        return true;
    }

    return false;
}

/*
 * warn_inline_asm — emit a build-time warning for every inline asm block
 * found in an app function. Inline asm bypasses VMMUPass instrumentation
 * (the pass only rewrites IR load/store, not raw asm). We warn so the user
 * is aware their app contains an unprotected path. This is a documented
 * thesis limitation, not a hard error.
 */
static void warn_inline_asm(Function &F)
{
    for(BasicBlock &BB : F)
    {
        for(Instruction &I : BB)
        {
            if(CallInst *CI = dyn_cast<CallInst>(&I))
            {
                if(CI->isInlineAsm())
                {
                    StringRef where = "<unknown>";
                    unsigned line = 0;
                    if(const DebugLoc &DL = I.getDebugLoc())
                    {
                        where = DL->getFilename();
                        line = DL->getLine();
                    }
                    errs() << "VMMUPass warning: inline asm in '"
                           << F.getName() << "' at "
                           << where << ":" << line
                           << " bypasses memory checks\n";
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Loop range check hoisting                                            */
/* ------------------------------------------------------------------ */

/*
 * tryHoistLoop — attempt to hoist a single range check for each
 * loop-invariant base pointer accessed inside loop L.
 *
 * Pattern matched:
 *   for (i = 0; i < N; i++) ... buf[i] ...
 *
 * IR form matched:
 *   %ptr = getelementptr T, ptr %base, <IV or cast-of-IV>
 *   load/store ... %ptr
 *   where %base is loop-invariant and IV is the canonical induction variable.
 *
 * Action:
 *   Insert vmmu_check(%base, N * sizeof(T), READ|WRITE) in the preheader.
 *   Add %base to `hoisted` so the per-element checks are skipped.
 */
static void tryHoistLoop(Loop *L,
                         ScalarEvolution &SE,
                         FunctionCallee CheckFn,
                         const DataLayout &DL,
                         LLVMContext &Ctx,
                         DenseSet<Value *> &hoisted)
{
    /* Need a preheader to insert into */
    BasicBlock *preheader = L->getLoopPreheader();
    if(!preheader)
    {
        return;
    }

    /*
     * Trip count via ScalarEvolution. Robust to the loop's exit form:
     * O2 rewrites `j < n` into `(j+1) == n`, which Loop::getBounds does
     * not recognise, but SCEV analyses the recurrence directly.
     */
    const SCEV *btc = SE.getBackedgeTakenCount(L);
    if(isa<SCEVCouldNotCompute>(btc))
    {
        return;
    }

    Type *SizeTy = DL.getIntPtrType(Ctx);

    /* trip_count = backedge_taken_count + 1, in pointer-size integer */
    const SCEV *one = SE.getOne(btc->getType());
    const SCEV *trip = SE.getTruncateOrZeroExtend(SE.getAddExpr(btc, one),
                                                  SizeTy);

    /*
     * For each load/store whose address is an affine recurrence on this
     * loop (base + i*stride, base loop-invariant, start == base), record
     * base -> stride. This matches buf[i] regardless of the latch form.
     */
    DenseMap<Value *, const SCEV *> base_to_step;

    for(BasicBlock *BB : L->blocks())
    {
        for(Instruction &I : *BB)
        {
            Value *ptr = nullptr;

            if(auto *LD = dyn_cast<LoadInst>(&I))
            {
                ptr = LD->getPointerOperand();
            }
            else if(auto *ST = dyn_cast<StoreInst>(&I))
            {
                ptr = ST->getPointerOperand();
            }
            else
            {
                continue;
            }

            auto *GEP = dyn_cast<GetElementPtrInst>(ptr);
            if(!GEP)
            {
                continue;
            }

            Value *base = GEP->getPointerOperand();
            if(!L->isLoopInvariant(base))
            {
                continue;
            }

            const SCEV *addr = SE.getSCEV(GEP);
            auto *ar = dyn_cast<SCEVAddRecExpr>(addr);
            if(!ar || ar->getLoop() != L || !ar->isAffine())
            {
                continue;
            }

            /* The recurrence must start exactly at the base pointer */
            if(ar->getStart() != SE.getSCEV(base))
            {
                continue;
            }

            base_to_step[base] = ar->getStepRecurrence(SE);
        }
    }

    if(base_to_step.empty())
    {
        return;
    }

    /* Materialise one hoisted range check per base in the preheader */
    Instruction *insert_pt = preheader->getTerminator();
    IRBuilder<> B(insert_pt);
    Type *I8Ty = Type::getInt8Ty(Ctx);
    SCEVExpander expander(SE, "vmmu.hoist");

    for(auto &[base, step] : base_to_step)
    {
        /* range_bytes = trip_count * stride */
        const SCEV *step_sz = SE.getTruncateOrZeroExtend(step, SizeTy);
        const SCEV *range_scev = SE.getMulExpr(trip, step_sz);
        Value *range = expander.expandCodeFor(range_scev, SizeTy,
                                              insert_pt->getIterator());

        /* vmmu_check(base, range, PERM_READ|PERM_WRITE=3) */
        B.CreateCall(CheckFn, {base, range, ConstantInt::get(I8Ty, 3)});

        hoisted.insert(base);
    }
}

/* ------------------------------------------------------------------ */
/* Pass                                                                 */
/* ------------------------------------------------------------------ */

namespace
{

struct VMMUPass : PassInfoMixin<VMMUPass>
{
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM)
    {
        if(should_skip(F))
        {
            return PreservedAnalyses::all();
        }

        warn_inline_asm(F);

        Module *M = F.getParent();
        LLVMContext &Ctx = M->getContext();
        const DataLayout &DL = M->getDataLayout();

        Type *VoidTy = Type::getVoidTy(Ctx);
        Type *PtrTy = PointerType::getUnqual(Ctx);
        Type *SizeTy = DL.getIntPtrType(Ctx);
        Type *I8Ty = Type::getInt8Ty(Ctx);

        /* Stack check at function entry */
        FunctionCallee StackCheckFn = M->getOrInsertFunction(
            "vmmu_stack_check",
            FunctionType::get(VoidTy, {}, false));
        BasicBlock &EntryBB = F.getEntryBlock();
        IRBuilder<> EntryBuilder(&*EntryBB.getFirstInsertionPt());
        EntryBuilder.CreateCall(StackCheckFn, {});

        /* vmmu_check declaration */
        FunctionCallee CheckFn = M->getOrInsertFunction(
            "vmmu_check",
            FunctionType::get(VoidTy, {PtrTy, SizeTy, I8Ty}, false));

        /* GlobalsPass metadata */
        std::map<GlobalVariable *, uint64_t> global_offsets;
        FunctionCallee GlobalsBaseFn;

        if(NamedMDNode *NMD = M->getNamedMetadata("vmmu.globals"))
        {
            GlobalsBaseFn = M->getOrInsertFunction(
                "vmmu_globals_base",
                FunctionType::get(PtrTy, {}, false));
            for(MDNode *N : NMD->operands())
            {
                auto *NameMD = dyn_cast<MDString>(N->getOperand(0));
                auto *OffsetMD = dyn_cast<ConstantAsMetadata>(N->getOperand(1));
                if(!NameMD || !OffsetMD)
                {
                    continue;
                }
                StringRef gname = NameMD->getString();
                uint64_t offset = cast<ConstantInt>(
                                      OffsetMD->getValue())
                                      ->getZExtValue();
                if(GlobalVariable *GV = M->getGlobalVariable(gname))
                {
                    global_offsets[GV] = offset;
                }
            }
        }

        /*
         * Loop range hoisting.
         *
         * Request LoopInfo and ScalarEvolution analyses, then try to hoist
         * one range check per array base per loop into the preheader.
         * Bases that were hoisted are added to `hoisted` — the per-element
         * check below skips load/stores whose base pointer is in this set.
         */
        DenseSet<Value *> hoisted;

        auto &LI = AM.getResult<LoopAnalysis>(F);
        auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
        auto &AC = AM.getResult<AssumptionAnalysis>(F);
        auto &SE = AM.getResult<ScalarEvolutionAnalysis>(F);

        /*
         * Put every loop into simplified form so inner loops get a
         * dedicated preheader — that is where the hoisted range check is
         * inserted. Without this, nested loops (buf[j] inside for i) have
         * no preheader and fall back to per-element checks.
         */
        SmallVector<Loop *, 8> top_loops(LI.begin(), LI.end());
        for(Loop *L : top_loops)
        {
            simplifyLoop(L, &DT, &LI, &SE, &AC, nullptr,
                         /*PreserveLCSSA=*/true);
        }

        SmallVector<Loop *, 8> all_loops;
        for(Loop *L : LI)
        {
            for(Loop *sub : L->getLoopsInPreorder())
            {
                all_loops.push_back(sub);
            }
        }

        for(Loop *L : all_loops)
        {
            tryHoistLoop(L, SE, CheckFn, DL, Ctx, hoisted);
        }

        /* Collect load/store targets */
        SmallVector<Instruction *, 64> targets;
        for(BasicBlock &BB : F)
        {
            for(Instruction &I : BB)
            {
                if(isa<LoadInst>(I) || isa<StoreInst>(I))
                {
                    targets.push_back(&I);
                }
            }
        }

        if(targets.empty())
        {
            return PreservedAnalyses::all();
        }

        for(Instruction *I : targets)
        {
            IRBuilder<> B(I);

            Value *ptr;
            uint64_t byte_size;
            uint8_t access_flag;

            if(auto *LI = dyn_cast<LoadInst>(I))
            {
                ptr = LI->getPointerOperand();
                byte_size = DL.getTypeStoreSize(LI->getType());
                access_flag = 1; /* PERM_READ */
            }
            else
            {
                auto *SI = cast<StoreInst>(I);
                ptr = SI->getPointerOperand();
                byte_size = DL.getTypeStoreSize(
                    SI->getValueOperand()->getType());
                access_flag = 2; /* PERM_WRITE */
            }

            /* Global variable redirection */
            if(!global_offsets.empty())
            {
                if(auto *GV = dyn_cast<GlobalVariable>(ptr))
                {
                    auto it = global_offsets.find(GV);
                    if(it != global_offsets.end())
                    {
                        Value *base = B.CreateCall(GlobalsBaseFn, {});
                        Value *new_ptr = B.CreateConstGEP1_64(
                            Type::getInt8Ty(Ctx), base, it->second);
                        if(auto *LI = dyn_cast<LoadInst>(I))
                        {
                            LI->setOperand(0, new_ptr);
                        }
                        else
                        {
                            cast<StoreInst>(I)->setOperand(1, new_ptr);
                        }
                        ptr = new_ptr;
                    }
                }
            }

            /*
             * Skip per-element check if this GEP's base was already covered
             * by a hoisted range check in the loop preheader.
             */
            if(auto *GEP = dyn_cast<GetElementPtrInst>(ptr))
            {
                if(hoisted.count(GEP->getPointerOperand()))
                {
                    continue; /* range check already covers this access */
                }
            }

            B.CreateCall(CheckFn, {ptr,
                                   ConstantInt::get(SizeTy, byte_size),
                                   ConstantInt::get(I8Ty, access_flag)});
        }

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
        LLVM_PLUGIN_API_VERSION, "VMMUPassO0", "v1.0",
        [](PassBuilder &PB)
        {
            /* O0 variant.
             *
             * The optimization-pipeline extension points (VectorizerStart,
             * ScalarOptimizerLate, ...) are NOT invoked at -O0, so this pass
             * registers at PipelineStart, which buildO0DefaultPipeline does
             * run. At -O0 induction variables live in memory (no mem2reg),
             * so ScalarEvolution cannot see canonical counted loops and the
             * range hoisting would fail. We therefore run PromotePass
             * (mem2reg) immediately before VMMUPass so the hoisting matcher
             * sees SSA phi-node IVs and can hoist one check per array. */
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel)
                {
                    FunctionPassManager FPM;
                    FPM.addPass(PromotePass());
                    FPM.addPass(VMMUPass{});
                    MPM.addPass(createModuleToFunctionPassAdaptor(
                        std::move(FPM)));
                });
        }};
}
