// Copyright (c) by respective owners including Yahoo!, Microsoft, and
// individual contributors. All rights reserved. Released under a BSD (revised)
// license as described in the file LICENSE.

#include "../automl_impl.h"

#include "vw/common/vw_exception.h"

/*
This reduction implements the ChaCha algorithm from page 5 of the following paper:
https://arxiv.org/pdf/2106.04815.pdf

There are two key differences between this implementation and the algorithm described
in the paper. First, the paper assumes all knowledge (of examples and namespaces) is
known at the start, whereas that information is gathered as we process examples in reality.
Second, this algorithm follows the pattern Schedule -> Update Champ -> Learn, whereas
the paper follows the pattern Schedule -> Learn -> Update Champ. This should not make a
functional difference. Nearly all of the variables and functions described in the
algorithm live in the config_manager struct. The following provides a translation of
terms used in the paper to this implementation:

Variables from ChaCha:
c_init -> 0: The first champ is initialized to the 0th indexed set of weights
b(budget) -> max_live_configs: The weights/configs to learn on
C -> current_champ: The weight index of the current champ
S -> configs: The entire set of challengers/configs generated so far
B(+ C) -> estimators: The live challengers (including the champ) with statistics

Functions from ChaCha:
ConfigOracle -> config_oracle(): Generates new configs based on champ
Schedule -> schedule(): Swaps out / manages live configs on each step
Schedule.Choose -> calc_priority(): Determines priority when selecting next live config
Predict/Incur Loss -> offset_learn(): Updates weights and learns on live configs
Better -> better(): Changes champ if challenger is better
Worse -> worse(): Removes challenger is much worse than champ

New Concepts:
The 'eligible_to_inactivate' flag within socred_config is an implementation of the median
function in Algorithm 2 of Chacha. Basically we set half (or some number with the
--priority_challengers flag) of the live configs as regular, and the other half as priority.
The champ is a priority config, but is used for predictions while the other configs are not.
When a priority config runs out of lease, its lease it doubled and it's able to keep running.
When a regular config runs out of lease, we first compare it's lower_bound to each of the
priority config's ips. If the lower bound is higher, we'll swap the priorities (priority
becomes regular, regular becomes priority), then double its lease and keep running. If the
lower_bound of the regular config can't beat the ips of any priority configs, then it is
swapped out for some new config on the index_queue. The idea is that some live slots should
be used for priority challengers to have more time to beat the champ, while others are used
to swap out and try new configs more consistently.
*/

namespace VW
{
namespace reductions
{
namespace automl
{
// config_manager is a state machine (config_manager_state) 'time' moves forward after a call into one_step()
// this can also be interpreted as a pre-learn() hook since it gets called by a learn() right before calling
// into its own base_learner.learn(). see learn_automl(...)
interaction_config_manager::interaction_config_manager(uint64_t global_lease, uint64_t max_live_configs,
    std::shared_ptr<VW::rand_state> rand_state, uint64_t priority_challengers, std::string interaction_type,
    std::string oracle_type, dense_parameters& weights, priority_func* calc_priority, double automl_significance_level,
    double automl_estimator_decay, VW::io::logger* logger, uint32_t& wpp, bool lb_trick, bool ccb_on)
    : global_lease(global_lease)
    , max_live_configs(max_live_configs)
    , priority_challengers(priority_challengers)
    , interaction_type(interaction_type)
    , weights(weights)
    , automl_significance_level(automl_significance_level)
    , automl_estimator_decay(automl_estimator_decay)
    , logger(logger)
    , wpp(wpp)
    , lb_trick(lb_trick)
    , ccb_on(ccb_on)
    , _config_oracle(config_oracle(global_lease, calc_priority, index_queue, ns_counter, configs, interaction_type,
          oracle_type, std::move(rand_state)))
{
  configs.emplace_back(global_lease);
  configs[0].state = VW::reductions::automl::config_state::Live;
  estimators.emplace_back(std::make_pair(aml_estimator(automl_significance_level, automl_estimator_decay),
      estimator_config(automl_significance_level, automl_estimator_decay)));
  ++_config_oracle.valid_config_size;
}

bool interaction_config_manager::swap_eligible_to_inactivate(
    bool lb_trick, std::vector<std::pair<aml_estimator, estimator_config>>& estimators, uint64_t live_slot)
{
  const uint64_t current_champ = 0;
  for (uint64_t other_live_slot = 0; other_live_slot < estimators.size(); ++other_live_slot)
  {
    bool better = lb_trick
        ? estimators[live_slot].first.lower_bound() > (1.f - estimators[other_live_slot].first.lower_bound())
        : estimators[live_slot].first.lower_bound() > estimators[other_live_slot].first.upper_bound();
    if (!estimators[other_live_slot].first.eligible_to_inactivate && other_live_slot != current_champ && better)
    {
      estimators[live_slot].first.eligible_to_inactivate = false;
      estimators[other_live_slot].first.eligible_to_inactivate = true;
      return true;
    }
  }
  return false;
}

// This function defines the logic update the live configs' leases, and to swap out
// new configs when the lease runs out.
void interaction_config_manager::schedule()
{
  for (uint64_t live_slot = 0; live_slot < max_live_configs; ++live_slot)
  {
    bool need_new_estimator = estimators.size() <= live_slot;
    /*
    Scheduling a new live config is necessary in 3 cases:
    1. We have not reached the maximum number of live configs yet
    2. The current live config has been removed due to Chacha's worse function
    3. A config has reached its lease
    */
    if (need_new_estimator ||
        configs[estimators[live_slot].first.config_index].state == VW::reductions::automl::config_state::Removed ||
        estimators[live_slot].first.update_count >= configs[estimators[live_slot].first.config_index].lease)
    {
      // Double the lease check swap for eligible_to_inactivate configs
      if (!need_new_estimator &&
          configs[estimators[live_slot].first.config_index].state == VW::reductions::automl::config_state::Live)
      {
        configs[estimators[live_slot].first.config_index].lease *= 2;
        if (!estimators[live_slot].first.eligible_to_inactivate ||
            swap_eligible_to_inactivate(lb_trick, estimators, live_slot))
        { continue; }
      }
      // Skip over removed configs in index queue, and do nothing we we run out of eligible configs
      while (!index_queue.empty() &&
          configs[index_queue.top().second].state == VW::reductions::automl::config_state::Removed)
      { index_queue.pop(); }
      if (index_queue.empty() && !_config_oracle.repopulate_index_queue()) { continue; }
      // Allocate new estimator if we haven't reached maximum yet
      if (need_new_estimator)
      {
        estimators.emplace_back(std::make_pair(aml_estimator(automl_significance_level, automl_estimator_decay),
            estimator_config(automl_significance_level, automl_estimator_decay)));
        if (live_slot > priority_challengers) { estimators.back().first.eligible_to_inactivate = true; }
      }
      // Only inactivate current config if lease is reached
      if (!need_new_estimator &&
          configs[estimators[live_slot].first.config_index].state == VW::reductions::automl::config_state::Live)
      { configs[estimators[live_slot].first.config_index].state = VW::reductions::automl::config_state::Inactive; }
      // Set all features of new live config
      estimators[live_slot].first.reset_stats(automl_significance_level, automl_estimator_decay);
      estimators[live_slot].second.reset_stats(automl_significance_level, automl_estimator_decay);
      uint64_t new_live_config_index = choose(index_queue);
      estimators[live_slot].first.config_index = new_live_config_index;
      configs[new_live_config_index].state = VW::reductions::automl::config_state::Live;
      weights.move_offsets(current_champ, live_slot, wpp);
      // Regenerate interactions each time an exclusion is swapped in
      gen_interactions(ccb_on, ns_counter, interaction_type, configs, estimators, live_slot);
      // We may also want to 0 out weights here? Currently keep all same in live_slot position
    }
  }
}

bool better(bool lb_trick, aml_estimator& challenger, estimator_config& champ)
{
  return lb_trick ? challenger.lower_bound() > (1.f - champ.lower_bound())
                  : challenger.lower_bound() > champ.upper_bound();
}

void interaction_config_manager::update_champ()
{
  bool champ_change = false;
  uint64_t old_champ_slot = current_champ;
  uint64_t winning_challenger_slot = 0;

  // compare lowerbound of any challenger to the ips of the champ, and switch whenever when the LB beats the champ
  for (uint64_t live_slot = 0; live_slot < estimators.size(); ++live_slot)
  {
    if (live_slot == current_champ) { continue; }
    // If challenger is better ('better function from Chacha')
    if (better(lb_trick, estimators[live_slot].first, estimators[live_slot].second))
    {
      champ_change = true;
      winning_challenger_slot = live_slot;
    }
    else if (worse())  // If challenger is worse ('worse function from Chacha')
    {
      configs[estimators[live_slot].first.config_index].state = VW::reductions::automl::config_state::Removed;
    }
  }
  if (champ_change)
  {
    /*
     * Note here that the wining challenger (and its weights) will be moved into slot 0, and the old
     * champion will move into slot 1. All other weights are no longer relevant and will later take on the
     * champ's weights. The current champion will always be in slot 0, so if the winning challenger is in
     * slot 3 with 5 live models, the following operations will occur:
     * w0 w1 w2 w3 w4
     * w3 w1 w2 w0 w4 // w3 are the weights for the winning challenger (new champion) and placed in slot 0
     * w3 w0 w2 w0 w4 // w0 are the old champ's weights and place in slot 1, other weights are irrelevant
     */
    weights.move_offsets(winning_challenger_slot, old_champ_slot, wpp, true);
    if (winning_challenger_slot != 1) { weights.move_offsets(winning_challenger_slot, 1, wpp, false); }

    this->total_champ_switches++;
    while (!index_queue.empty()) { index_queue.pop(); };
    estimators[winning_challenger_slot].first.eligible_to_inactivate = false;
    if (priority_challengers > 1) { estimators[old_champ_slot].first.eligible_to_inactivate = false; }
    exclusion_config new_champ_config = configs[estimators[winning_challenger_slot].first.config_index];
    exclusion_config old_champ_config = configs[estimators[old_champ_slot].first.config_index];
    configs[0] = std::move(new_champ_config);
    configs[1] = std::move(old_champ_config);
    estimators[winning_challenger_slot].first.config_index = 0;
    estimators[old_champ_slot].first.config_index = 1;
    auto champ_estimator = std::move(estimators[winning_challenger_slot]);
    auto old_champ_estimator = std::move(estimators[old_champ_slot]);
    estimators.clear();
    estimators.push_back(std::move(champ_estimator));
    estimators.push_back(std::move(old_champ_estimator));
    assert(current_champ == 0);
    _config_oracle.valid_config_size = 2;

    /*
     * These operations rearrange the scoring data to sync up the new champion and old champion. Assume the first
     * challenger (chal_1) is taking over the champ (old_champ). The estimators would have this configuration before a
     * champ change: slot_0: <old_champ_dummy, old_champ_dummy_track_champ> slot_1: <chal_1, chal_1_track_champ> Note
     * that the estimators <old_champ_dummy, old_champ_dummy_track_champ> are not updated since the champion does not
     * need to track itself. After the champ change, the same estimators will look like this: slot_0: <chal_1,
     * chal_1_track_champ> slot_1: <old_champ_dummy, old_champ_dummy_track_champ> We then want to move the statistics
     * from chal_1_track_champ to old_champ_dummy and chal_1 to old_champ_dummy_track_champ since they both share the
     * same horizons, but have swapped which is champion and which is challenger. While we want to swap these
     * statistics, we don't want to alter the other state such as interactions and config_index.
     */
    estimators[1].first = aml_estimator(std::move(estimators[0].second), estimators[1].first.config_index,
        estimators[1].first.eligible_to_inactivate, estimators[1].first.live_interactions);
    estimators[1].second = estimators[0].first;
    if (lb_trick)
    {
      estimators[1].first.reset_stats();
      estimators[1].second.reset_stats();
    }
    _config_oracle.do_work(estimators, current_champ);
  }
}
}  // namespace automl
}  // namespace reductions
}  // namespace VW
