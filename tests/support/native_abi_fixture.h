#pragma once
#include <vector>
// Test-owned objects only. These are NOT declarations of game/IL2CPP layouts.
struct AbiFixtureMethod;
struct AbiFixtureField;
struct AbiFixtureClass {bool value;int bytes;unsigned alignment;const AbiFixtureClass* parent;const AbiFixtureMethod* method=nullptr;const AbiFixtureField* field=nullptr;const AbiFixtureClass* element=nullptr;int rank=0;};
struct AbiFixtureType {const char* name;const AbiFixtureClass* klass=nullptr;};
struct AbiFixtureMethod {const AbiFixtureClass* owner;const AbiFixtureMethod* implementation;unsigned flags,count;
    const char* name="get_type";AbiFixtureType returns{"System.Int32"};const AbiFixtureType* parameters[3]{};const AbiFixtureMethod* next=nullptr;};
struct AbiFixtureField {const char* name;AbiFixtureType type;int value;unsigned flags=0x10;const AbiFixtureField* next=nullptr;const AbiFixtureClass* owner=nullptr;unsigned offset=0;};
struct AbiFixtureObject {const AbiFixtureClass* klass;alignas(16) unsigned char data[512];};
struct AbiFixtureArray {AbiFixtureObject object;AbiFixtureClass type;std::vector<void*> elements;};
