# Using Switchboard and Phonebook Externally

Switchboard was designed as a self-contained entity from ILLIXR that one can reuse in other
projects. The relevant API is [here for Switchboard][switchboard_api] and [here for
Phonebook][phonebook_api].

One simply needs to copy these files, maintaining directory structure.

```
illixr/switchboard.hpp
illixr/phonebook.hpp
illixr/record_logger.hpp
illixr/managed_thread.hpp
illixr/concurrentqueue/blockingconcurrentqueue.hpp
illixr/concurrentqueue/concurrentqueue.hpp
illixr/concurrentqueue/lightweightsemaphore.hpp
```

This will serve as our `main.cpp`:

```
#include <iostream>
#include "illixr/switchboard.hpp"

class service : public ILLIXR::phonebook::service {
public:
    void act() { std::cout << "Hello from service\n"; };
};

class data : public ILLIXR::switchboard::event {
public:
    data(size_t id_) : id{id_} { }
    size_t id;
};

int main() {
    ILLIXR::phonebook main_pb;
    main_pb.register_impl<service>(std::make_shared<service>());
    main_pb.lookup_impl<service>()->act();

    // From docs of Switchboard: if first arg is null, logging is disabled.
	// Logging should be disabled if we are running externally.
    ILLIXR::switchboard main_sb {nullptr};
    auto writer = main_sb.get_writer<data>("topic");
    auto reader = main_sb.get_reader<data>("topic");
    writer.put(writer.allocate<data>(42));
    std::cout << "The answer to life... is " << reader.get_ro()->id << std::endl;

    return 0;
}
```

Switchboard and Phonebook will only work with compilers that support C++17 or better.

For example:

```
# Must copy with directory structure
mkdir -p illixr/concurrentqueue
cp path/to/ILLIXR/illixr/switchboard.hpp illixr
cp path/to/ILLIXR/illixr/phonebook.hpp illixr
cp path/to/ILLIXR/illixr/record_logger.hpp illixr
cp path/to/ILLIXR/illixr/managed_thread.hpp illixr
cp path/to/ILLIXR/illixr/concurrentqueue/blockingconcurrentqueue.hpp illixr/concurrentqueue/blockingconcurrentqueue.hpp
cp path/to/ILLIXR/illixr/concurrentqueue/concurrentqueue.hpp illixr/concurrentqueue/concurrentqueue.hpp
cp path/to/ILLIXR/illixr/concurrentqueue/lightweightsemaphore.hpp illixr/concurrentqueue/lightweightsemaphore.hpp
emacs main.cpp # copy and paste from this doc

# This will *probbaly* work with any C++-17 compatible compiler, but I've tested with clang-10.
# Nix is my preferred package manager, but you can use whichever you like.
# This command will not affect system packages, just create a temporary environment with the right clang.
nix-shell -p clang_10

# Compile
clang++ -Wextra -pthread -std=c++17 main.cpp

# Run
./a.out
```

The output is:

```
Register 7service
Hello from service
Creating: topic for 4data
The answer to life... is 42
```

[switchboard_api]: https://illixr.github.io/ILLIXR/api/html/classILLIXR_1_1switchboard.html
[phonebook_api]: https://illixr.github.io/ILLIXR/api/html/classILLIXR_1_1phonebook.html
