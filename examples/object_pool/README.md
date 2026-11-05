# Fixed-Size Object Pool Pattern

Demonstrates how FragmentX automatically handles recurring fixed-size objects:
- Thread-local cache intercepts rapid allocations and deallocations.
- Reuses freed blocks in O(1) time without lock contention.
- Measures high throughput operations per second.

