# Canonical Mixed-Lifetime Workload

This example demonstrates the primary pathology that FragmentX solves:
- Interleaved short-lived objects (mean lifetime ~500 ns) and long-lived objects (held for minutes/process lifetime).
- In a traditional allocator with single shared arenas, freeing short-lived objects leaves scattered "holes" that cannot be reused for larger allocations or reclaimed to the OS because long-lived objects anchor the pages.
- When analyzed by FragmentX (`fragmentx analyze`), it triggers `LIFETIME MIXING DETECTED`.
- Running `fragmentx optimize` synthesizes a `LifetimeGrouped` policy that routes short-lived and long-lived objects into distinct arenas, eliminating this fragmentation entirely.

