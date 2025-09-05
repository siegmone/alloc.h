# alloc.h
**Header-only** library for memory allocators in C.

Current allocators:
- Bump/Arena allocator

Currently uses only basic **malloc/free** for allocations, no OS specific API (mmap, sbrk, VirtualAlloc, etc...).
