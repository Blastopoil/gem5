#ifndef __CPU_PRED_FALSE_PRED_HH__
#define __CPU_PRED_FALSE_PRED_HH__

#include <vector>

#include "base/sat_counter.hh"
#include "base/types.hh"
#include "cpu/pred/conditional.hh"
#include "params/AlwaysFalseBP.hh"

namespace gem5
{

namespace branch_prediction
{

/**
 * A predictor that always predict a branch as non taken.
 * TODO: Figure out wether this will happen for uncond branches.
 */
class AlwaysFalseBP : public ConditionalPredictor
{
  public:
    /**
     * Default branch predictor constructor.
     */
    AlwaysFalseBP(const AlwaysFalseBPParams &params);

    // Overriding interface functions
    bool lookup(ThreadID tid, Addr pc, void * &bp_history) override;

    void branchPlaceholder(ThreadID tid, Addr pc, bool uncond,
                           void * &bpHistory) override;

    void updateHistories(ThreadID tid, Addr pc, bool uncond, bool taken,
                         Addr target, const StaticInstPtr &inst,
                         void * &bp_history) override;

    void update(ThreadID tid, Addr pc, bool taken,
                void * &bp_history, bool squashed,
                const StaticInstPtr & inst, Addr target) override;

    void squash(ThreadID tid, void * &bp_history) override
    { assert(bp_history == NULL); }
};

} // namespace branch_prediction
} // namespace gem5

#endif // __CPU_PRED_FALSE_PRED_HH__
