//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft Corporation and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------
#pragma once
#include <list>

class WScriptJsrt
{
public:
    static bool Initialize();
    static bool Uninitialize();

    class CallbackMessage : public MessageBase
    {
        JsValueRef m_function;

        CallbackMessage(CallbackMessage const&);

    public:
        CallbackMessage(unsigned int time, JsValueRef function);
        ~CallbackMessage();

        HRESULT Call(LPCSTR fileName);
        HRESULT CallFunction(LPCSTR fileName);
        template <class Func>
        static CallbackMessage* Create(JsValueRef function, const Func& func, unsigned int time = 0)
        {
            return new CustomMessage<Func, CallbackMessage>(time, function, func);
        }
    };

    class ModuleMessage : public MessageBase
    {
    private:
        JsModuleRecord moduleRecord;
        JsValueRef specifier;

        ModuleMessage(JsModuleRecord module, JsValueRef specifier);

    public:
        ~ModuleMessage();

        virtual HRESULT Call(LPCSTR fileName) override;

        static ModuleMessage* Create(JsModuleRecord module, JsValueRef specifier)
        {
            return new ModuleMessage(module, specifier);
        }

    };

    class ManagedModule
    {
    private:
        std::string normalizedSpecifier;
        std::string specifier;
        uint numChildren = 0;
        ManagedModule* childModules[50];
        std::map<std::string, ManagedModule*> parentMap;
        bool beingChecked = false;
        bool isDynamicOrRoot;
        bool hasParents = false;
        uint state = 0;//0 = needs to be parsed, 1 = parsed, 2 = ready

        ManagedModule(JsModuleRecord module, std::string specifier, std::string normalizedSpecifier, bool isDynamicOrRoot);

    public:
        ~ManagedModule();
        JsModuleRecord moduleRecord;
        JsErrorCode addChild(JsValueRef specifier);
        JsErrorCode Update();
        JsErrorCode childComplete();
        bool CheckChildren();
        bool isReady() { return state == 2; };
        void SetHasParents() { hasParents = true; };
        bool GetHasParents() { return hasParents; };
        uint GetNumChildren() { return numChildren; };
        void SetIsDynamicOrRoot() { isDynamicOrRoot = true; };
        void SetEntryPoint() { state = 1; };
        
        static ManagedModule* Create(JsModuleRecord module, std::string specifier, std::string normalizedSpecifier, bool isDynamicOrRoot = false)
        {
            return new ManagedModule(module, specifier, normalizedSpecifier, isDynamicOrRoot);
        }

        static ManagedModule* findModule(JsModuleRecord parent, JsValueRef specifier);
    };

    class ManagedModuleMessage : public MessageBase
    {
    private:
        ManagedModule* module;
        ManagedModuleMessage(ManagedModule* module);

    public:
        virtual HRESULT Call(LPCSTR fileName) override;
        
        static ManagedModuleMessage* Create(ManagedModule* module)
        {
            return new ManagedModuleMessage(module);
        }
    };

    static void AddMessageQueue(MessageQueue *messageQueue);
    static void PushMessage(MessageBase *message) { messageQueue->InsertSorted(message); }

    static JsErrorCode FetchImportedModule(_In_ JsModuleRecord referencingModule, _In_ JsValueRef specifier, _Outptr_result_maybenull_ JsModuleRecord* dependentModuleRecord);
    static JsErrorCode FetchImportedModuleFromScript(_In_ DWORD_PTR dwReferencingSourceContext, _In_ JsValueRef specifier, _Outptr_result_maybenull_ JsModuleRecord* dependentModuleRecord);
    static JsErrorCode NotifyModuleReadyCallback(_In_opt_ JsModuleRecord referencingModule, _In_opt_ JsValueRef exceptionVar);
    static JsErrorCode ProvideModuleCallback(_In_ JsModuleRecord referencingModule, _In_ JsValueRef specifier, _Outptr_result_maybenull_ JsModuleRecord* dependentModuleRecord);
    static JsErrorCode InitializeModuleCallbacks();
    static void CALLBACK PromiseContinuationCallback(JsValueRef task, void *callbackState);
    static void CALLBACK PromiseRejectionTrackerCallback(JsValueRef promise, JsValueRef reason, bool handled, void *callbackState);

    static LPCWSTR ConvertErrorCodeToMessage(JsErrorCode errorCode)
    {
        switch (errorCode)
        {
        case (JsErrorCode::JsErrorInvalidArgument) :
            return _u("TypeError: InvalidArgument");
        case (JsErrorCode::JsErrorNullArgument) :
            return _u("TypeError: NullArgument");
        case (JsErrorCode::JsErrorArgumentNotObject) :
            return _u("TypeError: ArgumentNotAnObject");
        case (JsErrorCode::JsErrorOutOfMemory) :
            return _u("OutOfMemory");
        case (JsErrorCode::JsErrorScriptException) :
            return _u("ScriptError");
        case (JsErrorCode::JsErrorScriptCompile) :
            return _u("SyntaxError");
        case (JsErrorCode::JsErrorFatal) :
            return _u("FatalError");
        case (JsErrorCode::JsErrorInExceptionState) :
            return _u("ErrorInExceptionState");
        default:
            AssertMsg(false, "Unexpected JsErrorCode");
            return nullptr;
        }
    }

#if ENABLE_TTD
    static void CALLBACK JsContextBeforeCollectCallback(JsRef contextRef, void *data);
#endif

    static bool PrintException(LPCSTR fileName, JsErrorCode jsErrorCode);
    static JsValueRef LoadScript(JsValueRef callee, LPCSTR fileName, LPCSTR fileContent, LPCSTR scriptInjectType, bool isSourceModule, JsFinalizeCallback finalizeCallback, bool isFile);
    static DWORD_PTR GetNextSourceContext();
    static JsValueRef LoadScriptFileHelper(JsValueRef callee, JsValueRef *arguments, unsigned short argumentCount, bool isSourceModule);
    static JsValueRef LoadScriptHelper(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState, bool isSourceModule);
    static bool InstallObjectsOnObject(JsValueRef object, const char* name, JsNativeFunction nativeFunction);
    static void FinalizeFree(void * addr);
    static void RegisterScriptDir(DWORD_PTR sourceContext, LPCSTR fullDirNarrow);
private:
    static bool CreateArgumentsObject(JsValueRef *argsObject);
    static bool CreateNamedFunction(const char*, JsNativeFunction callback, JsValueRef* functionVar);
    static void GetDir(LPCSTR fullPathNarrow, std::string *fullDirNarrow);
    static JsValueRef CALLBACK EchoCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK QuitCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK LoadScriptFileCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK LoadScriptCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK LoadModuleCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK GetModuleNamespace(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK SetTimeoutCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK ClearTimeoutCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK AttachCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK DetachCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK DumpFunctionPositionCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK RequestAsyncBreakCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);

    static JsValueRef CALLBACK EmptyCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsErrorCode CALLBACK LoadModuleFromString(LPCSTR fileName, LPCSTR fileContent, LPCSTR fullName = nullptr, bool isFile = false);
    static JsErrorCode CALLBACK InitializeModuleInfo(JsValueRef specifier, JsModuleRecord moduleRecord);

    static JsValueRef CALLBACK LoadBinaryFileCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK LoadTextFileCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK RegisterModuleSourceCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK FlagCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK ReadLineStdinCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    
    static JsValueRef CALLBACK BroadcastCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK ReceiveBroadcastCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK ReportCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK GetReportCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK LeavingCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK SleepCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);
    static JsValueRef CALLBACK GetProxyPropertiesCallback(JsValueRef callee, bool isConstructCall, JsValueRef *arguments, unsigned short argumentCount, void *callbackState);

    static JsErrorCode FetchImportedModuleHelper(JsModuleRecord referencingModule, JsValueRef specifier, __out JsModuleRecord* dependentModuleRecord, LPCSTR refdir = nullptr);

    static MessageQueue *messageQueue;
    static DWORD_PTR sourceContext;
    static std::map<std::string, JsModuleRecord> moduleRecordMap;
    static std::map<std::string, WScriptJsrt::ManagedModule*> managedModuleMap;
    static std::map<JsModuleRecord, std::string> moduleDirMap;
    static std::map<DWORD_PTR, std::string> scriptDirMap;
};
