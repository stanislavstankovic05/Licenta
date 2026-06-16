/*
 * GlobalsPass.cpp — Mutable global variable analysis pass for the VMMU.
 *
 * Collects every mutable GlobalVariable defined in the module (app code),
 * assigns each a byte offset inside a per-app globals frame, then emits:
 *
 *   1. __vmmu_globals_size  — uint32_t constant: total bytes the globals
 *                             frame must be.  app_load() reads this to
 *                             call vmmu_malloc() for the right amount.
 *
 *   2. !vmmu.globals        — named IR metadata: one node per tracked
 *                             global: { MDString name, i64 offset }.
 *                             VMMUPass reads this table to rewrite every
 *                             direct global access to go through the frame.
 *
 * Size enforcement:
 *   If the total mutable global data exceeds one frame (VMMU_PAGE_SIZE =
 *   4096 bytes), emitError() fires and the build stops with a clear message.
 *   Move large data to const (flash) or pass it through the entry-point
 *   argument instead.
 *
 * Skipped globals:
 *   - const          → read-only, lives in flash, no isolation needed
 *   - isDeclaration  → extern, defined in another TU
 *   - VMMU internals (vmmu_*, g_app*, __vmmu_*, frame_*, heap_*)
 *   - Zephyr objects (_k_*, __k_*, z_*, _z_*)
 *   - Compiler / linker internals (__ prefix)
 *
 * Execution order:
 *   Registered at PipelineStartEP so the metadata is in place before
 *   VMMUPass (registered at OptimizerLastEP) reads it.
 *
 * Usage:
 *   clang -fpass-plugin=libGlobalsPass.so \
 *         -fpass-plugin=libVMMUPass.so     \
 *         app.c -o app.o
 */

#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Plugins/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

/* One physical frame = one page */
static constexpr uint64_t VMMU_PAGE_SIZE = 4096u;

/* ------------------------------------------------------------------ */
/* Skip predicate                                                       */
/* ------------------------------------------------------------------ */

static bool should_skip_global(const GlobalVariable &GV)
{
    /* Not defined here — no body to inspect */
    if(GV.isConstant())
    {
        return true; /* const → flash */
    }
    if(GV.isDeclaration())
    {
        return true; /* extern */
    }
    if(!GV.hasInitializer())
    {
        return true;
    }

    StringRef name = GV.getName();

    /* VMMU runtime internals */
    if(name.starts_with("vmmu_") ||
       name.starts_with("__vmmu_") ||
       name.starts_with("g_app") || /* g_apps[], g_app_stacks[] */
       name.starts_with("heap_") ||
       name.starts_with("frame_"))
    {
        return true;
    }

    /* Zephyr kernel objects */
    if(name.starts_with("_k_") ||
       name.starts_with("__k_") ||
       name.starts_with("z_") ||
       name.starts_with("_z_"))
    {
        return true;
    }

    /* Compiler / linker generated symbols */
    if(name.starts_with("__"))
    {
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* Pass                                                                 */
/* ------------------------------------------------------------------ */

namespace
{

struct GlobalsPass : PassInfoMixin<GlobalsPass>
{
    PreservedAnalyses run(Module &M, ModuleAnalysisManager &)
    {
        const DataLayout &DL = M.getDataLayout();
        LLVMContext &Ctx = M.getContext();

        /* ---- collect mutable app globals, assign byte offsets ---- */
        std::vector<std::pair<GlobalVariable *, uint64_t>> layout;
        uint64_t total = 0;

        for(GlobalVariable &GV : M.globals())
        {
            if(should_skip_global(GV))
            {
                continue;
            }

            uint64_t sz = DL.getTypeAllocSize(GV.getValueType());
            uint64_t align = DL.getABITypeAlign(GV.getValueType()).value();

            /* round up current offset to this variable's natural alignment */
            total = (total + align - 1u) & ~(align - 1u);
            layout.push_back({&GV, total});
            total += sz;
        }

        if(layout.empty())
        {
            errs() << "[GlobalsPass] no mutable app globals found — nothing to do\n";
            return PreservedAnalyses::all();
        }

        /* ---- size enforcement: must fit in one frame ---- */
        if(total > VMMU_PAGE_SIZE)
        {
            M.getContext().emitError(
                "vmmu: mutable app globals (" + std::to_string(total) +
                " bytes) exceed one frame (" +
                std::to_string(VMMU_PAGE_SIZE) +
                " bytes). Move large data to const or pass via entry point.");
            return PreservedAnalyses::all();
        }

        errs() << "[GlobalsPass] "
               << layout.size() << " mutable globals, "
               << total << " bytes total\n";

        /* ---- emit __vmmu_globals_size as an extern uint32_t constant ----
         *
         * app_entry_wrapper() (app.c) references this symbol at runtime:
         *   if (__vmmu_globals_size > 0) vmmu_malloc(__vmmu_globals_size);
         *
         * A weak default (= 0) is defined in app.c so builds without this
         * pass still link.
         */
        auto *U32Ty = Type::getInt32Ty(Ctx);

        /* avoid duplicate if the pass somehow runs twice */
        if(!M.getNamedGlobal("__vmmu_globals_size"))
        {
            new GlobalVariable(
                M, U32Ty,
                /*isConstant=*/true,
                GlobalValue::ExternalLinkage,
                ConstantInt::get(U32Ty, (uint32_t)total),
                "__vmmu_globals_size");
        }

        /* ---- emit offset metadata !vmmu.globals ----
         *
         * Each operand node: !{ MDString "name", i64 offset }
         * VMMUPass iterates these to build its GlobalVariable → offset map.
         */
        NamedMDNode *NMD = M.getOrInsertNamedMetadata("vmmu.globals");
        auto *I64 = Type::getInt64Ty(Ctx);

        for(auto &[GV, offset] : layout)
        {
            Metadata *ops[] = {
                MDString::get(Ctx, GV->getName()),
                ConstantAsMetadata::get(ConstantInt::get(I64, offset))};
            NMD->addOperand(MDNode::get(Ctx, ops));

            errs() << "[GlobalsPass]   "
                   << GV->getName()
                   << "  offset=" << offset
                   << "  size=" << DL.getTypeAllocSize(GV->getValueType())
                   << "\n";
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
        LLVM_PLUGIN_API_VERSION, "GlobalsPass", "v1.0",
        [](PassBuilder &PB)
        {
            /*
             * PipelineStart runs before any optimisation — metadata is
             * guaranteed to be present when VMMUPass (OptimizerLast) runs.
             * 2-parameter callback (no LTO phase argument at this EP).
             */
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &MPM, OptimizationLevel)
                {
                    MPM.addPass(GlobalsPass{});
                });
        }};
}
