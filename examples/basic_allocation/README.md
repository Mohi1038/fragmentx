# Basic Allocation Example

Demonstrates:
- Creating a `fragmentx::Config` and configuring region sizes.
- Instantiating `fragmentx::Allocator`.
- Raw byte allocations and deallocations.
- Typed allocations with lifetime hints (`allocator.construct<T>(LifetimeHint::ShortLived)`).
- Querying runtime heap statistics (`allocator.statistics()`).

