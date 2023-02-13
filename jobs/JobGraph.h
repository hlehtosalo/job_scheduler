#pragma once

#include <type_traits>
#include <memory>
#include <vector>
#include <atomic>
#include <cstring>
#include <cassert>

#include "Job.h"

namespace jobs {

	class JobQueue;
	class JobGraph;

	// Node in a JobGraph. Contains a single root Job that will be run when all nodes this depends on are completed.
	// The root Job can then spawn sub-Jobs which need to be completed for the node to be considered completed.
	class JobGraphNode {
	public:
		using Size = uint32_t;
		using AtomicSize = std::atomic<Size>;
		static_assert(AtomicSize::is_always_lock_free, "JobGraphNode will work without this, but may not be lock-free. It wants to be lock-free.");

		JobGraphNode(const JobGraphNode&) = delete;
		JobGraphNode(JobGraphNode&&) = delete;
		JobGraphNode& operator=(const JobGraphNode&) = delete;
		JobGraphNode& operator=(JobGraphNode&&) = delete;
		// Called by JobSpawner when a new Job is spawned as a sub-Job.
		void job_added();
		// Called by Job after running its function.
		void job_completed(JobQueue& queue);
		const Job* get_root_job() const;

	private:
		friend class JobGraph;

		template<typename Params>
		JobGraphNode(JobFunction* root_job_function, const Params& params, const JobGraph* owner);
		void add_successor(JobGraphNode& successor);
		bool is_ancestor_of(const JobGraphNode* descendant) const;

		Job root_job;
		Size initial_predecessor_amount = 0;
		AtomicSize predecessor_amount = 0;
		AtomicSize unfinished_amount = 1;
		std::vector<JobGraphNode*> successors;
		const JobGraph* owner;
	};

	template<typename Params>
	inline JobGraphNode::JobGraphNode(JobFunction* root_job_function, const Params& params, const JobGraph* owner) : owner(owner) {
		static_assert(sizeof(Params) <= param_buffer_size, "Params has to fit into Job::param_buffer. Data that does not fit needs to be allocated elsewhere and pointed to in Params.");
		static_assert(std::is_trivial_v<Params>, "Params has to be a trivial type. The data is memcpy'd into Job::param_buffer.");
		std::memcpy(root_job.param_buffer, &params, sizeof(Params));
		root_job.function = root_job_function;
		root_job.node = this;
	}

	inline void JobGraphNode::job_added() {
		unfinished_amount.fetch_add(1, std::memory_order::relaxed);
	}

	inline const Job* JobGraphNode::get_root_job() const {
		return &root_job;
	}

	// Dependency graph for Jobs. Not generally meant to be modified while it is being run; dynamic dispatch can instead be achieved by
	// having a Job function spawn sub-Jobs into its own node based on some state (external to the job system).
	// It would be possible to modify the graph on the fly, but would need to make sure dependencies are not broken.
	class JobGraph {
	public:
		// Creates a node with no prior dependencies. The root jobs of all such nodes will begin executing when the Scheduler runs this graph.
		template<typename Params>
		JobGraphNode* new_node(JobFunction* root_job_function, const Params& params);
		// Creates a node that depends on given predecessor nodes. All predecessors are passed at once to enforce an acyclic graph, meaning no circular dependencies.
		template<typename Params, size_t N>
		JobGraphNode* new_node(JobFunction* root_job_function, const Params& params, JobGraphNode* (&predecessors)[N]);
		// Creates a node that depends on given predecessor nodes. All predecessors are passed at once to enforce an acyclic graph, meaning no circular dependencies.
		template<typename Params, size_t N>
		JobGraphNode* new_node(JobFunction* root_job_function, const Params& params, JobGraphNode* (&&predecessors)[N]);
		// Called by Scheduler in order to start running the graph. Returns a pointer to the root job of a root node, null if indexing out of bounds.
		const Job* get_root_job(uint32_t index) const;

	private:
		std::vector<std::unique_ptr<JobGraphNode>> nodes;
		std::vector<JobGraphNode*> root_nodes;
	};

	template<typename Params>
	inline JobGraphNode* JobGraph::new_node(JobFunction* root_job_function, const Params& params) {
		nodes.push_back(std::unique_ptr<JobGraphNode>(new JobGraphNode(root_job_function, params, this)));
		JobGraphNode* node = nodes.back().get();
		root_nodes.push_back(node);
		return node;
	}

	template<typename Params, size_t N>
	inline JobGraphNode* JobGraph::new_node(JobFunction* root_job_function, const Params& params, JobGraphNode* (&predecessors)[N]) {
		nodes.push_back(std::unique_ptr<JobGraphNode>(new JobGraphNode(root_job_function, params, this)));
		JobGraphNode* node = nodes.back().get();
		for (JobGraphNode* predecessor : predecessors) {
			assert(predecessor->owner == this);
			bool redundant = false;
			for (const JobGraphNode* descendant_candidate : predecessors) {
				if (descendant_candidate != predecessor && predecessor->is_ancestor_of(descendant_candidate)) {
					redundant = true;
					break;
				}
			}
			if (!redundant) {
				predecessor->add_successor(*node);
			}
		}
		return node;
	}

	template<typename Params, size_t N>
	inline JobGraphNode* JobGraph::new_node(JobFunction* root_job_function, const Params& params, JobGraphNode* (&&predecessors)[N]) {
		return new_node(root_job_function, params, predecessors);
	}

	inline const Job* JobGraph::get_root_job(uint32_t index) const {
		if (index >= root_nodes.size()) {
			return nullptr;
		}
		return root_nodes[index]->get_root_job();
	}

}