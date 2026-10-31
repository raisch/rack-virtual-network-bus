# Test layout

Portable tests must not require VCV Rack or a live network unless explicitly
marked as integration tests.

The planned test boundaries are:

- protocol and topic unit tests;
- discovery and stream state-machine tests;
- CV and gate/trigger event tests;
- deterministic network-impairment tests;
- platform and live-multicast integration tests.

## M0 smoke test

From the repository root:

```sh
make -C tests test
```

The same source can be compiled directly with a C++20 compiler:

```sh
c++ -std=c++11 -Wall -Wextra -Wpedantic -Werror \
  tests/smoke_test.cc -o tests/smoke_test
tests/smoke_test
```

On Windows, use the repository's available C++20 compiler, for example:

```bat
cl /std:c++14 /W4 /WX tests\smoke_test.cc /Fe:tests\smoke_test.exe
tests\smoke_test.exe
```
