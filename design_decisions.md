# Threads vs processes -> executables vs shared-objects

The threads vs procsses is a false fight. I think the primary distinction should be between distributing executables or distributing shared-objects. Distributing executables locks the RT into putting each into its own process, while distributing shared-objects leaves the possibility for either processes or threads, depending on the context.

## Distribute executables and shared-objects

Each component would be a separate binary; Then each component is binary is `exec`ed into separate processes which all talk to each other. This mandates the process model where possible.

This is how people distribute loosely coupled components such as [webserver CGI](https://en.wikipedia.org/wiki/Common_Gateway_Interface).

### Pros
- Components can be written in other languages, as long as they speak the correct IPC (could be specified in gRPC).

- Components can be run remotely and accessed over the network.

- Independent tests and independent builds.

- Coarse-grain fault-tolerance comes for free.
  - A segfault kills the whole process, but that process can be restarted.
  - However, faults which result in infinite loops, livelock, or deadlock will still degrade the system. A heartbeat must be kept to detect these, and that is no easier than with shared-objects and threads.
  - However, some performance and state will be degraded, because unrelated components in the same process will be terminated (this is what I mean by coarse-grain).

- Coarse-grain hot-swappable.
  - However, `vfork`/`exec` make this slow (100ms). It depends on the circumstance whether this delay matters.
  - However, a whole process has to be taken down, so adjacent unrelated components will also have to be swapped or restarted.

### Cons

- It becomes difficult to create new high data-bandwidth dependencies between existing components; However, this might not be necessary.

- Passing OpenGL handles between processes might not work; more work needs to be done to determine this.

- Locks components into being processes.
  - With shared-objects we can do either; We can even switch from processes to threads without changing the component libraries.

- We would have to use shared-objects for high data-bandwidth dependencies anyway, so this becomes a second pipeline to maintain.

- IPC has higher latency (ms compared to shared-memory's ns). This could be acceptable, depending on the process boundary.
  - This could be fixed by using `mmap` to gain shared-memory between processes.

## Distribute shared-objects

The user downloads (or builds) a bunch of component .so libraries. The user tells the RT about the path to each component they want, and the RT loads them in (`dlopen`/`dlsym`) and calls methods from them.

This is how Python distributes [third-party C-modules](https://docs.python.org/3.8/extending/building.html#building), and how LLVM distributes [third-party passes](https://releases.llvm.org/8.0.0/docs/WritingAnLLVMPass.html#registering-dynamically-loaded-passes).

### Pros
- Components can be written in other languages, as long as they have a C++ adapter class.
  - This is slightly more work than speaking the correct IPC, however it is not a lot of work. One would have to write a "stub" C++ class that satisfies the C++ interface, while forwarding data to whatever language (most likely under 30 sLOC). We could provide Python wrappers to simplify the common-case.

- Components can be run remotely and accessed over the network.
  - Remote component authors would write a local stub class that forwards to their remote class (much like the previous case).

- Component-authors can independently test and build.

- Components are fine-grain hot-swappable.
  - Individual threads can be changed without changing any others. In the process-case, neighboring components would also be restarted.

- Components have fine-grain fault-tolerance.
  - One thread failing does not even effect the other threads (better than the process case).

- Component-authors can decide to use processes or threads internally. The RT doesn't care, as long as they properly manage their resources.

### Cons

- Component-authors need to write stub classes to do some things.

- Might have more threads than strictly necessary, with some idling.
  - This should be no problem for a smart scheduler.

# Future questions

- How often will fault-tolerance be useful?
  - In most cases I can think of, a fault indicates a botched experiment, and the right thing to do is fix the code.
  - Do video-game systems often have this kind of fault-tolerance? I think that if there is a fault in a video game, the state could be corrupted, so they prefer to failing quickly than trying to recover.

- Test fault-tolerance and killing threads using `clone` syscall.

- Think about the larger design-structure. Will this be pub/sub topics like ROS? Will this be event-driven like NodeJS? How will that integrate with a realtime scheduler?

# Future recommendations (in either case)

- Use seperate repositories for interface definitions.
  - Component builds need to pull only this--not the rest of the RT--to build against.
  - This encourages ILLIXR devs to change interface as little as possible.
  - Use Semantic Versioning (semver) for interfaces.
    - https://semver.org/
    - Allows one to automatically determine if components are compatible with each other and a specific version of the RT.
  - Use FlatBuffers, not protobufs
    - FlatBuffers have almost the same speed as C-structs, but are more language-neutral.
    - https://google.github.io/flatbuffers/flatbuffers_benchmarks.html
    - Inside the RT, classes can wrap a FlatBuffer and provide OOPy features.

- Use assert to encode RT's expectations (in terms of preconditions/postconditions) for components.
  - This makes it easier to move to statically verifiable "contract-programming" in future.

- Use a YAML configuration instead of arg-parsing.
  - Otherwise, we duplicate work and duplicate the location of settings.

- I am not yet convinced we need Nix if we use bazel smartly, but perhaps I will be convinced later.
  - Bazel can handle different lib-versions and different languages.

- Set up linting, unittesting, and automatic formatting on commit-hooks. The result of unittests should be displayed with continuous integration.

- Wherever possible, the RT should own the concurrent-main-loop, and call out to the component synchronously. Then the component author does not need to write concurrent code, and we can ensure that the concurrency is done properly.
  - This is not possible for all components; SLAM probably wants to manage its own main-loop.

- We should write correctness and accuracy tests for component-authors to pull.
  - This reduces the setup-time for component-authors and ensures some quality.
  - Could distribute as a "template" repo.
