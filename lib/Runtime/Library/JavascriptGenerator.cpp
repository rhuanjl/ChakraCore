//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#include "RuntimeLibraryPch.h"
#include "Language/InterpreterStackFrame.h"

namespace Js
{
    JavascriptGenerator::JavascriptGenerator(DynamicType* type, Arguments &args, ScriptFunction* scriptFunction)
        : DynamicObject(type), frame(nullptr), state(GeneratorState::Suspended), args(args), scriptFunction(scriptFunction)
    {
    }

    JavascriptGenerator* JavascriptGenerator::New(Recycler* recycler, DynamicType* generatorType, Arguments& args, ScriptFunction* scriptFunction)
    {
#if GLOBAL_ENABLE_WRITE_BARRIER
        if (CONFIG_FLAG(ForceSoftwareWriteBarrier))
        {
            JavascriptGenerator* obj = RecyclerNewFinalized(
                recycler, JavascriptGenerator, generatorType, args, scriptFunction);
            if (obj->args.Values != nullptr)
            {
                recycler->RegisterPendingWriteBarrierBlock(obj->args.Values, obj->args.Info.Count * sizeof(Var));
                recycler->RegisterPendingWriteBarrierBlock(&obj->args.Values, sizeof(Var*));
            }
            return obj;
        }
        else
#endif
        {
            return RecyclerNew(recycler, JavascriptGenerator, generatorType, args, scriptFunction);
        }
    }

    JavascriptGenerator *JavascriptGenerator::New(Recycler *recycler, DynamicType *generatorType, Arguments &args,
        Js::JavascriptGenerator::GeneratorState generatorState)
    {
        JavascriptGenerator *obj = JavascriptGenerator::New(recycler, generatorType, args, nullptr);
        obj->SetState(generatorState);
        return obj;
    }

    template <> bool VarIsImpl<JavascriptGenerator>(RecyclableObject* obj)
    {
        return JavascriptOperators::GetTypeId(obj) == TypeIds_Generator;
    }

    void JavascriptGenerator::SetFrame(InterpreterStackFrame* frame, size_t bytes)
    {
        Assert(this->frame == nullptr);
        this->frame = frame;
#if GLOBAL_ENABLE_WRITE_BARRIER
        if (CONFIG_FLAG(ForceSoftwareWriteBarrier))
        {
            this->GetScriptContext()->GetRecycler()->RegisterPendingWriteBarrierBlock(frame, bytes);
        }
#endif
    }

    void JavascriptGenerator::SetFrameSlots(Js::RegSlot slotCount, Field(Var)* frameSlotArray)
    {
        AssertMsg(this->frame->GetFunctionBody()->GetLocalsCount() == slotCount, "Unexpected mismatch in frame slot count for generated.");
        for (Js::RegSlot i = 0; i < slotCount; i++)
        {
            this->GetFrame()->m_localSlots[i] = frameSlotArray[i];
        }
    }

#if GLOBAL_ENABLE_WRITE_BARRIER
    void JavascriptGenerator::Finalize(bool isShutdown)
    {
        if (CONFIG_FLAG(ForceSoftwareWriteBarrier) && !isShutdown)
        {
            if (this->frame)
            {
                this->GetScriptContext()->GetRecycler()->UnRegisterPendingWriteBarrierBlock(this->frame);
            }
            if (this->args.Values)
            {
                this->GetScriptContext()->GetRecycler()->UnRegisterPendingWriteBarrierBlock(this->args.Values);
            }
        }
    }
#endif

    Var JavascriptGenerator::CallGenerator(ResumeYieldData* yieldData, const char16* apiNameForErrorMessage)
    {
        ScriptContext* scriptContext = this->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();
        Var result = nullptr;

        if (this->IsExecuting())
        {
            JavascriptError::ThrowTypeError(scriptContext, JSERR_GeneratorAlreadyExecuting, apiNameForErrorMessage);
        }

        {
            // RAII helper to set the state of the generator to completed if an exception is thrown
            // or if the save state InterpreterStackFrame is never created implying the generator
            // is JITed and returned without ever yielding.
            class GeneratorStateHelper
            {
                JavascriptGenerator* g;
                bool didThrow;
            public:
                GeneratorStateHelper(JavascriptGenerator* g) : g(g), didThrow(true) { g->SetState(GeneratorState::Executing); }
                ~GeneratorStateHelper() { g->SetState(didThrow || g->frame == nullptr ? GeneratorState::Completed : GeneratorState::Suspended); }
                void DidNotThrow() { didThrow = false; }
            } helper(this);

            Var thunkArgs[] = { this, yieldData };
            Arguments arguments(_countof(thunkArgs), thunkArgs);

            JavascriptExceptionObject *exception = nullptr;

            try
            {
                BEGIN_SAFE_REENTRANT_CALL(scriptContext->GetThreadContext())
                {
                    result = JavascriptFunction::CallFunction<1>(this->scriptFunction, this->scriptFunction->GetEntryPoint(), arguments);
                }
                END_SAFE_REENTRANT_CALL
                helper.DidNotThrow();
            }
            catch (const JavascriptException& err)
            {
                exception = err.GetAndClear();
            }

            if (exception != nullptr)
            {
                if (!exception->IsGeneratorReturnException())
                {
                    JavascriptExceptionOperators::DoThrowCheckClone(exception, scriptContext);
                }
                result = exception->GetThrownObject(nullptr);
            }
        }

        if (!this->IsCompleted())
        {
            int nextOffset = this->frame->GetReader()->GetCurrentOffset();
            int endOffset = this->frame->GetFunctionBody()->GetByteCode()->GetLength();

            if (nextOffset != endOffset - 1)
            {
                // Yielded values are already wrapped in an IteratorResult object, so we don't need to wrap them.
                return result;
            }
        }

        result = library->CreateIteratorResultObject(result, library->GetTrue());
        this->SetState(GeneratorState::Completed);

        return result;
    }

    Var JavascriptGenerator::EntryNext(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Generator.prototype.next"));

        if (!VarIs<JavascriptGenerator>(args[0]))
        {
            JavascriptError::ThrowTypeErrorVar(scriptContext, JSERR_NeedObjectOfType, _u("Generator.prototype.next"), _u("Generator"));
        }

        JavascriptGenerator* generator = UnsafeVarTo<JavascriptGenerator>(args[0]);
        if (generator->GetIsAsync())
        {
            JavascriptError::ThrowTypeErrorVar(scriptContext, JSERR_NeedObjectOfType, _u("Generator.prototype.next"), _u("Generator"));
        }

        Var input = args.Info.Count > 1 ? args[1] : library->GetUndefined();

        if (generator->IsCompleted())
        {
            return library->CreateIteratorResultObjectUndefinedTrue();
        }

        ResumeYieldData yieldData(input, nullptr);
        return generator->CallGenerator(&yieldData, _u("Generator.prototype.next"));
    }

    Var JavascriptGenerator::EntryReturn(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Generator.prototype.return"));

        if (!VarIs<JavascriptGenerator>(args[0]))
        {
            JavascriptError::ThrowTypeErrorVar(scriptContext, JSERR_NeedObjectOfType, _u("Generator.prototype.return"), _u("Generator"));
        }

        JavascriptGenerator* generator = UnsafeVarTo<JavascriptGenerator>(args[0]);
        if (generator->GetIsAsync())
        {
            JavascriptError::ThrowTypeErrorVar(scriptContext, JSERR_NeedObjectOfType, _u("Generator.prototype.return"), _u("Generator"));
        }

        Var input = args.Info.Count > 1 ? args[1] : library->GetUndefined();

        if (generator->IsSuspendedStart())
        {
            generator->SetState(GeneratorState::Completed);
        }

        if (generator->IsCompleted())
        {
            return library->CreateIteratorResultObject(input, library->GetTrue());
        }

        ResumeYieldData yieldData(input, RecyclerNew(scriptContext->GetRecycler(), GeneratorReturnExceptionObject, input, scriptContext));
        return generator->CallGenerator(&yieldData, _u("Generator.prototype.return"));
    }

    Var JavascriptGenerator::EntryThrow(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("Generator.prototype.throw"));

        if (!VarIs<JavascriptGenerator>(args[0]))
        {
            JavascriptError::ThrowTypeErrorVar(scriptContext, JSERR_NeedObjectOfType, _u("Generator.prototype.throw"), _u("Generator"));
        }

        JavascriptGenerator* generator = UnsafeVarTo<JavascriptGenerator>(args[0]);
        if (generator->GetIsAsync())
        {
            JavascriptError::ThrowTypeErrorVar(scriptContext, JSERR_NeedObjectOfType, _u("Generator.prototype.throw"), _u("Generator"));
        }

        Var input = args.Info.Count > 1 ? args[1] : library->GetUndefined();

        if (generator->IsSuspendedStart())
        {
            generator->SetState(GeneratorState::Completed);
        }

        if (generator->IsCompleted())
        {
            JavascriptExceptionOperators::OP_Throw(input, scriptContext);
        }

        ResumeYieldData yieldData(input, RecyclerNew(scriptContext->GetRecycler(), JavascriptExceptionObject, input, scriptContext, nullptr));
        return generator->CallGenerator(&yieldData, _u("Generator.prototype.throw"));
    }

    Var JavascriptGenerator::EntryAsyncNext(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("AsyncGenerator.prototype.next"));

        Var input = args.Info.Count > 1 ? args[1] : library->GetUndefined();

        return JavascriptGenerator::AsyncGeneratorEnqueue(args[0], scriptContext, input, nullptr, _u("AsyncGenerator.prototype.next"));
    }

    Var JavascriptGenerator::EntryAsyncReturn(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("AsyncGenerator.prototype.return"));

        Var input = args.Info.Count > 1 ? args[1] : library->GetUndefined();

        return JavascriptGenerator::AsyncGeneratorEnqueue(args[0], scriptContext, input,
            RecyclerNew(scriptContext->GetRecycler(), GeneratorReturnExceptionObject, input, scriptContext), _u("AsyncGenerator.prototype.return"));
    }

    Var JavascriptGenerator::EntryAsyncThrow(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);

        ARGUMENTS(args, callInfo);
        ScriptContext* scriptContext = function->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        AUTO_TAG_NATIVE_LIBRARY_ENTRY(function, callInfo, _u("AsyncGenerator.prototype.throw"));

        Var input = args.Info.Count > 1 ? args[1] : library->GetUndefined();

        return JavascriptGenerator::AsyncGeneratorEnqueue(args[0], scriptContext, input,
            RecyclerNew(scriptContext->GetRecycler(), JavascriptExceptionObject, input, scriptContext, nullptr), _u("AsyncGenerator.prototype.throw"));
    }

    void JavascriptGenerator::ProcessAsyncGeneratorAwait(Var value)
    {
        ScriptContext* scriptContext = this->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        //#await
        // 1. Let asyncContext be the running execution context.
        // 2. Let promiseCapability be ! NewPromiseCapability(%Promise%).
        // 3. Perform ! Call(promiseCapability.[[Resolve]], undefined, << promise >>).
        JavascriptPromise* promise = UnsafeVarTo<JavascriptPromise>(JavascriptPromise::CreateResolvedPromise(value, scriptContext));
        // 4. Let stepsFulfilled be the algorithm steps defined in Await Fulfilled Functions.
        // 5. Let onFulfilled be CreateBuiltinFunction(stepsFulfilled, << [[AsyncContext]] >>).
        RecyclableObject* onFulfilled = library->CreateAsyncGeneratorAwaitFunction(this, false);
        // 6. Set onFulfilled.[[AsyncContext]] to asyncContext.
        // 7. Let stepsRejected be the algorithm steps defined in Await Rejected Functions.
        // 8. Let onRejected be CreateBuiltinFunction(stepsRejected, << [[AsyncContext]] >>).
        RecyclableObject* onRejected = library->CreateAsyncGeneratorAwaitFunction(this, true);
        // 9. Set onRejected.[[AsyncContext]] to asyncContext.
        // 10. Perform ! PerformPromiseThen(promiseCapability.[[Promise]], onFulfilled, onRejected).
        JavascriptPromise::CreateThenPromise(promise, onFulfilled, onRejected, scriptContext);
        // 11. Remove asyncContext from the execution context stack and restore the execution context that is at the top of the execution context stack as the running execution context.
        // 12. Set the code evaluation state of asyncContext such that when evaluation is resumed with a Completion completion, the following steps of the algorithm that invoked Await will be performed, with completion available.
    }

    void JavascriptGenerator::ProcessAsyncGeneratorYield(Var value, bool isYieldStar)
    {
        ScriptContext* scriptContext = this->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        //#await
        // 1. Let asyncContext be the running execution context.
        // 2. Let promiseCapability be ! NewPromiseCapability(%Promise%).
        // 3. Perform ! Call(promiseCapability.[[Resolve]], undefined, << promise >>).
        JavascriptPromise* promise = UnsafeVarTo<JavascriptPromise>(JavascriptPromise::CreateResolvedPromise(value, scriptContext));
        // 4. Let stepsFulfilled be the algorithm steps defined in Await Fulfilled Functions.
        // 5. Let onFulfilled be CreateBuiltinFunction(stepsFulfilled, << [[AsyncContext]] >>).
        RecyclableObject* continuation = library->CreateAsyncGeneratorAwaitYieldFunction(this, isYieldStar);
        // 6. Set onFulfilled.[[AsyncContext]] to asyncContext.
        // 7. Let stepsRejected be the algorithm steps defined in Await Rejected Functions.
        // 8. Let onRejected be CreateBuiltinFunction(stepsRejected, << [[AsyncContext]] >>).
        // 9. Set onRejected.[[AsyncContext]] to asyncContext.
        // 10. Perform ! PerformPromiseThen(promiseCapability.[[Promise]], onFulfilled, onRejected).
        JavascriptPromise::CreateThenPromise(promise, continuation, continuation, scriptContext);
        // 11. Remove asyncContext from the execution context stack and restore the execution context that is at the top of the execution context stack as the running execution context.
        // 12. Set the code evaluation state of asyncContext such that when evaluation is resumed with a Completion completion, the following steps of the algorithm that invoked Await will be performed, with completion available.
    }


    Var JavascriptGenerator::EntryAsyncGeneratorAwaitReject(RecyclableObject* function, CallInfo callInfo, ...)
    {
        ScriptContext* scriptContext = function->GetScriptContext();
        PROBE_STACK(scriptContext, Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));
        AssertOrFailFastMsg(args.Info.Count > 1, "Should never call EntryAsyncGeneratorAwait without a parameter");

        AsyncGeneratorNextProcessor* resumeNextReturnProcessor = VarTo<AsyncGeneratorNextProcessor>(function);

        ResumeYieldData yieldData(args[1], RecyclerNew(scriptContext->GetRecycler(), JavascriptExceptionObject, args[1], scriptContext, nullptr));
        resumeNextReturnProcessor->GetGenerator()->CallAsyncGenerator(&yieldData);
        return scriptContext->GetLibrary()->GetUndefined();
    }

    Var JavascriptGenerator::EntryAsyncGeneratorAwaitRevolve(RecyclableObject* function, CallInfo callInfo, ...)
    {
        ScriptContext* scriptContext = function->GetScriptContext();
        PROBE_STACK(scriptContext, Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));
        AssertOrFailFastMsg(args.Info.Count > 1, "Should never call EntryAsyncGeneratorAwait without a parameter");

        AsyncGeneratorNextProcessor* resumeNextReturnProcessor = VarTo<AsyncGeneratorNextProcessor>(function);

        ResumeYieldData yieldData(args.Values[1], nullptr);
        resumeNextReturnProcessor->GetGenerator()->CallAsyncGenerator(&yieldData);
        return scriptContext->GetLibrary()->GetUndefined();
    }

    void JavascriptGenerator::CallAsyncGenerator(ResumeYieldData* yieldData)
    {
        ScriptContext* scriptContext = this->GetScriptContext();
        Var result = nullptr;
        JavascriptExceptionObject *exception = nullptr;
        AssertMsg(isAsync, "Should not call CallAsyncGenerator on a non-async generator");
        {
            // RAII helper to set the state of the generator to completed if an exception is thrown
            // or if the save state InterpreterStackFrame is never created implying the generator
            // is JITed and returned without ever yielding.
            class GeneratorStateHelper
            {
                JavascriptGenerator* g;
                bool didThrow;
            public:
                GeneratorStateHelper(JavascriptGenerator* g) : g(g), didThrow(true) { g->SetState(GeneratorState::Executing); }
                ~GeneratorStateHelper() { if (didThrow || g->frame == nullptr) {g->SetState(GeneratorState::Completed); } }
                void DidNotThrow() { didThrow = false; }
            } helper(this);

            Var thunkArgs[] = { this, yieldData };
            Arguments arguments(_countof(thunkArgs), thunkArgs);
            try
            {
                BEGIN_SAFE_REENTRANT_CALL(scriptContext->GetThreadContext())
                {
                    result = JavascriptFunction::CallFunction<1>(this->scriptFunction, this->scriptFunction->GetEntryPoint(), arguments);
                }
                END_SAFE_REENTRANT_CALL
                helper.DidNotThrow();
            }
            catch (const JavascriptException& err)
            {
                exception = err.GetAndClear();
            }
        }

        if (exception != nullptr)
        {
            result = exception->GetThrownObject(nullptr);
            if (!exception->IsGeneratorReturnException())
            {
                AsyncGeneratorReject(result);
                return;
            }
        }

        if (!this->IsCompleted())
        {
            int nextOffset = this->frame->GetReader()->GetCurrentOffset();
            int endOffset = this->frame->GetFunctionBody()->GetByteCode()->GetLength();

            if (nextOffset != endOffset - 1)
            {
                // specifically check for yieldStar
                if (yieldData->isYieldStar)
                {
                    ProcessAsyncGeneratorYield(result, true);
                    return;
                }

                RecyclableObject* resultObject = VarTo<RecyclableObject>(result);
                Var value = JavascriptOperators::GetProperty(resultObject, PropertyIds::value, scriptContext);
                // await is signified by omitting the done property from the object
                if (JavascriptOperators::HasProperty(resultObject, PropertyIds::done))
                {
                    ProcessAsyncGeneratorYield(value, false);
                    return;
                }
                ProcessAsyncGeneratorAwait(value);
                return;
            }
        }

        this->SetState(GeneratorState::Completed);
        AsyncGeneratorResolve(result, true);
    }

    Var JavascriptGenerator::EntryAsyncGeneratorAwaitYield(RecyclableObject* function, CallInfo callInfo, ...)
    {
        ScriptContext* scriptContext = function->GetScriptContext();
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));
        AssertOrFailFastMsg(args.Info.Count > 1, "Should never call EntryAsyncGeneratorAwaitYield without a parameter");
        AsyncGeneratorNextProcessor* resumeNextReturnProcessor = VarTo<AsyncGeneratorNextProcessor>(function);
        JavascriptGenerator* generator = resumeNextReturnProcessor->GetGenerator();
        generator->SetState(GeneratorState::Suspended);

        generator->AsyncGeneratorResolve(args[1], false);
        return scriptContext->GetLibrary()->GetUndefined();
    }

    Var JavascriptGenerator::EntryAsyncGeneratorAwaitYieldStar(RecyclableObject* function, CallInfo callInfo, ...)
    {
        ScriptContext* scriptContext = function->GetScriptContext();
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));
        AssertOrFailFastMsg(args.Info.Count > 1, "Should never call EntryAsyncGeneratorAwaitYield without a parameter");
        AsyncGeneratorNextProcessor* resumeNextReturnProcessor = VarTo<AsyncGeneratorNextProcessor>(function);
        JavascriptGenerator* generator = resumeNextReturnProcessor->GetGenerator();
        generator->SetState(GeneratorState::Suspended);

        if (VarIs<RecyclableObject>(args[1]))
        {
            RecyclableObject* yieldData = UnsafeVarTo<RecyclableObject>(args[1]);
            Var value = JavascriptOperators::GetProperty(yieldData, PropertyIds::value, scriptContext);
            generator->ProcessAsyncGeneratorYield(value, false);
            return scriptContext->GetLibrary()->GetUndefined();
        }
        Var error = generator->CreateTypeError(JSERR_NonObjectFromIterable, scriptContext, _u("yield*"));
        generator->ProcessAsyncGeneratorYield(error, false);
        return scriptContext->GetLibrary()->GetUndefined();
    }

    Var JavascriptGenerator::AsyncGeneratorEnqueue(Var thisValue, ScriptContext* scriptContext, Var input, JavascriptExceptionObject* exceptionObj, const char16* apiNameForErrorMessage)
    {
        JavascriptPromise* promise = JavascriptPromise::CreateEnginePromise(scriptContext);

        if (!VarIs<JavascriptGenerator>(thisValue))
        {
            Var error = CreateTypeError(JSERR_NeedObjectOfType, scriptContext, apiNameForErrorMessage, _u("AsyncGenerator"));
            promise->Reject(error, scriptContext);
            return promise;
        }

        JavascriptGenerator* generator = UnsafeVarTo<JavascriptGenerator>(thisValue);

        if (!generator->GetIsAsync())
        {
            Var error = CreateTypeError(JSERR_NeedObjectOfType, scriptContext, apiNameForErrorMessage, _u("AsyncGenerator"));
            promise->Reject(error, scriptContext);
            return promise;
        }

        AsyncGeneratorRequest* request = RecyclerNew(scriptContext->GetRecycler(), AsyncGeneratorRequest, input, exceptionObj, promise);
        generator->EnqueueRequest(request);

        if (!generator->IsExecuting())
        {
            generator->AsyncGeneratorResumeNext();
        }

        return request->promise;
    }

    // #sec-asyncgeneratorresumenext
    void JavascriptGenerator::AsyncGeneratorResumeNext()
    {
        // 1. Assert: generator is an AsyncGenerator instance.
        // 2. Let state be generator.[[AsyncGeneratorState]].
        // 3. Assert: state is not "executing".
        // 4. If state is "awaiting-return", return undefined.
        // 5. Let queue be generator.[[AsyncGeneratorQueue]].
        // 6. If queue is an empty List, return undefined.
        AssertMsg(isAsync, "Should not call AsyncGeneratorResumeNext on non-async generator");
        if (IsAwaitingReturn() || !HasRequests())
        {
            return;
        }

        ScriptContext* scriptContext = this->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();

        // 7. Let next be the value of the first element of queue.
        AsyncGeneratorRequest* next = GetRequest(false);
        // 8. Assert: next is an AsyncGeneratorRequest record.
        // 9. Let completion be next.[[Completion]].
        // 10. If completion is an abrupt completion, then
        if (next->exceptionObj != nullptr)
        {
            // a. If state is "suspendedStart", then
            //    i. Set generator.[[AsyncGeneratorState]] to "completed".
            //    ii. Set state to "completed".
            if (IsSuspendedStart())
            {
                SetState(GeneratorState::Completed);
            }
            // b. If state is "completed", then
            if (IsCompleted())
            {
                // i. If completion.[[Type]] is return, then
                if (next->exceptionObj->IsGeneratorReturnException())
                {
                    // 1. Set generator.[[AsyncGeneratorState]] to "awaiting-return".
                    SetState(GeneratorState::AwaitingReturn);
                    // 2. Let promiseCapability be ! NewPromiseCapability(%Promise%).
                    // 3. Perform ! Call(promiseCapability.[[Resolve]], undefined, << completion.[[Value]] >>).
                    JavascriptPromise* promise = UnsafeVarTo<JavascriptPromise>(JavascriptPromise::CreateResolvedPromise(next->data, this->GetScriptContext()));
                    // 4. Let stepsFulfilled be the algorithm steps defined in AsyncGeneratorResumeNext Return  Processor Fulfilled Functions.
                    // 5. Let onFulfilled be CreateBuiltinFunction(stepsFulfilled, << [[Generator]] >>).
                    // 6. Set onFulfilled.[[Generator]] to generator.
                    RecyclableObject* onFulfilled = library->CreateAsyncGeneratorResumeNextReturnProcessorFunction(this, false);
                    // 7. Let stepsRejected be the algorithm steps defined in AsyncGeneratorResumeNext Return Processor Rejected Functions.
                    // 8. Let onRejected be CreateBuiltinFunction(stepsRejected, << [[Generator]] >>).
                    // 9. Set onRejected.[[Generator]] to generator.
                    RecyclableObject* onRejected = library->CreateAsyncGeneratorResumeNextReturnProcessorFunction(this, true);
                    // 10. Perform ! PerformPromiseThen(promiseCapability.[[Promise]], onFulfilled, onRejected).
                    JavascriptPromise::CreateThenPromise(promise, onFulfilled, onRejected, scriptContext);
                    // 11. Return undefined.
                    return;
                }
                // ii. Else,
                else
                {
                    // 1. Assert: completion.[[Type]] is throw.
                    // 2. Perform ! AsyncGeneratorReject(generator, completion.[[Value]]).
                    AsyncGeneratorReject(next->data);
                    // 3. Return undefined.
                    return;
                }
            }

        }
        // 11. Else if state is "completed", return ! AsyncGeneratorResolve(generator, undefined, true).
        else if (IsCompleted())
        {
            AsyncGeneratorResolve(library->GetUndefined(), true);
            return;
        }

        // 12. Assert: state is either "suspendedStart" or "suspendedYield".    
        // 13. Let genContext be generator.[[AsyncGeneratorContext]].
        // 14. Let callerContext be the running execution context.
        // 15. Suspend callerContext.
        // 16. Set generator.[[AsyncGeneratorState]] to "executing".
        SetState(GeneratorState::Executing);
        // 17. Push genContext onto the execution context stack; genContext is now the running execution context.
        // 18. Resume the suspended evaluation of genContext using completion as the result of the operation that suspended it. Let result be the completion record returned by the resumed computation.
        ResumeYieldData data(next->data, next->exceptionObj);
        CallAsyncGenerator(&data);
        // 19. Assert: result is never an abrupt completion.
        // 20. Assert: When we return here, genContext has already been removed from the execution context stack and callerContext is the currently running execution context.
        // 21. Return undefined.
    }

    AsyncGeneratorQueue* JavascriptGenerator::EnsureAsyncGeneratorQueue()
    {
        AssertMsg(isAsync, "Should not add Async Generator Queue to non-async generator");
        if (asyncGeneratorQueue != nullptr)
        {
            return asyncGeneratorQueue;
        }

        Recycler* recycler = this->GetScriptContext()->GetRecycler();
        asyncGeneratorQueue = RecyclerNew(recycler, AsyncGeneratorQueue, recycler);
        return asyncGeneratorQueue;
    }


    void JavascriptGenerator::InitialiseAsyncGenerator(ScriptContext* scriptContext)
    {
        AssertMsg(isAsync, "Should not call InitialiseAsyncGenerator on a non-async generator");
        ResumeYieldData data(scriptContext->GetLibrary()->GetUndefined(), nullptr);
        Var thunkArgs[] = { this, &data };
        Arguments arguments(_countof(thunkArgs), thunkArgs);
        BEGIN_SAFE_REENTRANT_CALL(scriptContext->GetThreadContext())
        {
            JavascriptFunction::CallFunction<1>(this->scriptFunction, this->scriptFunction->GetEntryPoint(), arguments);
        }
        END_SAFE_REENTRANT_CALL
        SetState(JavascriptGenerator::GeneratorState::SuspendedStart);
    }

    void JavascriptGenerator::AsyncGeneratorResolve(Var value, bool done)
    {
        AssertMsg(isAsync, "Should not call AsyncGeneratorResolve on a non-async generator");
        AsyncGeneratorRequest* next = GetRequest(true);
        AssertMsg(next != nullptr, "Should never call AsyncGeneratorResolve with an empty queue");

        ScriptContext* scriptContext = this->GetScriptContext();
        JavascriptLibrary* library = scriptContext->GetLibrary();
        Var result = library->CreateIteratorResultObject(value, done ? library->GetTrue() : library->GetFalse());

        next->promise->Resolve(result, scriptContext);
        AsyncGeneratorResumeNext();
    }

    void JavascriptGenerator::AsyncGeneratorReject(Var reason)
    {
        AssertMsg(isAsync, "Should not call AsyncGeneratorReject on a non-async generator");
        AsyncGeneratorRequest* next = GetRequest(true);
        AssertMsg(next != nullptr, "Should never call AsyncGeneratorReject with an empty queue");

        ScriptContext* scriptContext = this->GetScriptContext();

        next->promise->Reject(reason, scriptContext);
        AsyncGeneratorResumeNext();
    }

    Var JavascriptGenerator::CreateTypeError(HRESULT hr, ScriptContext* scriptContext, ...)
    {
        JavascriptLibrary* library = scriptContext->GetLibrary();
        JavascriptError* typeError = library->CreateTypeError();
        va_list params;
        va_start(params, scriptContext);
        JavascriptError::SetErrorMessage(typeError, hr, scriptContext, params);
        va_end(params);
        return typeError;
    }

    Var JavascriptGenerator::EntryAsyncGeneratorResumeNextReturnProcessorResolve(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));
        AssertOrFailFastMsg(args.Info.Count > 1, "Should never call EntryAsyncGeneratorResumeNextReturnProcessor without a parameter");
        AsyncGeneratorNextProcessor* resumeNextReturnProcessor = VarTo<AsyncGeneratorNextProcessor>(function);

        resumeNextReturnProcessor->GetGenerator()->SetState(GeneratorState::Completed);
        resumeNextReturnProcessor->GetGenerator()->AsyncGeneratorResolve(args[1], true);
        return  function->GetScriptContext()->GetLibrary()->GetUndefined();
    }

    Var JavascriptGenerator::EntryAsyncGeneratorResumeNextReturnProcessorReject(RecyclableObject* function, CallInfo callInfo, ...)
    {
        PROBE_STACK(function->GetScriptContext(), Js::Constants::MinStackDefault);
        ARGUMENTS(args, callInfo);
        Assert(!(callInfo.Flags & CallFlags_New));
        AssertOrFailFastMsg(args.Info.Count > 1, "Should never call EntryAsyncGeneratorResumeNextReturnProcessor without a parameter");
        AsyncGeneratorNextProcessor* resumeNextReturnProcessor = VarTo<AsyncGeneratorNextProcessor>(function);

        resumeNextReturnProcessor->GetGenerator()->AsyncGeneratorReject(args[1]);
        return  function->GetScriptContext()->GetLibrary()->GetUndefined();
    }

    template <> bool VarIsImpl<AsyncGeneratorNextProcessor>(RecyclableObject* obj)
    {
        if (VarIs<JavascriptFunction>(obj))
        {
            return VirtualTableInfo<AsyncGeneratorNextProcessor>::HasVirtualTable(obj)
                || VirtualTableInfo<CrossSiteObject<AsyncGeneratorNextProcessor>>::HasVirtualTable(obj);
        }

        return false;
    }

#if ENABLE_TTD

    void JavascriptGenerator::MarkVisitKindSpecificPtrs(TTD::SnapshotExtractor* extractor)
    {
        if (this->scriptFunction != nullptr)
        {
            extractor->MarkVisitVar(this->scriptFunction);
        }

        // frame is null when generator has been completed
        if (this->frame != nullptr)
        {
            // mark slot variables for traversal
            Js::RegSlot slotCount = this->frame->GetFunctionBody()->GetLocalsCount();
            for (Js::RegSlot i = 0; i < slotCount; i++)
            {
                Js::Var curr = this->frame->m_localSlots[i];
                if (curr != nullptr)
                {
                    extractor->MarkVisitVar(curr);
                }
            }
        }

        // args.Values is null when generator has been completed
        if (this->args.Values != nullptr)
        {
            // mark argument variables for traversal
            uint32 argCount = this->args.GetArgCountWithExtraArgs();
            for (uint32 i = 0; i < argCount; i++)
            {
                Js::Var curr = this->args[i];
                if (curr != nullptr)
                {
                    extractor->MarkVisitVar(curr);
                }
            }
        }
    }

    TTD::NSSnapObjects::SnapObjectType JavascriptGenerator::GetSnapTag_TTD() const
    {
        return TTD::NSSnapObjects::SnapObjectType::SnapGenerator;
    }

    void JavascriptGenerator::ExtractSnapObjectDataInto(TTD::NSSnapObjects::SnapObject* objData, TTD::SlabAllocator& alloc)
    {
        TTD::NSSnapObjects::SnapGeneratorInfo* gi = alloc.SlabAllocateStruct<TTD::NSSnapObjects::SnapGeneratorInfo>();

        // TODO: BUGBUG - figure out how to determine what the prototype was
        gi->generatorPrototype = 0;
        //if (this->GetPrototype() == this->GetScriptContext()->GetLibrary()->GetNull())
        //{
        //    gi->generatorPrototype = 1;
        //}
        //else if (this->GetType() == this->GetScriptContext()->GetLibrary()->GetGeneratorConstructorPrototypeObjectType())
        //{
        //    // check type here, not prototype, since type is static across generators
        //    gi->generatorPrototype = 2;
        //}
        //else
        //{
        //    //TTDAssert(false, "unexpected prototype found JavascriptGenerator");
        //}

        gi->scriptFunction = TTD_CONVERT_VAR_TO_PTR_ID(this->scriptFunction);
        gi->state = static_cast<uint32>(this->state);


        // grab slot info from InterpreterStackFrame
        gi->frame_slotCount = 0;
        gi->frame_slotArray = nullptr;
        if (this->frame != nullptr)
        {
            gi->frame_slotCount = this->frame->GetFunctionBody()->GetLocalsCount();
            if (gi->frame_slotCount > 0)
            {
                gi->frame_slotArray = alloc.SlabAllocateArray<TTD::TTDVar>(gi->frame_slotCount);
            }
            for (Js::RegSlot i = 0; i < gi->frame_slotCount; i++)
            {
                gi->frame_slotArray[i] = this->frame->m_localSlots[i];
            }
        }

        // grab arguments
        TTD_PTR_ID* depArray = nullptr;
        uint32 depCount = 0;

        if (this->args.Values == nullptr)
        {
            gi->arguments_count = 0;
        }
        else
        {
            gi->arguments_count = this->args.GetArgCountWithExtraArgs();
        }

        gi->arguments_values = nullptr;
        if (gi->arguments_count > 0)
        {
            gi->arguments_values = alloc.SlabAllocateArray<TTD::TTDVar>(gi->arguments_count);
            depArray = alloc.SlabReserveArraySpace<TTD_PTR_ID>(gi->arguments_count);
        }

        for (uint32 i = 0; i < gi->arguments_count; i++)
        {
            gi->arguments_values[i] = this->args[i];
            if (gi->arguments_values[i] != nullptr && TTD::JsSupport::IsVarComplexKind(gi->arguments_values[i]))
            {
                depArray[depCount] = TTD_CONVERT_VAR_TO_PTR_ID(gi->arguments_values[i]);
                depCount++;
            }
        }

        if (depCount > 0)
        {
            alloc.SlabCommitArraySpace<TTD_PTR_ID>(depCount, gi->arguments_count);
        }
        else if (gi->arguments_count > 0)
        {
            alloc.SlabAbortArraySpace<TTD_PTR_ID>(gi->arguments_count);
        }

        if (this->frame != nullptr)
        {
            gi->byteCodeReader_offset = this->frame->GetReader()->GetCurrentOffset();
        }
        else
        {
            gi->byteCodeReader_offset = 0;
        }

        // Copy the CallInfo data into the struct
        gi->arguments_callInfo_count = this->args.Info.Count;
        gi->arguments_callInfo_flags = this->args.Info.Flags;

        // TODO:  understand why there's a mis-match between args.Info.Count and GetArgCountWithExtraArgs
        // TTDAssert(this->args.Info.Count == gi->arguments_count, "mismatched count between args.Info and GetArgCountWithExtraArgs");

        if (depCount == 0)
        {
            TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapGeneratorInfo*, TTD::NSSnapObjects::SnapObjectType::SnapGenerator>(objData, gi);
        }
        else
        {
            TTDAssert(depArray != nullptr, "depArray should be non-null if depCount is > 0");
            TTD::NSSnapObjects::StdExtractSetKindSpecificInfo<TTD::NSSnapObjects::SnapGeneratorInfo*, TTD::NSSnapObjects::SnapObjectType::SnapGenerator>(objData, gi, alloc, depCount, depArray);
        }

    }
#endif
}
