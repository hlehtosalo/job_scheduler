#include "JobGraph.h"

#include "JobQueue.h"

namespace jobs {

	void JobGraphNode::job_completed(JobQueue& queue) {
		const Size old_unfinished_amount = unfinished_amount.fetch_sub(1, std::memory_order::seq_cst);
		assert(old_unfinished_amount > 0);
		if (old_unfinished_amount > 1) {
			return;
		}
		for (JobGraphNode* successor : successors) {
			const Size old_predecessor_amount = successor->predecessor_amount.fetch_sub(1, std::memory_order::relaxed);
			assert(old_predecessor_amount > 0);
			if (old_predecessor_amount == 1) {
				const bool pushed = queue.push(&successor->root_job);
				assert(pushed);
			}
		}
		unfinished_amount.store(1, std::memory_order::relaxed);
		predecessor_amount.store(initial_predecessor_amount, std::memory_order::relaxed);
	}

	void JobGraphNode::add_successor(JobGraphNode& successor) {
		successors.push_back(&successor);
		successor.initial_predecessor_amount++;
		successor.predecessor_amount.store(successor.initial_predecessor_amount, std::memory_order::relaxed);
	}

	bool JobGraphNode::is_ancestor_of(const JobGraphNode* descendant) const {
		for (const JobGraphNode* successor : successors) {
			if (successor == descendant) {
				return true;
			}
		}
		for (const JobGraphNode* successor : successors) {
			if (successor->is_ancestor_of(descendant)) {
				return true;
			}
		}
		return false;
	}

}