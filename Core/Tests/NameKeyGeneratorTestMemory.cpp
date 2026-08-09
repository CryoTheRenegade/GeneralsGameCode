/*
** Command & Conquer Generals(tm)
** Copyright 2025 TheSuperHackers
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "PreRTS.h"

#include <cstdlib>
#include <cstring>

MemoryPoolFactory* TheMemoryPoolFactory = nullptr;
DynamicMemoryAllocator* TheDynamicMemoryAllocator = nullptr;

namespace
{
	MemoryPoolFactory memoryPoolFactory;
	DynamicMemoryAllocator dynamicMemoryAllocator;
	Bool memoryManagerInitialized = false;
}

void* DynamicMemoryAllocator::allocateBytesDoNotZeroImplementation(Int numBytes)
{
	void* memory = std::malloc(numBytes);
	if (memory == nullptr)
		std::abort();

	return memory;
}

void* DynamicMemoryAllocator::allocateBytesImplementation(Int numBytes)
{
	void* memory = allocateBytesDoNotZeroImplementation(numBytes);
	std::memset(memory, 0, numBytes);
	return memory;
}

void DynamicMemoryAllocator::freeBytes(void* memory)
{
	std::free(memory);
}

Int DynamicMemoryAllocator::getActualAllocationSize(Int numBytes)
{
	return numBytes;
}

void initMemoryManager()
{
	TheMemoryPoolFactory = &memoryPoolFactory;
	TheDynamicMemoryAllocator = &dynamicMemoryAllocator;
	memoryManagerInitialized = true;
}

Bool isMemoryManagerOfficiallyInited()
{
	return memoryManagerInitialized;
}

SubsystemInterface::SubsystemInterface()
{
}

SubsystemInterface::~SubsystemInterface()
{
}

const char* INI::getNextToken(const char*)
{
	return "";
}

void shutdownMemoryManager()
{
	memoryManagerInitialized = false;
	TheDynamicMemoryAllocator = nullptr;
	TheMemoryPoolFactory = nullptr;
}
