#pragma once
// Test-owned objects only. These are NOT declarations of game/IL2CPP layouts.
struct AbiFixtureMethod;
struct AbiFixtureField;
struct AbiFixtureClass {bool value;int bytes;unsigned alignment;const AbiFixtureClass* parent;const AbiFixtureMethod* method=nullptr;const AbiFixtureField* field=nullptr;};
struct AbiFixtureType {const char* name;const AbiFixtureClass* klass=nullptr;};
struct AbiFixtureMethod {const AbiFixtureClass* owner;const AbiFixtureMethod* implementation;unsigned flags,count;
    const char* name="get_type";AbiFixtureType returns{"System.Int32"};const AbiFixtureType* parameters[3]{};};
struct AbiFixtureField {const char* name;AbiFixtureType type;int value;unsigned flags=0x10;};
struct AbiFixtureObject {const AbiFixtureClass* klass;alignas(16) unsigned char data[16];};
