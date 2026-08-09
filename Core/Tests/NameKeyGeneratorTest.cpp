/*
** Command & Conquer Generals(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
*/

#include "Common/GameMemory.h"
#include "Common/NameKeyGenerator.h"

#include <cstdio>

int main()
{
	initMemoryManager();
	int result = 0;

	{
		NameKeyGenerator generator;
		generator.init();

		const NameKeyType lowercaseCharKey = generator.nameToLowercaseKey("Test1234567");
		const NameKeyType regularCharKey = generator.nameToKey("test1234567");
		if (lowercaseCharKey != regularCharKey)
		{
			std::fprintf(stderr, "const char* overload returned different keys: %d and %d\n", lowercaseCharKey, regularCharKey);
			result |= 1;
		}

		generator.reset();

		const AsciiString mixedCaseName("AnotherTest1234567");
		const AsciiString lowercaseName("anothertest1234567");
		const NameKeyType lowercaseStringKey = generator.nameToLowercaseKey(mixedCaseName);
		const NameKeyType regularStringKey = generator.nameToKey(lowercaseName);
		if (lowercaseStringKey != regularStringKey)
		{
			std::fprintf(stderr, "AsciiString overload returned different keys: %d and %d\n", lowercaseStringKey, regularStringKey);
			result |= 2;
		}
	}

	shutdownMemoryManager();
	if (result == 0)
		std::puts("NameKeyGenerator lowercase cache-miss tests passed");

	return result;
}
