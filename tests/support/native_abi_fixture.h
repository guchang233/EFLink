#pragma once
// Test-owned objects only. These are NOT declarations of game/IL2CPP layouts.
struct AbiFixtureMethod;
struct AbiFixtureClass {bool value;int bytes;unsigned alignment;const AbiFixtureClass* parent;const AbiFixtureMethod* method=nullptr;};
struct AbiFixtureMethod {const AbiFixtureClass* owner;const AbiFixtureMethod* implementation;unsigned flags,count;
    const char* name="get_type";const char* returns="System.Int32";};
struct AbiFixtureObject {const AbiFixtureClass* klass;alignas(16) unsigned char data[16];};
