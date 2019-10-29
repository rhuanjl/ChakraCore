//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#pragma once

namespace Js
{

class JavascriptAsyncFunction : public JavascriptGeneratorFunction
{
private:
    static FunctionInfo functionInfo;

    DEFINE_VTABLE_CTOR(JavascriptAsyncFunction, JavascriptGeneratorFunction);
    DEFINE_MARSHAL_OBJECT_TO_SCRIPT_CONTEXT(JavascriptAsyncFunction);

protected:
    JavascriptAsyncFunction(DynamicType* type);

public:
    JavascriptAsyncFunction(DynamicType* type, GeneratorVirtualScriptFunction* scriptFunction);

    static JavascriptAsyncFunction* New(
        ScriptContext* scriptContext,
        GeneratorVirtualScriptFunction* scriptFunction);
    
    static Var EntryAsyncFunctionImplementation(
        RecyclableObject* function,
        CallInfo callInfo, ...);
    
    
    static Var EntryAsyncSpawnStepNextFunction(
        RecyclableObject* function,
        CallInfo callInfo, ...);

    static Var EntryAsyncSpawnStepThrowFunction(
        RecyclableObject* function,
        CallInfo callInfo, ...);


    static bool Test(JavascriptFunction *obj)
    {
        return 
            VirtualTableInfo<JavascriptAsyncFunction>::HasVirtualTable(obj) ||
            VirtualTableInfo<CrossSiteObject<JavascriptAsyncFunction>>::HasVirtualTable(obj);
    }

#if ENABLE_TTD
    virtual TTD::NSSnapObjects::SnapObjectType GetSnapTag_TTD() const override;
    virtual void ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc) override;
#endif

    virtual VTableValue DummyVirtualFunctionToHinderLinkerICF()
    {
        return VTableValue::VtableJavascriptAsyncFunction;
    }

private:
    static void AsyncSpawnStep(Var resolvedValue, 
        JavascriptAsyncSpawnStepFunction* successFunction,
        JavascriptAsyncSpawnStepFunction* failFunction,
        JavascriptFunction* generatorMethod);
};

template<>
bool VarIsImpl<JavascriptAsyncFunction>(RecyclableObject* obj);

class JavascriptAsyncSpawnStepFunction : public RuntimeFunction
{
protected:
    DEFINE_VTABLE_CTOR(JavascriptAsyncSpawnStepFunction, RuntimeFunction);
    DEFINE_MARSHAL_OBJECT_TO_SCRIPT_CONTEXT(JavascriptAsyncSpawnStepFunction);

public:
    JavascriptAsyncSpawnStepFunction(
        DynamicType* type,
        FunctionInfo* functionInfo,
        JavascriptGenerator* generator,
        Var resolve = nullptr,
        Var reject = nullptr,
        RuntimeFunction* otherMethod = nullptr) :
            RuntimeFunction(type, functionInfo),
            generator(generator),
            resolve(resolve),
            reject(reject),
            otherMethod(otherMethod) {}

    Field(JavascriptGenerator*) generator;
    Field(Var) reject;
    Field(Var) resolve;
    Field(RuntimeFunction*) otherMethod;

#if ENABLE_TTD
    virtual void MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor) override;
    virtual TTD::NSSnapObjects::SnapObjectType GetSnapTag_TTD() const override;
    virtual void ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc) override;
#endif
};

template<>
bool VarIsImpl<JavascriptAsyncSpawnStepFunction>(RecyclableObject* obj);

}
