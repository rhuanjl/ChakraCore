//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "RuntimeLibraryPch.h"

using namespace Js;

FunctionInfo JavascriptAsyncFunction::functionInfo(
    FORCE_NO_WRITE_BARRIER_TAG(JavascriptAsyncFunction::EntryAsyncFunctionImplementation),
    (FunctionInfo::Attributes)(FunctionInfo::DoNotProfile | FunctionInfo::ErrorOnNew));

JavascriptAsyncFunction::JavascriptAsyncFunction(
    DynamicType* type,
    GeneratorVirtualScriptFunction* scriptFunction) :
        JavascriptGeneratorFunction(type, &functionInfo, scriptFunction)
{
    DebugOnly(VerifyEntryPoint());
}

JavascriptAsyncFunction* JavascriptAsyncFunction::New(
    ScriptContext* scriptContext,
    GeneratorVirtualScriptFunction* scriptFunction)
{
    return scriptContext->GetLibrary()->CreateAsyncFunction(
        functionInfo.GetOriginalEntryPoint(),
        scriptFunction);
}

template<>
bool Js::VarIsImpl<JavascriptAsyncFunction>(RecyclableObject* obj)
{
    return VarIs<JavascriptFunction>(obj) && (
        VirtualTableInfo<JavascriptAsyncFunction>::HasVirtualTable(obj) ||
        VirtualTableInfo<CrossSiteObject<JavascriptAsyncFunction>>::HasVirtualTable(obj)
    );
}

Var JavascriptAsyncFunction::EntryAsyncFunctionImplementation(
    RecyclableObject* function,
    CallInfo callInfo, ...)
{
    auto* scriptContext = function->GetScriptContext();
    PROBE_STACK(scriptContext, Js::Constants::MinStackDefault);
    ARGUMENTS(args, callInfo);

    auto* library = scriptContext->GetLibrary();
    auto* asyncFn = VarTo<JavascriptAsyncFunction>(function);
    auto* scriptFn = asyncFn->GetGeneratorVirtualScriptFunction();
    auto* generator = library->CreateGenerator(args, scriptFn, library->GetNull());
    auto* promise = library->CreatePromise();

    JavascriptExceptionObject* exception = nullptr;
    JavascriptPromiseResolveOrRejectFunction* resolve;
    JavascriptPromiseResolveOrRejectFunction* reject;
    JavascriptPromise::InitializePromise(promise, &resolve, &reject, scriptContext);

    auto* successFunction = library->CreateAsyncSpawnStepFunction(
        EntryAsyncSpawnStepNextFunction,
        generator,
        resolve,
        reject);

    auto* failFunction = library->CreateAsyncSpawnStepFunction(
        EntryAsyncSpawnStepThrowFunction,
        generator,
        resolve,
        reject,
        successFunction);

    successFunction->otherMethod = failFunction;

    try
    {
        AsyncSpawnStep(library->GetUndefined(), successFunction, failFunction, library->EnsureGeneratorNextFunction());
    }
    catch (const JavascriptException& err)
    {
        exception = err.GetAndClear();
    }

    if (exception != nullptr)
        JavascriptPromise::TryRejectWithExceptionObject(exception, reject, scriptContext);

    return promise;
}

Var JavascriptAsyncFunction::EntryAsyncSpawnStepNextFunction(
    RecyclableObject* function,
    CallInfo callInfo, ...)
{
    auto* scriptContext = function->GetScriptContext();
    auto* library = scriptContext->GetLibrary();
    auto* undefinedVar = library->GetUndefined();
    PROBE_STACK(scriptContext, Js::Constants::MinStackDefault);
    ARGUMENTS(args, callInfo);
    Var resolvedValue = args.Info.Count > 1 ? args[1] : undefinedVar;

    auto* successFunction = VarTo<JavascriptAsyncSpawnStepFunction>(function);
    AsyncSpawnStep(resolvedValue, successFunction, VarTo<JavascriptAsyncSpawnStepFunction>(successFunction->otherMethod), library->EnsureGeneratorNextFunction());

    return undefinedVar;
}

Var JavascriptAsyncFunction::EntryAsyncSpawnStepThrowFunction(
    RecyclableObject* function,
    CallInfo callInfo, ...)
{
    auto* scriptContext = function->GetScriptContext();
    auto* library = scriptContext->GetLibrary();
    auto* undefinedVar = library->GetUndefined();
    PROBE_STACK(scriptContext, Js::Constants::MinStackDefault);
    ARGUMENTS(args, callInfo);
    Var resolvedValue = args.Info.Count > 1 ? args[1] : undefinedVar;

    auto* failFunction = VarTo<JavascriptAsyncSpawnStepFunction>(function);
    AsyncSpawnStep(resolvedValue, VarTo<JavascriptAsyncSpawnStepFunction>(failFunction->otherMethod), failFunction, library->EnsureGeneratorThrowFunction());

    return undefinedVar;
}

void JavascriptAsyncFunction::AsyncSpawnStep(Var resolvedValue, 
    JavascriptAsyncSpawnStepFunction* successFunction,
    JavascriptAsyncSpawnStepFunction* failFunction,
    JavascriptFunction* generatorMethod)
{
    auto* scriptContext = successFunction->GetScriptContext();
    JavascriptGenerator* generator = successFunction->generator;
    RecyclableObject* result = nullptr;

    BEGIN_SAFE_REENTRANT_REGION(scriptContext->GetThreadContext())

    try
    {
        BEGIN_SAFE_REENTRANT_CALL(scriptContext->GetThreadContext())
        {
            Var resultVar = CALL_FUNCTION(
                scriptContext->GetThreadContext(),
                generatorMethod,
                CallInfo(CallFlags_Value, 2),
                static_cast<Var>(generator),
                static_cast<Var>(resolvedValue));
            result = VarTo<RecyclableObject>(resultVar);
        }
        END_SAFE_REENTRANT_CALL
    }
    catch (const JavascriptException& err)
    {
        JavascriptExceptionObject* exception = err.GetAndClear();
        JavascriptPromise::TryRejectWithExceptionObject(exception, successFunction->reject, scriptContext);
        return;
    }

    JavascriptLibrary* library = scriptContext->GetLibrary();
    Var undefinedVar = library->GetUndefined();

    Assert(result != nullptr);

    Var value = JavascriptOperators::GetProperty(result, PropertyIds::value, scriptContext);

    // If the generator is done, resolve the promise
    if (generator->IsCompleted())
    {
        if (!JavascriptConversion::IsCallable(successFunction->resolve))
            JavascriptError::ThrowTypeError(scriptContext, JSERR_NeedFunction);

        CALL_FUNCTION(
            scriptContext->GetThreadContext(),
            VarTo<RecyclableObject>(successFunction->resolve),
            CallInfo(CallFlags_Value, 2),
            undefinedVar,
            value);

        return;
    }
    else
    {
        Assert(JavascriptOperators::GetTypeId(result) == TypeIds_AwaitObject);
    }

    auto* promise = JavascriptPromise::InternalPromiseResolve(value, scriptContext);
    auto* unused = JavascriptPromise::UnusedPromiseCapability(scriptContext);
    
    JavascriptPromise::PerformPromiseThen(
        promise,
        unused,
        successFunction,
        failFunction,
        scriptContext);

    END_SAFE_REENTRANT_REGION
}

template<>
bool Js::VarIsImpl<JavascriptAsyncSpawnStepFunction>(RecyclableObject* obj)
{
    return VarIs<JavascriptFunction>(obj) && (
        VirtualTableInfo<JavascriptAsyncSpawnStepFunction>::HasVirtualTable(obj) ||
        VirtualTableInfo<CrossSiteObject<JavascriptAsyncSpawnStepFunction>>::HasVirtualTable(obj)
    );
}

#if ENABLE_TTD

TTD::NSSnapObjects::SnapObjectType JavascriptAsyncFunction::GetSnapTag_TTD() const
{
    return TTD::NSSnapObjects::SnapObjectType::SnapAsyncFunction;
}

void JavascriptAsyncFunction::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
{
    TTD::NSSnapObjects::SnapGeneratorFunctionInfo* fi = nullptr;
    uint32 depCount = 0;
    TTD_PTR_ID* depArray = nullptr;

    this->CreateSnapObjectInfo(alloc, &fi, &depArray, &depCount);

    if (depCount == 0)
    {
        TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapGeneratorFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::SnapAsyncFunction>(objData, fi);
    }
    else
    {
        TTDAssert(depArray != nullptr, "depArray should be non-null if depCount is > 0");
        TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapGeneratorFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::SnapAsyncFunction>(objData, fi, alloc, depCount, depArray);
    }
}

void JavascriptAsyncSpawnStepFunction::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
{
    if (this->generator != nullptr)
    {
        extractor->MarkVisitVar(this->generator);
    }

    if (this->reject != nullptr)
    {
        extractor->MarkVisitVar(this->reject);
    }

    if (this->resolve != nullptr)
    {
        extractor->MarkVisitVar(this->resolve);
    }

    if (this->otherMethod != nullptr)
    {
        extractor->MarkVisitVar(this->otherMethod);
    }
}

TTD::NSSnapObjects::SnapObjectType JavascriptAsyncSpawnStepFunction::GetSnapTag_TTD() const
{
    return TTD::NSSnapObjects::SnapObjectType::JavascriptAsyncSpawnStepFunction;
}

void JavascriptAsyncSpawnStepFunction::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
{
    TTD::NSSnapObjects::SnapJavascriptAsyncSpawnStepFunctionInfo* info = alloc.SlabAllocateStruct<TTD::NSSnapObjects::SnapJavascriptAsyncSpawnStepFunctionInfo>();
    info->generator = TTD_CONVERT_VAR_TO_PTR_ID(this->generator);
    info->reject = this->reject;
    info->resolve = this->resolve;
    info->otherMethod = TTD_CONVERT_VAR_TO_PTR_ID(this->otherMethod);

    info->entryPoint = 0;
    JavascriptMethod entryPoint = this->GetFunctionInfo()->GetOriginalEntryPoint();
    if (entryPoint == JavascriptAsyncFunction::EntryAsyncSpawnStepNextFunction)
    {
        info->entryPoint = 1;
    }
    else if (entryPoint == JavascriptAsyncFunction::EntryAsyncSpawnStepThrowFunction)
    {
        info->entryPoint = 2;
    }
    else
    {
        TTDAssert(false, "Unexpected entrypoint found JavascriptAsyncSpawnStepArgumentExecutorFunction");
    }

    const uint32 maxDeps = 4;
    uint32 depCount = 0;
    TTD_PTR_ID* depArray = alloc.SlabReserveArraySpace<TTD_PTR_ID>(maxDeps);
    if (this->reject != nullptr &&  TTD::JsSupport::IsVarComplexKind(this->reject))
    {
        depArray[depCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->reject);
        depCount++;
    }

    if (this->resolve != nullptr &&  TTD::JsSupport::IsVarComplexKind(this->resolve))
    {
        depArray[depCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->resolve);
        depCount++;
    }

    if (this->otherMethod != nullptr &&  TTD::JsSupport::IsVarComplexKind(this->otherMethod))
    {
        depArray[depCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->otherMethod);
        depCount++;
    }

    if (this->generator != nullptr)
    {
        depArray[depCount] = TTD_CONVERT_VAR_TO_PTR_ID(this->generator);
        depCount++;
    }

    if (depCount > 0)
    {
        alloc.SlabCommitArraySpace<TTD_PTR_ID>(depCount, maxDeps);
    }
    else
    {
        alloc.SlabAbortArraySpace<TTD_PTR_ID>(maxDeps);
    }

    if (depCount == 0)
    {
        TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapJavascriptAsyncSpawnStepFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::JavascriptAsyncSpawnStepFunction>(objData, info);
    }
    else
    {
        TTDAssert(depArray != nullptr, "depArray should be non-null if depCount is > 0");
        TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapJavascriptAsyncSpawnStepFunctionInfo*, TTD::NSSnapObjects::SnapObjectType::JavascriptAsyncSpawnStepFunction>(objData, info, alloc, depCount, depArray);
    }
}

#endif
