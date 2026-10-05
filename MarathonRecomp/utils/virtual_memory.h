#pragma once
#ifndef _WIN32
#include <cstddef>
#include <cstdint>
#include <sys/mman.h>

inline uint8_t* AllocateGuestMemory(size_t size)
{
    void* memory = mmap(reinterpret_cast<void*>(0x100000000ull), size,
        PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (memory == MAP_FAILED)
        memory = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (memory == MAP_FAILED)
        return nullptr;
    if (mprotect(memory, 4096, PROT_NONE) != 0)
    {
        munmap(memory, size);
        return nullptr;
    }
    return static_cast<uint8_t*>(memory);
}
#endif
