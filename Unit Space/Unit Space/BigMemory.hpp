//
//  BigMemory.hpp
//  Unit Space
//
//  Created by Nigel Barber on 01/06/2018.
//  Copyright © 2018 Phyllotaxis Limited. All rights reserved.
//
#import <sys/mman.h>

constexpr size_t kBigMemorySize = 4294967296;

template<typename T>
struct BigMemory {
    BigMemory() {
        header = (Header *)malloc(sizeof(Header));
        header->memory = mmap(NULL, kBigMemorySize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, 0, 0);
        header->end = header->begin = (T *)header->memory;
        header->refCount = 1;
    }
    BigMemory(const BigMemory& other)         { assign(other); }
    BigMemory& operator= (BigMemory other)    { assign(other); return *this; }
    ~BigMemory() {
        if (--(header->refCount) == 0) {
            munmap(header->memory, kBigMemorySize);
            free(header);
        }
    }
    void assign(const BigMemory& other) {
        if (this != & other) {
            this->~BigMemory();
            header = other.header;
            header->refCount++;
        }
    }
    size_t count() { return header->end - header->begin; }
    size_t size() { return (header->end - header->begin) * sizeof(T); }
    T *get() const { return header->begin; }
    T *next() const { return header->end++; }
private:
    struct Header { int refCount; void *memory; T *begin, *end; };
    Header *header;
};
