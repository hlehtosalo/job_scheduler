# Lock-free job scheduler
Personal project, not meant for distribution!
This was mostly made as an excercise, although I might update it if and when I start using it in projects.

The scheduler is based on the Chase-Lev work-stealing deque, specifically the optimized implementation with more relaxed memory operations, described here: https://fzn.fr/readings/ppopp13.pdf

A notable difference in my deque implementation is that it has a fixed capacity instead of growing dynamically. I chose this because the scheduler is meant for game engine use, where I consider the more stable performance important, and the unbounded capacity unnecessary. The dequeue capacity, along with other compile-time parameters, is defined in Config.h.

The scheduler runs predefined (or defined between runs, i.e. frames) job dependency graphs: Directed acyclic graphs, where each node corresponds to a job. Any job can spawn new jobs in two different ways: As sub-jobs that need to be completed before the node they belong to is considered completed, and as free jobs whose only synchronization guarantee is that they are completed before the end of the whole run. Because the jobs corresponding to nodes typically spawn new jobs, they are called root jobs in the code.

One limitation of the scheduler is that it's not particularly ergonomic to write parallel divide-and-conquer algorithms on it. User code needs to handle buffers for intermediate results, as opposed to simply waiting for the recursively called functions to return their results. An example of this, a parallel sum, can be seen in the benchmark / test code in Main.cpp.

The code in Main.cpp is a simple correctness test, and performance benchmark against a basic single-threaded implementation. A large number of simple but quite expensive hashes are computed and written to a vector, followed by adding all the numbers together. It's not the best of tests, but it demonstrates the basic usage of the scheduler and depencencies, as well as the logging of profiling data.

Future work would likely focus on a more flexible dependency model, such as being able to modify the job graph while it's executed, and on utility code to make high-level concepts easier to implement.
