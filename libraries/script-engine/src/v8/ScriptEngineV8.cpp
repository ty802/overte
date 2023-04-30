//
//  ScriptEngineV8.cpp
//  libraries/script-engine/src/v8
//
//  Created by Brad Hefta-Gaub on 12/14/13.
//  Modified for V8 by dr Karol Suprynowicz on 2022/10/08
//  Copyright 2013 High Fidelity, Inc.
//  Copyright 2022-2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ScriptEngineV8.h"

#include <chrono>
#include <mutex>
#include <thread>

#include <QtCore/QCoreApplication>
#include <QtCore/QEventLoop>
#include <QtCore/QFileInfo>
#include <QtCore/QTimer>
#include <QtCore/QThread>
#include <QtCore/QRegularExpression>

#include <QtCore/QFuture>
#include <QtConcurrent/QtConcurrentRun>

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QMenu>

#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>

#include <shared/LocalFileAccessGate.h>
#include <shared/QtHelpers.h>
#include <shared/AbstractLoggerInterface.h>

#include <Profile.h>

#include <v8-profiler.h>

#include "../ScriptEngineLogging.h"
#include "../ScriptProgram.h"
#include "../ScriptValue.h"

#include "ScriptContextV8Wrapper.h"
#include "ScriptObjectV8Proxy.h"
#include "ScriptProgramV8Wrapper.h"
#include "ScriptValueV8Wrapper.h"
#include "V8Lambda.h"
#include "ScriptEngineLoggingV8.h"

static const int MAX_DEBUG_VALUE_LENGTH { 80 };

std::once_flag ScriptEngineV8::_v8InitOnceFlag;
QMutex ScriptEngineV8::_v8InitMutex;

bool ScriptEngineV8::IS_THREADSAFE_INVOCATION(const QThread* thread, const QString& method) {
    const QThread* currentThread = QThread::currentThread();
    if (currentThread == thread) {
        return true;
    }
    qCCritical(scriptengine_v8) << QString("Scripting::%1 @ %2 -- ignoring thread-unsafe call from %3")
                              .arg(method)
                              .arg(thread ? thread->objectName() : "(!thread)")
                              .arg(QThread::currentThread()->objectName());
    qCDebug(scriptengine_v8) << "(please resolve on the calling side by using invokeMethod, executeOnScriptThread, etc.)";
    Q_ASSERT(false);
    return false;
}

ScriptValue ScriptEngineV8::makeError(const ScriptValue& _other, const QString& type) {
    if (!IS_THREADSAFE_INVOCATION(thread(), __FUNCTION__)) {
        return nullValue();
    }
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    return nullValue();
}

    //V8TODO
    /*
    auto other = _other;
    if (_other.constGet()->IsString()) {
        other = QScriptEngine::newObject();
        other.setProperty("message", _other.toString());
    }
    auto proto = QScriptEngine::globalObject().property(type);
    if (!proto.isFunction()) {
        proto = QScriptEngine::globalObject().property(other.prototype().property("constructor").property("name").toString());
    }
    if (!proto.isFunction()) {
#ifdef DEBUG_JS_EXCEPTIONS
        qCDebug(shared) << "BaseScriptEngine::makeError -- couldn't find constructor for" << type << " -- using Error instead";
#endif
        proto = QScriptEngine::globalObject().property("Error");
    }
    if (other.engine() != this) {
        // JS Objects are parented to a specific script engine instance
        // -- this effectively ~clones it locally by routing through a QVariant and back
        other = QScriptEngine::toScriptValue(other.toVariant());
    }
    // ~ var err = new Error(other.message)
    auto err = proto.construct(V8ScriptValueList({ other.property("message") }));

    // transfer over any existing properties
    V8ScriptValueIterator it(other);
    while (it.hasNext()) {
        it.next();
        err.setProperty(it.name(), it.value());
    }
    return err;*/
//}


// check syntax and when there are issues returns an actual "SyntaxError" with the details
ScriptValue ScriptEngineV8::checkScriptSyntax(ScriptProgramPointer program) {
    if (!IS_THREADSAFE_INVOCATION(thread(), __FUNCTION__)) {
        return nullValue();
    }
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    ScriptSyntaxCheckResultPointer syntaxCheck = program->checkSyntax();
    //V8TODO
    if (syntaxCheck->state() != ScriptSyntaxCheckResult::Valid) {
        auto err = globalObject().property("SyntaxError").construct(ScriptValueList({ newValue(syntaxCheck->errorMessage()) }));
        err.setProperty("fileName", program->fileName());
        err.setProperty("lineNumber", syntaxCheck->errorLineNumber());
        err.setProperty("expressionBeginOffset", syntaxCheck->errorColumnNumber());
        err.setProperty("stack", syntaxCheck->errorBacktrace());
        //err.setProperty("stack", currentContext()->backtrace().join(ScriptManager::SCRIPT_BACKTRACE_SEP));
        {
            const auto error = syntaxCheck->errorMessage();
            const auto line = QString::number(syntaxCheck->errorLineNumber());
            const auto column = QString::number(syntaxCheck->errorColumnNumber());
            // for compatibility with legacy reporting
            const auto message = QString("[SyntaxError] %1 in %2:%3(%4)").arg(error, program->fileName(), line, column);
            err.setProperty("formatted", message);
        }
        return err;
    }
    return undefinedValue();
}
/*ScriptValue ScriptEngineV8::lintScript(const QString& sourceCode, const QString& fileName, const int lineNumber) {
    if (!IS_THREADSAFE_INVOCATION(thread(), __FUNCTION__)) {
        return nullValue();
    }
    //V8TODO
    const auto syntaxCheck = checkSyntax(sourceCode);
    if (syntaxCheck.state() != QScriptSyntaxCheckResult::Valid) {
        auto err = QScriptEngine::globalObject().property("SyntaxError").construct(V8ScriptValueList({ syntaxCheck.errorMessage() }));
        err.setProperty("fileName", fileName);
        err.setProperty("lineNumber", syntaxCheck.errorLineNumber());
        err.setProperty("expressionBeginOffset", syntaxCheck.errorColumnNumber());
        err.setProperty("stack", currentContext()->backtrace().join(ScriptManager::SCRIPT_BACKTRACE_SEP));
        {
            const auto error = syntaxCheck.errorMessage();
            const auto line = QString::number(syntaxCheck.errorLineNumber());
            const auto column = QString::number(syntaxCheck.errorColumnNumber());
            // for compatibility with legacy reporting
            const auto message = QString("[SyntaxError] %1 in %2:%3(%4)").arg(error, fileName, line, column);
            err.setProperty("formatted", message);
        }
        return ScriptValue(new ScriptValueV8Wrapper(this, std::move(err)));
    }
    return undefinedValue();
}*/



// Lambda
/*ScriptValue ScriptEngineV8::newLambdaFunction(std::function<V8ScriptValue(V8ScriptContext*, ScriptEngineV8*)> operation,
                                                 const V8ScriptValue& data,
                                                 const ValueOwnership& ownership) {
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    auto lambda = new Lambda(this, operation, data);
    auto object = newQObject(lambda, ownership);
    //V8TODO - I'm not sure if this works
    auto call = object.property("call");
    call.setPrototype(object);  // context->callee().prototype() === Lambda QObject
    call.setData(ScriptValue(new ScriptValueV8Wrapper(this, data)));         // context->callee().data() will === data param
    return call;
}*/
QString Lambda::toString() const {
    v8::Locker locker(_engine->getIsolate());
    v8::Isolate::Scope isolateScope(_engine->getIsolate());
    v8::HandleScope handleScope(_engine->getIsolate());
    v8::Context::Scope contextScope(_engine->getContext());
    v8::Local<v8::String> string;
    QString qString("");
    if (_data.constGet()->ToString(_engine->getContext()).ToLocal(&string)) {
        v8::String::Utf8Value utf8Value(_engine->getIsolate(), string);
        qString = QString(*utf8Value);
    }
    //V8TODO it was data.isValid() originally
    //I have no idea what happens here
    return QString("[Lambda%1]").arg((!_data.constGet()->IsNullOrUndefined()) ? " " + qString : qString);
}

Lambda::~Lambda() {
#ifdef DEBUG_JS_LAMBDA_FUNCS
    qCDebug(scriptengine_v8) << "~Lambda"
             << "this" << this;
#endif
}

Lambda::Lambda(ScriptEngineV8* engine,
               std::function<V8ScriptValue(ScriptEngineV8*)> operation,
               V8ScriptValue data) :
    _engine(engine),
    _operation(operation), _data(data) {
#ifdef DEBUG_JS_LAMBDA_FUNCS
    qCDebug(scriptengine_v8) << "Lambda" << data.toString();
#endif
}
V8ScriptValue Lambda::call() {
    if (!_engine->IS_THREADSAFE_INVOCATION(__FUNCTION__)) {
        return V8ScriptValue(_engine, v8::Null(_engine->getIsolate()));
    }
    // V8TODO: it needs to be done in entirely different way for V8
    Q_ASSERT(false);
    //return _operation(_engine->getContext(), _engine);
    //return V8ScriptValue(_engine->getIsolate(), v8::Null(_engine->getIsolate()));
    //return operation(static_cast<QScriptEngine*>(engine)->currentContext(), engine);
}

#ifdef DEBUG_JS
void ScriptEngineV8::_debugDump(const QString& header, const V8ScriptValue& object, const QString& footer) {
    // V8TODO
    /*if (!IS_THREADSAFE_INVOCATION(thread(), __FUNCTION__)) {
        return;
    }
    if (!header.isEmpty()) {
        qCDebug(shared) << header;
    }
    if (!object.isObject()) {
        qCDebug(shared) << "(!isObject)" << object.toVariant().toString() << object.toString();
        return;
    }
    V8ScriptValueIterator it(object);
    while (it.hasNext()) {
        it.next();
        qCDebug(shared) << it.name() << ":" << it.value().toString();
    }
    if (!footer.isEmpty()) {
        qCDebug(shared) << footer;
    }*/
}
#endif

v8::Platform* ScriptEngineV8::getV8Platform() {
    static std::unique_ptr<v8::Platform> platform = v8::platform::NewDefaultPlatform();
    return platform.get();
}

ScriptEngineV8::ScriptEngineV8(ScriptManager *manager) : ScriptEngine(manager), _evaluatingCounter(0)
    //V8TODO
    //_arrayBufferClass(new ArrayBufferClass(this))
{
    _v8InitMutex.lock();
    std::call_once ( _v8InitOnceFlag, [ ]{
        v8::V8::InitializeExternalStartupData("");

        // Experimentally determined that the maximum size that works on Linux with a stack size of 8192K is 8182.
        // That would seem to be the overhead of our code and V8.
        //
        // Windows stacks are 1MB.
        //
        // Based on that, going with 256K for stacks for now. That seems like a reasonable value.
        // We'll probably need a more complex system on the longer term, with configurable limits.
        // Flags to try:
        // V8TODO --single-threaded is to check if it fixes random crashes
        // --jitless - might improve debugging performance due to no JIT?
        // --assert-types

        v8::V8::InitializeICU();
#ifdef OVERTE_V8_MEMORY_DEBUG
        v8::V8::SetFlagsFromString("--stack-size=256 --track_gc_object_stats --assert-types");
#else
        v8::V8::SetFlagsFromString("--stack-size=256");
#endif
        //v8::V8::SetFlagsFromString("--stack-size=256 --single-threaded");
        v8::Platform* platform = getV8Platform();
        v8::V8::InitializePlatform(platform);
        v8::V8::Initialize(); qCDebug(scriptengine_v8) << "V8 platform initialized";
    } );
    _v8InitMutex.unlock();
    qCDebug(scriptengine_v8) << "Creating new script engine";
    {
        v8::Isolate::CreateParams isolateParams;
        isolateParams.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();
        _v8Isolate = v8::Isolate::New(isolateParams);
        v8::Locker locker(_v8Isolate);
        v8::Isolate::Scope isolateScope(_v8Isolate);
        v8::HandleScope handleScope(_v8Isolate);
        v8::Local<v8::Context> context = v8::Context::New(_v8Isolate);
        Q_ASSERT(!context.IsEmpty());
        v8::Context::Scope contextScope(context);
        _contexts.append(std::make_shared<ScriptContextV8Wrapper>(this,context, ScriptContextPointer()));

        V8ScriptValue nullScriptValue(this, v8::Null(_v8Isolate));
        _nullValue = ScriptValue(new ScriptValueV8Wrapper(this, nullScriptValue));

        V8ScriptValue undefined(this, v8::Undefined(_v8Isolate));
        _undefinedValue = ScriptValue(new ScriptValueV8Wrapper(this, undefined));

        registerSystemTypes();

        // V8TODO: dispose of isolate on ScriptEngineV8 destruction
        //v8::UniquePersistent<v8::Value> null = v8::UniquePersistent<v8::Value>(_v8Isolate, v8::Null(_v8Isolate));
        //_nullValue = ScriptValue(new ScriptValueV8Wrapper(this, std::move(null)));

        //V8ScriptValue undefined = v8::UniquePersistent<v8::Value>(_v8Isolate,v8::Undefined(_v8Isolate));
        //_undefinedValue = ScriptValue(new ScriptValueV8Wrapper(this, std::move(undefined)));

        // V8TODO:
        //QScriptEngine::setProcessEventsInterval(MSECS_PER_SECOND);
    }

    //_currentThread = QThread::currentThread();

    //if (_scriptManager) {
        // V8TODO: port to V8
        /*connect(this, &QScriptEngine::signalHandlerException, this, [this](const V8ScriptValue& exception) {
            if (hasUncaughtException()) {
                // the engine's uncaughtException() seems to produce much better stack traces here
                emit _scriptManager->unhandledException(cloneUncaughtException("signalHandlerException"));
                clearExceptions();
            } else {
                // ... but may not always be available -- so if needed we fallback to the passed exception
                V8ScriptValue thrown = makeError(exception);
                emit _scriptManager->unhandledException(ScriptValue(new ScriptValueV8Wrapper(this, std::move(thrown))));
            }
        }, Qt::DirectConnection);*/
        //moveToThread(scriptManager->thread());
        //setThread(scriptManager->thread());
    //}
}

void ScriptEngineV8::registerEnum(const QString& enumName, QMetaEnum newEnum) {
    if (!newEnum.isValid()) {
        qCCritical(scriptengine_v8) << "registerEnum called on invalid enum with name " << enumName;
        return;
    }
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());

    for (int i = 0; i < newEnum.keyCount(); i++) {
        const char* keyName = newEnum.key(i);
        QString fullName = enumName + "." + keyName;
        registerValue(fullName, V8ScriptValue(this, v8::Integer::New(_v8Isolate, newEnum.keyToValue(keyName))));
    }
}

void ScriptEngineV8::registerValue(const QString& valueName, V8ScriptValue value) {
    if (QThread::currentThread() != thread()) {
#ifdef THREAD_DEBUGGING
        qCDebug(scriptengine_v8) << "*** WARNING *** ScriptEngineV8::registerValue() called on wrong thread [" << QThread::currentThread() << "], invoking on correct thread [" << thread() << "]";
#endif
        QMetaObject::invokeMethod(this, "registerValue",
                                  Q_ARG(const QString&, valueName),
                                  Q_ARG(V8ScriptValue, value));
        return;
    }
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Local<v8::Context> context = getContext();
    v8::Context::Scope contextScope(getContext());
    QStringList pathToValue = valueName.split(".");
    int partsToGo = pathToValue.length();
    v8::Local<v8::Object> partObject = context->Global();

    for (const auto& pathPart : pathToValue) {
        partsToGo--;
        v8::Local<v8::String> pathPartV8 = v8::String::NewFromUtf8(_v8Isolate, pathPart.toStdString().c_str(),v8::NewStringType::kNormal).ToLocalChecked();
        v8::Local<v8::Value> currentPath;
        bool createProperty = false;
        if (!partObject->Get(context, pathPartV8).ToLocal(&currentPath)) {
            createProperty = true;
        }
        if (currentPath->IsUndefined()) {
            createProperty = true;
        }
        if (createProperty) {
            if (partsToGo > 0) {
                //This was commented out
                //QObject *object = new QObject;
                v8::Local<v8::Object> partValue = v8::Object::New(_v8Isolate);  //newQObject(object, QScriptEngine::ScriptOwnership);
                //V8ScriptValue partValue = QScriptEngine::newArray();  //newQObject(object, QScriptEngine::ScriptOwnership);
                if (!partObject->Set(context, pathPartV8, partValue).FromMaybe(false)) {
                    Q_ASSERT(false);
                }
            } else {
                //partObject = currentPath->ToObject();
                //V8TODO: do these still happen if asserts are disabled?
                if (!partObject->Set(context, pathPartV8, value.constGet()).FromMaybe(false)) {
                    Q_ASSERT(false);
                }
            }
        }

        v8::Local<v8::Value> child;
        if (!partObject->Get(context, pathPartV8).ToLocal(&child)) {
            Q_ASSERT(false);
        }
        if (partsToGo > 0) {
            if (!child->IsObject()) {
                QString details = *v8::String::Utf8Value(_v8Isolate, child->ToDetailString(context).ToLocalChecked());
                qCDebug(scriptengine_v8) << "ScriptEngineV8::registerValue: Part of path is not an object: " << pathPart << " details: " << details;
                Q_ASSERT(false);
            }
            partObject = v8::Local<v8::Object>::Cast(child);
        }
    }
}

void ScriptEngineV8::registerGlobalObject(const QString& name, QObject* object) {
    if (QThread::currentThread() != thread()) {
#ifdef THREAD_DEBUGGING
        qCDebug(scriptengine_v8) << "*** WARNING *** ScriptEngineV8::registerGlobalObject() called on wrong thread [" << QThread::currentThread() << "], invoking on correct thread [" << thread() << "]  name:" << name;
#endif
        QMetaObject::invokeMethod(this, "registerGlobalObject",
                                  Q_ARG(const QString&, name),
                                  Q_ARG(QObject*, object));
        return;
    }
#ifdef THREAD_DEBUGGING
    qCDebug(scriptengine_v8) << "ScriptEngineV8::registerGlobalObject() called on thread [" << QThread::currentThread() << "] name:" << name;
#endif
    /*bool is_isolate_exit_needed = false;
    if(!_v8Isolate->IsCurrent() && !_v8Locker) {
        // V8TODO: Theoretically only script thread should access this, so it should be safe
        _v8Locker.reset(new v8::Locker(_v8Isolate));
        _v8Isolate->Enter();
        is_isolate_exit_needed = true;
    }*/
    //{
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    Q_ASSERT(_v8Isolate->IsCurrent());
    v8::Local<v8::Context> context = getContext();
    v8::Context::Scope contextScope(context);
    v8::Local<v8::Object> v8GlobalObject = getContext()->Global();
    v8::Local<v8::String> v8Name = v8::String::NewFromUtf8(_v8Isolate, name.toStdString().c_str()).ToLocalChecked();

    // V8TODO: Is IsEmpty check enough or IsValid is needed too?
    if (!v8GlobalObject->Get(getContext(), v8Name).IsEmpty()) {
        if (object) {
            V8ScriptValue value = ScriptObjectV8Proxy::newQObject(this, object, ScriptEngine::QtOwnership);
            if(!v8GlobalObject->Set(getContext(), v8Name, value.get()).FromMaybe(false)) {
                Q_ASSERT(false);
            }
        } else {
            if(!v8GlobalObject->Set(getContext(), v8Name, v8::Null(_v8Isolate)).FromMaybe(false)) {
                Q_ASSERT(false);
            }
        }
    }
    //}
    /*if (is_isolate_exit_needed) {
        _v8Isolate->Exit();
        _v8Locker.reset(nullptr);
    }*/
}

void ScriptEngineV8::registerFunction(const QString& name, ScriptEngine::FunctionSignature functionSignature, int numArguments) {
    //if (QThread::currentThread() != ) {
    //}
    if (QThread::currentThread() != thread()) {
#ifdef THREAD_DEBUGGING
        qCDebug(scriptengine_v8) << "*** WARNING *** ScriptEngineV8::registerFunction() called on wrong thread [" << QThread::currentThread() << "], invoking on correct thread [" << thread() << "] name:" << name;
#endif
        QMetaObject::invokeMethod(this, "registerFunction",
                                  Q_ARG(const QString&, name),
                                  Q_ARG(ScriptEngine::FunctionSignature, functionSignature),
                                  Q_ARG(int, numArguments));
        return;
    }
#ifdef THREAD_DEBUGGING
    qCDebug(scriptengine_v8) << "ScriptEngineV8::registerFunction() called on thread [" << QThread::currentThread() << "] name:" << name;
#endif

    /*bool is_isolate_exit_needed = false;
    if(!_v8Isolate->IsCurrent() && !_v8Locker) {
        // V8TODO: Theoretically only script thread should access this, so it should be safe
        _v8Locker.reset(new v8::Locker(_v8Isolate));
        _v8Isolate->Enter();
        is_isolate_exit_needed = true;
    }
    {*/
        //auto scriptFun = static_cast<ScriptValueV8Wrapper*>(newFunction(functionSignature, numArguments).ptr())->toV8Value().constGet();
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    auto scriptFun = newFunction(functionSignature, numArguments);

    //getContext()->Global().Set();
    globalObject().setProperty(name, scriptFun);
    /*}
    if (is_isolate_exit_needed) {
        _v8Isolate->Exit();
        _v8Locker.reset(nullptr);
    }*/
}

void ScriptEngineV8::registerFunction(const QString& parent, const QString& name, ScriptEngine::FunctionSignature functionSignature, int numArguments) {
    if (QThread::currentThread() != thread()) {
#ifdef THREAD_DEBUGGING
        qCDebug(scriptengine_v8) << "*** WARNING *** ScriptEngineV8::registerFunction() called on wrong thread [" << QThread::currentThread() << "], invoking on correct thread [" << thread() << "] parent:" << parent << "name:" << name;
#endif
        QMetaObject::invokeMethod(this, "registerFunction",
                                  Q_ARG(const QString&, name),
                                  Q_ARG(ScriptEngine::FunctionSignature, functionSignature),
                                  Q_ARG(int, numArguments));
        return;
    }
#ifdef THREAD_DEBUGGING
    qCDebug(scriptengine_v8) << "ScriptEngineV8::registerFunction() called on thread [" << QThread::currentThread() << "] parent:" << parent << "name:" << name;
#endif

    /*bool is_isolate_exit_needed = false;
    if(!_v8Isolate->IsCurrent() && !_v8Locker) {
        // V8TODO: Theoretically only script thread should access this, so it should be safe
        _v8Locker.reset(new v8::Locker(_v8Isolate));
        _v8Isolate->Enter();
        is_isolate_exit_needed = true;
    }
    {*/
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    ScriptValue object = globalObject().property(parent);
    if (object.isValid()) {
        ScriptValue scriptFun = newFunction(functionSignature, numArguments);
        object.setProperty(name, scriptFun);
    }
    /*}
    if (is_isolate_exit_needed) {
        _v8Isolate->Exit();
        _v8Locker.reset(nullptr);
    }*/
}

void ScriptEngineV8::registerGetterSetter(const QString& name, ScriptEngine::FunctionSignature getter,
                                        ScriptEngine::FunctionSignature setter, const QString& parent) {
    if (QThread::currentThread() != thread()) {
#ifdef THREAD_DEBUGGING
        qCDebug(scriptengine_v8) << "*** WARNING *** ScriptEngineV8::registerGetterSetter() called on wrong thread [" << QThread::currentThread() << "], invoking on correct thread [" << thread() << "] "
            " name:" << name << "parent:" << parent;
#endif
        QMetaObject::invokeMethod(this, "registerGetterSetter",
                                  Q_ARG(const QString&, name),
                                  Q_ARG(ScriptEngine::FunctionSignature, getter),
                                  Q_ARG(ScriptEngine::FunctionSignature, setter),
                                  Q_ARG(const QString&, parent));
        return;
    }
#ifdef THREAD_DEBUGGING
    qCDebug(scriptengine_v8) << "ScriptEngineV8::registerGetterSetter() called on thread [" << QThread::currentThread() << "] name:" << name << "parent:" << parent;
#endif

    /*bool is_isolate_exit_needed = false;
    if(!_v8Isolate->IsCurrent() && !_v8Locker) {
        // V8TODO: Theoretically only script thread should access this, so it should be safe
        _v8Locker.reset(new v8::Locker(_v8Isolate));
        _v8Isolate->Enter();
        is_isolate_exit_needed = true;
    }
    {*/
        v8::Locker locker(_v8Isolate);
        v8::Isolate::Scope isolateScope(_v8Isolate);
        v8::HandleScope handleScope(_v8Isolate);
        v8::Context::Scope contextScope(getContext());

        /*auto getterFunction = [](v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
        //V8TODO: is using GetCurrentContext ok, or context wrapper needs to be added?
        v8::HandleScope handleScope(info.GetIsolate());
        auto context = info.GetIsolate()->GetCurrentContext();
        v8::Context::Scope contextScope(context);
        auto object = v8::Local<v8::Object>::Cast(info.Data());
        Q_ASSERT(object->InternalFieldCount() == 2);
        auto function = reinterpret_cast<ScriptEngine::FunctionSignature>
            (object->GetAlignedPointerFromInternalField(0));
        ScriptEngineV8 *scriptEngine = reinterpret_cast<ScriptEngineV8*>
            (object->GetAlignedPointerFromInternalField(1));
        ScriptContextV8Wrapper scriptContext(scriptEngine, &info);
        //V8TODO: this scriptContext needs to have FunctionCallbackInfo added
        ScriptValue result = function(&scriptContext, scriptEngine);
        ScriptValueV8Wrapper* unwrapped = ScriptValueV8Wrapper::unwrap(result);
        info.GetReturnValue().Set(unwrapped->toV8Value().constGet());
    };
    auto setterFunction = [](v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
        //V8TODO: is using GetCurrentContext ok, or context wrapper needs to be added?
        v8::HandleScope handleScope(info.GetIsolate());
        auto context = info.GetIsolate()->GetCurrentContext();
        v8::Context::Scope contextScope(context);
        auto object = v8::Local<v8::Object>::Cast(info.Data());
        Q_ASSERT(object->InternalFieldCount() == 2);
        auto function = reinterpret_cast<ScriptEngine::FunctionSignature>
            (object->GetAlignedPointerFromInternalField(0));
        ScriptEngineV8 *scriptEngine = reinterpret_cast<ScriptEngineV8*>
            (object->GetAlignedPointerFromInternalField(1));
        ScriptContextV8Wrapper scriptContext(scriptEngine, &info);
        //V8TODO: this scriptContext needs to have FunctionCallbackInfo added
        ScriptValue result = function(&scriptContext, scriptEngine);
        ScriptValueV8Wrapper* unwrapped = ScriptValueV8Wrapper::unwrap(result);
    };*/

        ScriptValue setterFunction = newFunction(setter, 1);
        ScriptValue getterFunction = newFunction(getter);
        V8ScriptValue unwrappedGetter = ScriptValueV8Wrapper::fullUnwrap(this, getterFunction);
        V8ScriptValue unwrappedSetter = ScriptValueV8Wrapper::fullUnwrap(this, setterFunction);
        v8::PropertyDescriptor propertyDescriptor(unwrappedGetter.get(), unwrappedSetter.get());

        //V8TODO: Getters/setters are probably done in a different way in V8. Maybe object template is needed?
        if (!parent.isNull() && !parent.isEmpty()) {
            ScriptValue object = globalObject().property(parent);
            if (object.isValid()) {
                V8ScriptValue v8parent = ScriptValueV8Wrapper::fullUnwrap(this, object);
                Q_ASSERT(v8parent.get()->IsObject());
                v8::Local<v8::Object> v8ParentObject = v8::Local<v8::Object>::Cast(v8parent.get());
                v8::Local<v8::String> v8propertyName =
                    v8::String::NewFromUtf8(_v8Isolate, name.toStdString().c_str()).ToLocalChecked();
                v8::Local<v8::Object> v8ObjectToSetProperty;
                ScriptObjectV8Proxy *proxy = ScriptObjectV8Proxy::unwrapProxy(V8ScriptValue(this, v8ParentObject));
                // If object is ScriptObjectV8Proxy, then setting property needs to be handled differently
                if (proxy) {
                    v8ObjectToSetProperty = v8ParentObject->GetInternalField(2).As<v8::Object>();
                } else {
                    v8ObjectToSetProperty = v8ParentObject;
                }
                    if (!v8ObjectToSetProperty->DefineProperty(getContext(), v8propertyName, propertyDescriptor).FromMaybe(false)) {
                    qCDebug(scriptengine_v8) << "DefineProperty failed for registerGetterSetter \"" << name << "\" for parent: \""
                                          << parent << "\"";
                }
                //object.setProperty(name, setterFunction, ScriptValue::PropertySetter);
                //object.setProperty(name, getterFunction, ScriptValue::PropertyGetter);
            } else {
                qCDebug(scriptengine_v8) << "Parent object \"" << parent << "\" for registerGetterSetter \"" << name
                                      << "\" is not valid: ";
            }
        } else {
            v8::Local<v8::String> v8propertyName =
                v8::String::NewFromUtf8(_v8Isolate, name.toStdString().c_str()).ToLocalChecked();
            if (!getContext()->Global()->DefineProperty(getContext(), v8propertyName, propertyDescriptor).FromMaybe(false)) {
                qCDebug(scriptengine_v8) << "DefineProperty failed for registerGetterSetter \"" << name << "\" for global object";
            }
            //globalObject().setProperty(name, setterFunction, ScriptValue::PropertySetter);
            //globalObject().setProperty(name, getterFunction, ScriptValue::PropertyGetter);
        }
    /*}
    if (is_isolate_exit_needed) {
        _v8Isolate->Exit();
        _v8Locker.reset(nullptr);
    }*/
}

v8::Local<v8::Context> ScriptEngineV8::getContext() {
    v8::EscapableHandleScope handleScope(_v8Isolate);
    Q_ASSERT(!_contexts.isEmpty());
    return handleScope.Escape(_contexts.last().get()->toV8Value());
}

const v8::Local<v8::Context> ScriptEngineV8::getConstContext() const {
    v8::EscapableHandleScope handleScope(_v8Isolate);
    Q_ASSERT(!_contexts.isEmpty());
    return handleScope.Escape(_contexts.last().get()->toV8Value());
}

// Stored objects are used to create global objects for evaluateInClosure
void ScriptEngineV8::storeGlobalObjectContents() {
    if (areGlobalObjectContentsStored) {
        return;
    }
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    auto context = getContext();
    v8::Context::Scope contextScope(context);
    v8::Local<v8::Object> globalMemberObjects = v8::Object::New(_v8Isolate);

    auto globalMemberNames = context->Global()->GetPropertyNames(context).ToLocalChecked();
    for (size_t i = 0; i < globalMemberNames->Length(); i++) {
        auto name = globalMemberNames->Get(context, i).ToLocalChecked();
        if(!globalMemberObjects->Set(context, name, context->Global()->Get(context, name).ToLocalChecked()).FromMaybe(false)) {
            Q_ASSERT(false);
        }
    }

    _globalObjectContents.Reset(_v8Isolate, globalMemberObjects);
    qCDebug(scriptengine_v8) << "ScriptEngineV8::storeGlobalObjectContents: " << globalMemberNames->Length() << " objects stored";
    areGlobalObjectContentsStored = true;
}

ScriptValue ScriptEngineV8::evaluateInClosure(const ScriptValue& _closure,
                                                           const ScriptProgramPointer& _program) {
    PROFILE_RANGE(script, "evaluateInClosure");
    if (!IS_THREADSAFE_INVOCATION(thread(), __FUNCTION__)) {
        return nullValue();
    }
    _evaluatingCounter++;
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    storeGlobalObjectContents();

    v8::Local<v8::Object> closureObject;
    //v8::Local<v8::Value> oldGlobal;
    v8::Local<v8::Value> closureGlobal;
    ScriptValueV8Wrapper* unwrappedClosure;
    ScriptProgramV8Wrapper* unwrappedProgram;
    //v8::Local<v8::Context> oldContext = getContext();

    {
        v8::Context::Scope contextScope(getContext());
        unwrappedProgram = ScriptProgramV8Wrapper::unwrap(_program);
        if (unwrappedProgram == nullptr) {
            _evaluatingCounter--;
            qCDebug(scriptengine_v8) << "Cannot unwrap program for closure";
            Q_ASSERT(false);
            return nullValue();
        }
        // V8TODO: is another context switch necessary after unwrapping closure?

        const auto fileName = unwrappedProgram->fileName();
        const auto shortName = QUrl(fileName).fileName();

        unwrappedClosure = ScriptValueV8Wrapper::unwrap(_closure);
        if (unwrappedClosure == nullptr) {
            _evaluatingCounter--;
            qCDebug(scriptengine_v8) << "Cannot unwrap closure";
            Q_ASSERT(false);
            return nullValue();
        }

        const V8ScriptValue& closure = unwrappedClosure->toV8Value();
        //const V8ScriptProgram& program = unwrappedProgram->toV8Value();
        if (!closure.constGet()->IsObject()) {
            _evaluatingCounter--;
            qCDebug(scriptengine_v8) << "Unwrapped closure is not an object";
            Q_ASSERT(false);
            return nullValue();
        }
        Q_ASSERT(closure.constGet()->IsObject());
        closureObject = v8::Local<v8::Object>::Cast(closure.constGet());
        qCDebug(scriptengine_v8) << "Closure object members:" << scriptValueDebugListMembersV8(closure);
        v8::Local<v8::Object> testObject = v8::Object::New(_v8Isolate);
        if(!testObject->Set(getContext(), v8::String::NewFromUtf8(_v8Isolate, "test_value").ToLocalChecked(), closureObject).FromMaybe(false)) {
            Q_ASSERT(false);
        }
        qCDebug(scriptengine_v8) << "Test object members:" << scriptValueDebugListMembersV8(V8ScriptValue(this, testObject));

        if (!closureObject->Get(closure.constGetContext(), v8::String::NewFromUtf8(_v8Isolate, "global").ToLocalChecked())
                 .ToLocal(&closureGlobal)) {
            _evaluatingCounter--;
            qCDebug(scriptengine_v8) << "Cannot get global from unwrapped closure";
            Q_ASSERT(false);
            return nullValue();
        }
        //qCDebug(scriptengine_v8) << "Closure global details:" << scriptValueDebugDetailsV8(V8ScriptValue(_v8Isolate, closureGlobal));
    }
    //oldGlobal = _v8Context.Get(_v8Isolate)->Global();
    v8::Local<v8::Context> closureContext;

    // V8TODO V8 cannot use arbitrary objects as global objects
    /*if (closureGlobal->IsObject()) {
#ifdef DEBUG_JS
        qCDebug(shared) << " setting global = closure.global" << shortName;
#endif
        closureContext = v8::Context::New(_v8Isolate, nullptr, v8::Local<v8::ObjectTemplate>(), closureGlobal);
        closureContext = v8::Context::New(_v8Isolate, nullptr, v8::Local<v8::ObjectTemplate>(), closureGlobal);
        //setGlobalObject(global);
    } else {
        closureContext = v8::Context::New(_v8Isolate);
    }*/
    closureContext = v8::Context::New(_v8Isolate);
    pushContext(closureContext);

    ScriptValue result;
    //auto context = pushContext();

    // V8TODO: a lot of functions rely on _v8Context, which was not updated here
    // It might cause trouble
    {
        v8::Context::Scope contextScope(closureContext);
        //const V8ScriptValue& closure = unwrappedClosure->toV8Value();
        if (!unwrappedProgram->compile()) {
            qCDebug(scriptengine_v8) << "Can't compile script for evaluating in closure";
            Q_ASSERT(false);
            popContext();
            return nullValue();
        }
        const V8ScriptProgram& program = unwrappedProgram->toV8Value();

        v8::Local<v8::Value> thiz;
        // V8TODO: not sure if "this" is used at all here
        /*if (!closureObject->Get(closure.constGetContext(), v8::String::NewFromUtf8(_v8Isolate, "this").ToLocalChecked())
                 .ToLocal(&thiz)) {
            _evaluatingCounter--;
            qCDebug(scriptengine_v8) << "Empty this object in closure";
            Q_ASSERT(false);
            return nullValue();
        }*/
        //thiz = closure.property("this");
        //qCDebug(scriptengine_v8) << "Closure this details:" << scriptValueDebugDetailsV8(V8ScriptValue(_v8Isolate, thiz));
        // V8TODO:
        /*if (thiz->IsObject()) {
#ifdef DEBUG_JS
            qCDebug(shared) << " setting this = closure.this" << shortName;
#endif
            //V8TODO I don't know how to do this in V8, will adding "this" to global object work?
            closureContext->Global()->Set(closureContext, v8::String::NewFromUtf8(_v8Isolate, "this").ToLocalChecked(), thiz);
            //context->setThisObject(thiz);
        }*/

        //context->pushScope(closure);
#ifdef DEBUG_JS
        qCDebug(shared) << QString("[%1] evaluateInClosure %2").arg(isEvaluating()).arg(shortName);
#endif
        {
            v8::TryCatch tryCatch(getIsolate());
            // Since V8 cannot use arbitrary object as global object, objects from main global need to be copied to closure's global object
            auto globalObjectContents = _globalObjectContents.Get(_v8Isolate);
            auto globalMemberNames = globalObjectContents->GetPropertyNames(globalObjectContents->CreationContext()).ToLocalChecked();
            for (size_t i = 0; i < globalMemberNames->Length(); i++) {
                auto name = globalMemberNames->Get(closureContext, i).ToLocalChecked();
                if(!closureContext->Global()->Set(closureContext, name, globalObjectContents->Get(globalObjectContents->CreationContext(), name).ToLocalChecked()).FromMaybe(false)) {
                    Q_ASSERT(false);
                }
            }
            qCDebug(scriptengine_v8) << "ScriptEngineV8::evaluateInClosure: " << globalMemberNames->Length() << " objects added to global";

            /*auto oldGlobalMemberNames = oldContext->Global()->GetPropertyNames(oldContext).ToLocalChecked();
            //auto oldGlobalMemberNames = oldContext->Global()->GetPropertyNames(closureContext).ToLocalChecked();
            for (size_t i = 0; i < oldGlobalMemberNames->Length(); i++) {
                auto name = oldGlobalMemberNames->Get(closureContext, i).ToLocalChecked();
                //auto name = oldGlobalMemberNames->Get(oldContext, i).ToLocalChecked();
                if(!closureContext->Global()->Set(closureContext, name, oldContext->Global()->Get(oldContext, name).ToLocalChecked()).FromMaybe(false)) {
                //if(!closureContext->Global()->Set(closureContext, name, oldContext->Global()->Get(closureContext, name).ToLocalChecked()).FromMaybe(false)) {
                    Q_ASSERT(false);
                }
            }*/
            // Objects from closure need to be copied to global object too
            // V8TODO: I'm not sure which context to use with Get
            auto closureMemberNames = closureObject->GetPropertyNames(closureContext).ToLocalChecked();
            //auto closureMemberNames = closureObject->GetPropertyNames(oldContext).ToLocalChecked();
            for (size_t i = 0; i < closureMemberNames->Length(); i++) {
                auto name = closureMemberNames->Get(closureContext, i).ToLocalChecked();
                //auto name = closureMemberNames->Get(oldContext, i).ToLocalChecked();
                if(!closureContext->Global()->Set(closureContext, name, closureObject->Get(closureContext, name).ToLocalChecked()).FromMaybe(false)) {
                //if(!closureContext->Global()->Set(closureContext, name, closureObject->Get(oldContext, name).ToLocalChecked()).FromMaybe(false)) {
                    Q_ASSERT(false);
                }
            }
            // List members of closure global object
            //QString membersString("");
            /*if (closureContext->Global()->IsObject()) {
                v8::Local<v8::String> membersStringV8;
                v8::Local<v8::Object> object = v8::Local<v8::Object>::Cast(closureContext->Global());
                auto names = object->GetPropertyNames(closureContext).ToLocalChecked();
                if (v8::JSON::Stringify(closureContext, names).ToLocal(&membersStringV8)) {
                    membersString = QString(*v8::String::Utf8Value(_v8Isolate, membersStringV8));
                }
                membersString = QString(*v8::String::Utf8Value(_v8Isolate, membersStringV8));
            } else {
                membersString = QString(" Is not an object");
            }*/
            //qCDebug(scriptengine_v8) << "Closure global before run:" << membersString;
            auto maybeResult = program.constGet()->GetUnboundScript()->BindToCurrentContext()->Run(closureContext);
            //qCDebug(scriptengine_v8) << "Closure after run:" << scriptValueDebugDetailsV8(closure);
            v8::Local<v8::Value> v8Result;
            if (!maybeResult.ToLocal(&v8Result)) {
                v8::String::Utf8Value utf8Value(getIsolate(), tryCatch.Exception());
                QString errorMessage = QString(*utf8Value);
                qCWarning(scriptengine_v8) << __FUNCTION__ << "---------- hasCaught:" << errorMessage;
                qCWarning(scriptengine_v8) << __FUNCTION__ << "---------- tryCatch details:" << formatErrorMessageFromTryCatch(tryCatch);
                //V8TODO: better error reporting
            }

            if (hasUncaughtException()) {
#ifdef DEBUG_JS_EXCEPTIONS
                qCWarning(shared) << __FUNCTION__ << "---------- hasCaught:" << err.toString() << result.toString();
                err.setProperty("_result", result);
#endif
                result = nullValue();
            } else {
                result = ScriptValue(new ScriptValueV8Wrapper(this, V8ScriptValue(this, v8Result)));
            }
        }
#ifdef DEBUG_JS
        qCDebug(shared) << QString("[%1] //evaluateInClosure %2").arg(isEvaluating()).arg(shortName);
#endif
        popContext();
    }
    //This is probably unnecessary in V8
    /*if (oldGlobal.isValid()) {
#ifdef DEBUG_JS
        qCDebug(shared) << " restoring global" << shortName;
#endif
        setGlobalObject(oldGlobal);
    }*/

    _evaluatingCounter--;
    return result;
}

ScriptValue ScriptEngineV8::evaluate(const QString& sourceCode, const QString& fileName) {
    //V8TODO

    if (QThread::currentThread() != thread()) {
        ScriptValue result;
#ifdef THREAD_DEBUGGING
        qCDebug(scriptengine_v8) << "*** WARNING *** ScriptEngineV8::evaluate() called on wrong thread [" << QThread::currentThread() << "], invoking on correct thread [" << thread() << "] "
            "sourceCode:" << sourceCode << " fileName:" << fileName;
#endif
        BLOCKING_INVOKE_METHOD(this, "evaluate",
                                  Q_RETURN_ARG(ScriptValue, result),
                                  Q_ARG(const QString&, sourceCode),
                                  Q_ARG(const QString&, fileName));
        return result;
    }
    // Compile and check syntax
    // V8TODO: Could these all be replaced with checkSyntax function from wrapper?
    Q_ASSERT(!_v8Isolate->IsDead());
    _evaluatingCounter++;
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    v8::ScriptOrigin scriptOrigin(getIsolate(), v8::String::NewFromUtf8(getIsolate(), fileName.toStdString().c_str()).ToLocalChecked());
    v8::Local<v8::Script> script;
    {
        v8::TryCatch tryCatch(getIsolate());
        if (!v8::Script::Compile(getContext(), v8::String::NewFromUtf8(getIsolate(), sourceCode.toStdString().c_str()).ToLocalChecked(), &scriptOrigin).ToLocal(&script)) {
            setUncaughtException(tryCatch, "Error while compiling script");
            _evaluatingCounter--;
            return nullValue();
        }
    }
    //qCDebug(scriptengine_v8) << "Script compilation succesful: " << fileName;

    //V8TODO
    /*auto syntaxError = lintScript(sourceCode, fileName);
    if (syntaxError.isError()) {
        if (!isEvaluating()) {
            syntaxError.setProperty("detail", "evaluate");
        }
        raiseException(syntaxError);
        maybeEmitUncaughtException("lint");
        return syntaxError;
    }*/
    //V8TODO
    /*if (script->IsNull()) {
        // can this happen?
        auto err = makeError(newValue("could not create V8ScriptProgram for " + fileName));
        raiseException(err);
        maybeEmitUncaughtException("compile");
        return err;
    }*/

    v8::Local<v8::Value> result;
    v8::TryCatch tryCatchRun(getIsolate());
    if (!script->Run(getContext()).ToLocal(&result)) {
        Q_ASSERT(tryCatchRun.HasCaught());
        auto runError = tryCatchRun.Message();
        ScriptValue errorValue(new ScriptValueV8Wrapper(this, V8ScriptValue(this, runError->Get())));
        qCDebug(scriptengine_v8) << "Running script: \"" << fileName << "\" " << formatErrorMessageFromTryCatch(tryCatchRun);
        //V8TODO


        //raiseException(errorValue);
        //maybeEmitUncaughtException("evaluate");
        setUncaughtException(tryCatchRun, "script evaluation");

        _evaluatingCounter--;
        return errorValue;
    }
    V8ScriptValue resultValue(this, result);
    _evaluatingCounter--;
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(resultValue)));
}


void ScriptEngineV8::setUncaughtEngineException(const QString &reason, const QString& info) {
    auto ex = std::make_shared<ScriptEngineException>(reason, info);
    setUncaughtException(ex);
}

void ScriptEngineV8::setUncaughtException(const v8::TryCatch &tryCatch, const QString& info) {
    if (!tryCatch.HasCaught()) {
        qCWarning(scriptengine_v8) << "setUncaughtException called without exception";
        clearExceptions();
        return;
    }

    auto ex = std::make_shared<ScriptRuntimeException>();
    ex->additionalInfo = info;

    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    QString result("");

    QString errorMessage = "";
    QString errorBacktrace = "";
    //v8::String::Utf8Value utf8Value(getIsolate(), tryCatch.Exception());
    v8::String::Utf8Value utf8Value(getIsolate(), tryCatch.Message()->Get());

    ex->errorMessage = QString(*utf8Value);

    auto exceptionValue = tryCatch.Exception();
    ex->thrownValue =  ScriptValue(new ScriptValueV8Wrapper(this, V8ScriptValue(this, exceptionValue)));


    v8::Local<v8::Message> exceptionMessage = tryCatch.Message();
    if (!exceptionMessage.IsEmpty()) {
        ex->errorLine = exceptionMessage->GetLineNumber(getContext()).FromJust();
        ex->errorColumn = exceptionMessage->GetStartColumn(getContext()).FromJust();
        v8::Local<v8::Value> backtraceV8String;
        if (tryCatch.StackTrace(getContext()).ToLocal(&backtraceV8String)) {
            if (backtraceV8String->IsString()) {
                if (v8::Local<v8::String>::Cast(backtraceV8String)->Length() > 0) {
                    v8::String::Utf8Value backtraceUtf8Value(getIsolate(), backtraceV8String);
                    QString errorBacktrace = *backtraceUtf8Value;
                    ex->backtrace = errorBacktrace.split("\n");

                }
            }
        }
    }

    setUncaughtException(ex);
}

void ScriptEngineV8::setUncaughtException(std::shared_ptr<ScriptException> uncaughtException) {
    qCDebug(scriptengine_v8) << "Emitting exception:" << uncaughtException;
    _uncaughtException = uncaughtException;

    auto copy = uncaughtException->clone();
    emit exception(copy);
}


QString ScriptEngineV8::formatErrorMessageFromTryCatch(v8::TryCatch &tryCatch) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    QString result("");
    int errorColumnNumber = 0;
    int errorLineNumber = 0;
    QString errorMessage = "";
    QString errorBacktrace = "";
    //v8::String::Utf8Value utf8Value(getIsolate(), tryCatch.Exception());
    v8::String::Utf8Value utf8Value(getIsolate(), tryCatch.Message()->Get());
    errorMessage = QString(*utf8Value);
    v8::Local<v8::Message> exceptionMessage = tryCatch.Message();
    if (!exceptionMessage.IsEmpty()) {
        errorLineNumber = exceptionMessage->GetLineNumber(getContext()).FromJust();
        errorColumnNumber = exceptionMessage->GetStartColumn(getContext()).FromJust();
        v8::Local<v8::Value> backtraceV8String;
        if (tryCatch.StackTrace(getContext()).ToLocal(&backtraceV8String)) {
            if (backtraceV8String->IsString()) {
                if (v8::Local<v8::String>::Cast(backtraceV8String)->Length() > 0) {
                    v8::String::Utf8Value backtraceUtf8Value(getIsolate(), backtraceV8String);
                    errorBacktrace = *backtraceUtf8Value;
                }
            }
        }
        QTextStream resultStream(&result);
        resultStream << "failed on line " << errorLineNumber << " column " << errorColumnNumber << " with message: \"" << errorMessage <<"\" backtrace: " << errorBacktrace;
    }
    return result;
}

v8::Local<v8::ObjectTemplate> ScriptEngineV8::getObjectProxyTemplate() {
    v8::EscapableHandleScope handleScope(_v8Isolate);
    if (_objectProxyTemplate.IsEmpty()) {
        auto objectTemplate = v8::ObjectTemplate::New(_v8Isolate);
        objectTemplate->SetInternalFieldCount(3);
        objectTemplate->SetHandler(v8::NamedPropertyHandlerConfiguration(ScriptObjectV8Proxy::v8Get, ScriptObjectV8Proxy::v8Set, nullptr, nullptr, ScriptObjectV8Proxy::v8GetPropertyNames));
        _objectProxyTemplate.Reset(_v8Isolate, objectTemplate);
    }

    return handleScope.Escape(_objectProxyTemplate.Get(_v8Isolate));
}

v8::Local<v8::ObjectTemplate> ScriptEngineV8::getMethodDataTemplate() {
    v8::EscapableHandleScope handleScope(_v8Isolate);
    if (_methodDataTemplate.IsEmpty()) {
        auto methodDataTemplate = v8::ObjectTemplate::New(_v8Isolate);
        methodDataTemplate->SetInternalFieldCount(2);
        _methodDataTemplate.Reset(_v8Isolate, methodDataTemplate);
    }

    return handleScope.Escape(_methodDataTemplate.Get(_v8Isolate));
}

v8::Local<v8::ObjectTemplate> ScriptEngineV8::getFunctionDataTemplate() {
    v8::EscapableHandleScope handleScope(_v8Isolate);
    if (_functionDataTemplate.IsEmpty()) {
        auto functionDataTemplate = v8::ObjectTemplate::New(_v8Isolate);
        functionDataTemplate->SetInternalFieldCount(2);
        _functionDataTemplate.Reset(_v8Isolate, functionDataTemplate);
    }

    return handleScope.Escape(_functionDataTemplate.Get(_v8Isolate));
}

v8::Local<v8::ObjectTemplate> ScriptEngineV8::getVariantDataTemplate() {
    v8::EscapableHandleScope handleScope(_v8Isolate);
    if (_variantDataTemplate.IsEmpty()) {
        auto variantDataTemplate = v8::ObjectTemplate::New(_v8Isolate);
        variantDataTemplate->SetInternalFieldCount(2);
        _variantDataTemplate.Reset(_v8Isolate, variantDataTemplate);
    }

    return handleScope.Escape(_variantDataTemplate.Get(_v8Isolate));
}

v8::Local<v8::ObjectTemplate> ScriptEngineV8::getVariantProxyTemplate() {
    v8::EscapableHandleScope handleScope(_v8Isolate);
    if (_variantProxyTemplate.IsEmpty()) {
        auto variantProxyTemplate = v8::ObjectTemplate::New(_v8Isolate);
        variantProxyTemplate->SetInternalFieldCount(2);
        variantProxyTemplate->SetHandler(v8::NamedPropertyHandlerConfiguration(ScriptVariantV8Proxy::v8Get, ScriptVariantV8Proxy::v8Set, nullptr, nullptr, ScriptVariantV8Proxy::v8GetPropertyNames));
        _variantProxyTemplate.Reset(_v8Isolate, variantProxyTemplate);
    }

    return handleScope.Escape(_variantProxyTemplate.Get(_v8Isolate));
}


ScriptContextV8Pointer ScriptEngineV8::pushContext(v8::Local<v8::Context> context) {
    v8::HandleScope handleScope(_v8Isolate);
    Q_ASSERT(!_contexts.isEmpty());
    ScriptContextPointer parent = _contexts.last();
    _contexts.append(std::make_shared<ScriptContextV8Wrapper>(this, context, ScriptContextPointer()));
    v8::Context::Scope contextScope(context);
    static volatile int debug_context_id = 1;
    if (!context->Global()->Set(context, v8::String::NewFromUtf8(_v8Isolate, "debug_context_id").ToLocalChecked(), v8::Integer::New(_v8Isolate, debug_context_id)).FromMaybe(false)) {
        Q_ASSERT(false);
    }
    debug_context_id++;
    return _contexts.last();
}

void ScriptEngineV8::popContext() {
    Q_ASSERT(!_contexts.isEmpty());
    _contexts.pop_back();
}

Q_INVOKABLE ScriptValue ScriptEngineV8::evaluate(const ScriptProgramPointer& program) {

    if (QThread::currentThread() != thread()) {
        ScriptValue result;
#ifdef THREAD_DEBUGGING
        qCDebug(scriptengine_v8) << "*** WARNING *** ScriptEngineV8::evaluate() called on wrong thread [" << QThread::currentThread() << "], invoking on correct thread [" << thread() << "] "
            "sourceCode:" << sourceCode << " fileName:" << fileName;
#endif
        BLOCKING_INVOKE_METHOD(this, "evaluate",
                                  Q_RETURN_ARG(ScriptValue, result),
                                  Q_ARG(const ScriptProgramPointer&, program));
        return result;
    }
    _evaluatingCounter++;
    /*bool is_isolate_exit_needed = false;
    if(!_v8Isolate->IsCurrent() && !_v8Locker) {
        // V8TODO: Theoretically only script thread should access this, so it should be safe
        _v8Locker.reset(new v8::Locker(_v8Isolate));
        _v8Isolate->Enter();
        is_isolate_exit_needed = true;
    }*/
    ScriptValue errorValue;
    ScriptValue resultValue;
    bool hasFailed = false;
    {
        v8::Locker locker(_v8Isolate);
        v8::Isolate::Scope isolateScope(_v8Isolate);
        v8::HandleScope handleScope(_v8Isolate);
        v8::Context::Scope contextScope(getContext());
        ScriptProgramV8Wrapper* unwrapped = ScriptProgramV8Wrapper::unwrap(program);
        if (!unwrapped) {
            setUncaughtEngineException("Could not unwrap program", "Compile error");
            hasFailed = true;
        }

        if(!hasFailed) {
            ScriptSyntaxCheckResultPointer syntaxCheck = unwrapped->checkSyntax();
            if (syntaxCheck->state() == ScriptSyntaxCheckResult::Error) {
                setUncaughtEngineException(syntaxCheck->errorMessage(), "Compile error");
                hasFailed = true;
            }
        }

        v8::Local<v8::Value> result;
        if(!hasFailed) {
            const V8ScriptProgram& v8Program = unwrapped->toV8Value();
            // V8TODO
            /*if (qProgram.isNull()) {
        // can this happen?
        auto err = makeError(newValue("requested program is empty"));
        raiseException(err);
        maybeEmitUncaughtException("compile");
        return err;
    }*/

            v8::TryCatch tryCatchRun(getIsolate());
            if (!v8Program.constGet()->Run(getContext()).ToLocal(&result)) {
                Q_ASSERT(tryCatchRun.HasCaught());
                auto runError = tryCatchRun.Message();
                errorValue = ScriptValue(new ScriptValueV8Wrapper(this, V8ScriptValue(this, runError->Get())));
                raiseException(errorValue, "evaluation error");
                hasFailed = true;
            } else {
                // V8TODO this is just to check if run will always return false for uncaught exception
                Q_ASSERT(!tryCatchRun.HasCaught());
            }
        }
        if(!hasFailed) {
            V8ScriptValue resultValueV8(this, result);
            resultValue = ScriptValue(new ScriptValueV8Wrapper(this, std::move(resultValueV8)));
        }
    }
    _evaluatingCounter--;
    /*if (is_isolate_exit_needed) {
        _v8Isolate->Exit();
        _v8Locker.reset(nullptr);
    }*/
    if (hasFailed) {
        return errorValue;
    } else {
        return resultValue;
    }
}


void ScriptEngineV8::updateMemoryCost(const qint64& deltaSize) {
    if (deltaSize > 0) {
        // We've patched qt to fix https://highfidelity.atlassian.net/browse/BUGZ-46 on mac and windows only.
#if defined(Q_OS_WIN) || defined(Q_OS_MAC)
        // V8TODO: it seems to be broken in V8 branch on Windows for some reason
        //reportAdditionalMemoryCost(deltaSize);
#endif
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ScriptEngine implementation

ScriptValue ScriptEngineV8::globalObject() {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getConstContext());
    V8ScriptValue global(this, getConstContext()->Global());// = QScriptEngine::globalObject(); // can't cache the value as it may change
    return ScriptValue(new ScriptValueV8Wrapper(const_cast<ScriptEngineV8*>(this), std::move(global)));
}

ScriptValue ScriptEngineV8::newArray(uint length) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    V8ScriptValue result(this, v8::Array::New(_v8Isolate, static_cast<int>(length)));
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::newArrayBuffer(const QByteArray& message) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    //V8TODO: this will leak memory
    std::shared_ptr<v8::BackingStore> backingStore(v8::ArrayBuffer::NewBackingStore(_v8Isolate, message.size()));
    std::memcpy(backingStore.get()->Data(), message.constData(), message.size());
    auto arrayBuffer = v8::ArrayBuffer::New(_v8Isolate, backingStore);
    /*V8ScriptValue data = QScriptEngine::newVariant(QVariant::fromValue(message));
    V8ScriptValue ctor = QScriptEngine::globalObject().property("ArrayBuffer");
    auto array = qscriptvalue_cast<ArrayBufferClass*>(ctor.data());
    if (!array) {
        return undefinedValue();
    }*/
    V8ScriptValue result(this, arrayBuffer);//QScriptEngine::newObject(array, data);
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::newObject() {
    /*bool is_isolate_exit_needed = false;
    if(!_v8Isolate->IsCurrent() && !_v8Locker) {
        // V8TODO: Theoretically only script thread should access this, so it should be safe
        _v8Locker.reset(new v8::Locker(_v8Isolate));
        _v8Isolate->Enter();
        is_isolate_exit_needed = true;
    }*/
    ScriptValue result;
    {
        v8::Locker locker(_v8Isolate);
        v8::Isolate::Scope isolateScope(_v8Isolate);
        v8::HandleScope handleScope(_v8Isolate);
        v8::Context::Scope contextScope(getContext());
        V8ScriptValue resultV8 = V8ScriptValue(this, v8::Object::New(_v8Isolate));
        result = ScriptValue(new ScriptValueV8Wrapper(this, std::move(resultV8)));
    }
    /*if (is_isolate_exit_needed) {
        _v8Isolate->Exit();
        _v8Locker.reset(nullptr);
    }*/
    return result;
}

ScriptValue ScriptEngineV8::newMethod(QObject* object, V8ScriptValue lifetime,
                               const QList<QMetaMethod>& metas, int numMaxParams) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    V8ScriptValue result(ScriptMethodV8Proxy::newMethod(this, object, lifetime, metas, numMaxParams));
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptProgramPointer ScriptEngineV8::newProgram(const QString& sourceCode, const QString& fileName) {
    //V8TODO: is it used between isolates?
    //V8TODO: should it be compiled on creation?
    //V8ScriptProgram result(sourceCode, fileName);

    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    return std::make_shared<ScriptProgramV8Wrapper>(this, sourceCode, fileName);
}

ScriptValue ScriptEngineV8::newQObject(QObject* object,
                                                    ScriptEngine::ValueOwnership ownership,
                                                    const ScriptEngine::QObjectWrapOptions& options) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    V8ScriptValue result = ScriptObjectV8Proxy::newQObject(this, object, ownership, options);
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::newValue(bool value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    V8ScriptValue result(this, v8::Boolean::New(_v8Isolate, value));
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::newValue(int value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    V8ScriptValue result(this, v8::Integer::New(_v8Isolate, value));
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::newValue(uint value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    V8ScriptValue result(this, v8::Uint32::New(_v8Isolate, value));
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::newValue(double value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    V8ScriptValue result(this, v8::Number::New(_v8Isolate, value));
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::newValue(const QString& value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    v8::Local<v8::String> valueV8 = v8::String::NewFromUtf8(_v8Isolate, value.toStdString().c_str(), v8::NewStringType::kNormal).ToLocalChecked();
    V8ScriptValue result(this, valueV8);
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::newValue(const QLatin1String& value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    v8::Local<v8::String> valueV8 = v8::String::NewFromUtf8(_v8Isolate, value.latin1(), v8::NewStringType::kNormal).ToLocalChecked();
    V8ScriptValue result(this, valueV8);
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::newValue(const char* value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    v8::Local<v8::String> valueV8 = v8::String::NewFromUtf8(_v8Isolate, value, v8::NewStringType::kNormal).ToLocalChecked();
    V8ScriptValue result(this, valueV8);
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::newVariant(const QVariant& value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    V8ScriptValue result = castVariantToValue(value);
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

ScriptValue ScriptEngineV8::nullValue() {
    return _nullValue;
}

ScriptValue ScriptEngineV8::undefinedValue() {
    return _undefinedValue;
}

void ScriptEngineV8::abortEvaluation() {
    //V8TODO
    //QScriptEngine::abortEvaluation();
}

void ScriptEngineV8::clearExceptions() {
    _uncaughtException.reset();
}

ScriptContext* ScriptEngineV8::currentContext() const {
    //V8TODO
    /*V8ScriptContext* localCtx = QScriptEngine::currentContext();
    if (!localCtx) {
        return nullptr;
    }
    if (!_currContext || _currContext->toV8Value() != localCtx) {
        _currContext = std::make_shared<ScriptContextV8Wrapper>(const_cast<ScriptEngineV8*>(this), localCtx);
    }*/
    //_currContext = std::make_shared<ScriptContextV8Wrapper>(const_cast<ScriptEngineV8*>(this), localCtx);
    /*if (!_currContext) {
        // I'm not sure how to do this without discarding const
        _currContext = std::make_shared<ScriptContextV8Wrapper>(const_cast<ScriptEngineV8*>(this));
    }*/
    // V8TODO: add FunctionCallbackInfo or PropertyCallbackInfo when necessary
    return _contexts.last().get();
}

bool ScriptEngineV8::hasUncaughtException() const {
    return _uncaughtException != nullptr;
}

bool ScriptEngineV8::isEvaluating() const {
    //return QScriptEngine::isEvaluating();
    return _evaluatingCounter > 0;
    return false;
}

ScriptValue ScriptEngineV8::newFunction(ScriptEngine::FunctionSignature fun, int length) {
    //V8TODO is callee() used for anything?
    /*auto innerFunc = [](V8ScriptContext* _context, QScriptEngine* _engine) -> V8ScriptValue {
        auto callee = _context->callee();
        QVariant funAddr = callee.property("_func").toVariant();
        ScriptEngine::FunctionSignature fun = reinterpret_cast<ScriptEngine::FunctionSignature>(funAddr.toULongLong());
        ScriptEngineV8* engine = static_cast<ScriptEngineV8*>(_engine);
        ScriptContextV8Wrapper context(engine, _context);
        ScriptValue result = fun(&context, engine);
        ScriptValueV8Wrapper* unwrapped = ScriptValueV8Wrapper::unwrap(result);
        return unwrapped ? unwrapped->toV8Value() : V8ScriptValue();
    };*/

    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());

    auto v8FunctionCallback = [](const v8::FunctionCallbackInfo<v8::Value>& info) {
        //V8TODO: is using GetCurrentContext ok, or context wrapper needs to be added?
        v8::HandleScope handleScope(info.GetIsolate());
        auto context = info.GetIsolate()->GetCurrentContext();
        v8::Context::Scope contextScope(context);
        Q_ASSERT(info.Data()->IsObject());
        auto object = v8::Local<v8::Object>::Cast(info.Data());
        Q_ASSERT(object->InternalFieldCount() == 2);
        auto function = reinterpret_cast<ScriptEngine::FunctionSignature>
            (object->GetAlignedPointerFromInternalField(0));
        ScriptEngineV8 *scriptEngine = reinterpret_cast<ScriptEngineV8*>
            (object->GetAlignedPointerFromInternalField(1));
        ScriptContextV8Wrapper scriptContext(scriptEngine, &info, scriptEngine->getContext(), scriptEngine->currentContext()->parentContext());
        ScriptContextGuard scriptContextGuard(&scriptContext);
        ScriptValue result = function(&scriptContext, scriptEngine);
        ScriptValueV8Wrapper* unwrapped = ScriptValueV8Wrapper::unwrap(result);
        if (unwrapped) {
            info.GetReturnValue().Set(unwrapped->toV8Value().constGet());
        }
    };
    //auto functionTemplate = v8::FunctionTemplate::New(_v8Isolate, v8FunctionCallback, v8::Local<v8::Value>(), v8::Local<v8::Signature>(), length);
    //auto functionData = v8::Object::New(_v8Isolate);
    //functionData->setIn
    auto functionDataTemplate = getFunctionDataTemplate();
    //auto functionDataTemplate = v8::ObjectTemplate::New(_v8Isolate);
    //functionDataTemplate->SetInternalFieldCount(2);
    auto functionData = functionDataTemplate->NewInstance(getContext()).ToLocalChecked();
    functionData->SetAlignedPointerInInternalField(0, reinterpret_cast<void*>(fun));
    functionData->SetAlignedPointerInInternalField(1, reinterpret_cast<void*>(this));
    //functionData->SetInternalField(3, v8::Null(_v8Isolate));
    auto v8Function = v8::Function::New(getContext(), v8FunctionCallback, functionData, length).ToLocalChecked();
    //auto functionObjectTemplate = functionTemplate->InstanceTemplate();
    //auto function =
    V8ScriptValue result(this, v8Function); // = QScriptEngine::newFunction(innerFunc, length);
    //auto funAddr = QScriptEngine::newVariant(QVariant(reinterpret_cast<qulonglong>(fun)));
    // V8TODO
    //result.setProperty("_func", funAddr, V8ScriptValue::PropertyFlags(V8ScriptValue::ReadOnly + V8ScriptValue::Undeletable + V8ScriptValue::SkipInEnumeration));
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(result)));
}

//V8TODO
void ScriptEngineV8::setObjectName(const QString& name) {
    QObject::setObjectName(name);
}

//V8TODO
bool ScriptEngineV8::setProperty(const char* name, const QVariant& value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    v8::Local<v8::Object> global = getContext()->Global();
    auto v8Name = v8::String::NewFromUtf8(getIsolate(), name).ToLocalChecked();
    V8ScriptValue v8Value = castVariantToValue(value);
    return global->Set(getContext(), v8Name, v8Value.get()).FromMaybe(false);
}

void ScriptEngineV8::setProcessEventsInterval(int interval) {
    //V8TODO
    //QScriptEngine::setProcessEventsInterval(interval);
}

QThread* ScriptEngineV8::thread() const {
    return QObject::thread();
}

void ScriptEngineV8::setThread(QThread* thread) {
    if (_v8Isolate->IsCurrent()) {
        _v8Isolate->Exit();
        qCDebug(scriptengine_v8) << "Script engine " << objectName() << " exited isolate";
    }
    Q_ASSERT(QObject::thread() == QThread::currentThread());
    /*if (_v8Locker) {
        _v8Locker.reset();
    }*/
    moveToThread(thread);
    qCDebug(scriptengine_v8) << "Moved script engine " << objectName() << " to different thread";
}

/*void ScriptEngineV8::enterIsolateOnThisThread() {
    Q_ASSERT(thread() == QThread::currentThread());
    Q_ASSERT(!_v8Locker);
    _v8Locker.reset(new v8::Locker(_v8Isolate));
    if (!_v8Isolate->IsCurrent()) {
        _v8Isolate->Enter();
        qCDebug(scriptengine_v8) << "Script engine " << objectName() << " entered isolate on a new thread";
    }
}*/


std::shared_ptr<ScriptException> ScriptEngineV8::uncaughtException() const {
    if (_uncaughtException) {
        return _uncaughtException->clone();
    } else {
        return std::shared_ptr<ScriptException>();
    }
}

bool ScriptEngineV8::raiseException(const QString& error, const QString &reason) {
    return raiseException(newValue(error), reason);
}

bool ScriptEngineV8::raiseException(const ScriptValue& exception, const QString &reason) {
    //V8TODO
    //Q_ASSERT(false);
//    qCCritical(scriptengine_v8) << "Script exception occurred: " << exception.toString();
//    ScriptValueV8Wrapper* unwrapped = ScriptValueV8Wrapper::unwrap(exception);
//    V8ScriptValue qException = unwrapped ? unwrapped->toV8Value() : QScriptEngine::newVariant(exception.toVariant());

  //  emit
    //return raiseException(qException);

//    qCCritical(scriptengine_v8) << "Raise exception for reason" << reason << "NOT IMPLEMENTED!";
//    return false;

    return raiseException(ScriptValueV8Wrapper::fullUnwrap(this, exception));
}



bool ScriptEngineV8::raiseException(const V8ScriptValue& exception) {
    if (!IS_THREADSAFE_INVOCATION(thread(), __FUNCTION__)) {
        return false;
    }


    _v8Isolate->ThrowException(exception.constGet());


    /*if (QScriptEngine::currentContext()) {
        // we have an active context / JS stack frame so throw the exception per usual
        QScriptEngine::currentContext()->throwValue(makeError(exception));
        return true;
    } else if (_scriptManager) {
        // we are within a pure C++ stack frame (ie: being called directly by other C++ code)
        // in this case no context information is available so just emit the exception for reporting
        V8ScriptValue thrown = makeError(exception);
        emit _scriptManager->unhandledException(ScriptValue(new ScriptValueV8Wrapper(this, std::move(thrown))));
    }*/
    //emit _scriptManager->unhandledException(ScriptValue(new ScriptValueV8Wrapper(this, std::move(thrown))));
    return false;
}


ScriptValue ScriptEngineV8::create(int type, const void* ptr) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    QVariant variant(type, ptr);
    V8ScriptValue scriptValue = castVariantToValue(variant);
    return ScriptValue(new ScriptValueV8Wrapper(this, std::move(scriptValue)));
}

QVariant ScriptEngineV8::convert(const ScriptValue& value, int typeId) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    ScriptValueV8Wrapper* unwrapped = ScriptValueV8Wrapper::unwrap(value);
    if (unwrapped == nullptr) {
        return QVariant();
    }

    QVariant var;
    if (!castValueToVariant(unwrapped->toV8Value(), var, typeId)) {
        return QVariant();
    }

    int destType = var.userType();
    if (destType != typeId) {
        var.convert(typeId);  // if conversion fails then var is set to QVariant()
    }

    return var;
    return QVariant();
}

void ScriptEngineV8::compileTest() {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());
    v8::Local<v8::Script> script;
    v8::ScriptOrigin scriptOrigin(getIsolate(), v8::String::NewFromUtf8(getIsolate(),"test").ToLocalChecked());
    if (v8::Script::Compile(getContext(), v8::String::NewFromUtf8(getIsolate(), "print(\"hello world\");").ToLocalChecked(), &scriptOrigin).ToLocal(&script)) {
        qCDebug(scriptengine_v8) << "Compile test successful";
    } else {
        qCDebug(scriptengine_v8) << "Compile test failed";
        Q_ASSERT(false);
    }
}

QString ScriptEngineV8::scriptValueDebugDetails(const ScriptValue &value) {
    V8ScriptValue v8Value = ScriptValueV8Wrapper::fullUnwrap(this, value);
    return scriptValueDebugDetailsV8(v8Value);
}

QString ScriptEngineV8::scriptValueDebugListMembers(const ScriptValue &value) {
    V8ScriptValue v8Value = ScriptValueV8Wrapper::fullUnwrap(this, value);
    return scriptValueDebugDetailsV8(v8Value);
}

QString ScriptEngineV8::scriptValueDebugListMembersV8(const V8ScriptValue &v8Value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());

    QString membersString("");
    if (v8Value.constGet()->IsObject()) {
        v8::Local<v8::String> membersStringV8;
        v8::Local<v8::Object> object = v8::Local<v8::Object>::Cast(v8Value.constGet());
        auto names = object->GetPropertyNames(getContext()).ToLocalChecked();
        if (v8::JSON::Stringify(getContext(), names).ToLocal(&membersStringV8)) {
            membersString = QString(*v8::String::Utf8Value(_v8Isolate, membersStringV8));
        }
        membersString = QString(*v8::String::Utf8Value(_v8Isolate, membersStringV8));
    } else {
        membersString = QString(" Is not an object");
    }
    return membersString;
}

QString ScriptEngineV8::scriptValueDebugDetailsV8(const V8ScriptValue &v8Value) {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    v8::HandleScope handleScope(_v8Isolate);
    v8::Context::Scope contextScope(getContext());

    QString parentValueQString("");
    v8::Local<v8::String> parentValueString;
    if (v8Value.constGet()->ToDetailString(getContext()).ToLocal(&parentValueString)) {
        parentValueQString = QString(*v8::String::Utf8Value(_v8Isolate, parentValueString));
    }
    QString JSONQString;
    v8::Local<v8::String> JSONString;
    if (v8::JSON::Stringify(getContext(), v8Value.constGet()).ToLocal(&JSONString)) {
        JSONQString = QString(*v8::String::Utf8Value(_v8Isolate, JSONString));
    }
    return parentValueQString + QString(" JSON: ") + JSONQString;
}

/*QStringList ScriptEngineV8::getCurrentStackTrace() {
    v8::Local<v8::StackTrace> stackTrace = v8::StackTrace::CurrentStackTrace(_v8Isolate, 100);
    QStringList backtrace;
    for (int n = 0; n < stackTrace->GetFrameCount(); n++) {
        v8::Local<v8::StackFrame> stackFrame = stackTrace->GetFrame(_v8Isolate, n);

    }
}*/

void ScriptEngineV8::logBacktrace(const QString &title) {
    QStringList backtrace = currentContext()->backtrace();
    qCDebug(scriptengine_v8) << title;
    for (int n = 0; n < backtrace.length(); n++) {
        qCDebug(scriptengine_v8) << backtrace[n];
    }
}

QStringList ScriptEngineV8::getCurrentScriptURLs() const {
    auto isolate = _v8Isolate;
    v8::Locker locker(isolate);
    v8::Isolate::Scope isolateScope(isolate);
    v8::HandleScope handleScope(isolate);
    v8::Context::Scope contextScope(_v8Isolate->GetCurrentContext());
    v8::Local<v8::StackTrace> stackTrace = v8::StackTrace::CurrentStackTrace(isolate, 100);
    QStringList scriptURLs;
    //V8TODO nicer formatting
    for (int i = 0; i < stackTrace->GetFrameCount(); i++) {
        v8::Local<v8::StackFrame> stackFrame = stackTrace->GetFrame(isolate, i);
        scriptURLs.append(QString(*v8::String::Utf8Value(isolate, stackFrame->GetScriptNameOrSourceURL())));
    }
    return scriptURLs;
}

ScriptEngineMemoryStatistics ScriptEngineV8::getMemoryUsageStatistics() {
    v8::Locker locker(_v8Isolate);
    v8::Isolate::Scope isolateScope(_v8Isolate);
    ScriptEngineMemoryStatistics statistics;
    v8::HeapStatistics heapStatistics;
    _v8Isolate->GetHeapStatistics(&heapStatistics);
    statistics.totalHeapSize = heapStatistics.total_available_size();
    statistics.usedHeapSize = heapStatistics.used_heap_size();
    statistics.totalAvailableSize = heapStatistics.total_available_size();
    statistics.totalGlobalHandlesSize = heapStatistics.total_global_handles_size();
    statistics.usedGlobalHandlesSize = heapStatistics.used_global_handles_size();
#ifdef OVERTE_V8_MEMORY_DEBUG
    statistics.scriptValueCount = scriptValueCount;
    statistics.scriptValueProxyCount = scriptValueProxyCount;
    statistics.qObjectCount = _qobjectWrapperMapV8.size();
#endif
    return statistics;
}

void ScriptEngineV8::startCollectingObjectStatistics() {
    auto heapProfiler = _v8Isolate->GetHeapProfiler();
    heapProfiler->StartTrackingHeapObjects();
}

void ScriptEngineV8::dumpHeapObjectStatistics() {
    // V8TODO: this is not very elegant, but very convenient
    QFile dumpFile("/tmp/heap_objectStatistics_dump.csv");
    if (!dumpFile.open(QFile::WriteOnly | QFile::Truncate)) {
        return;
    }
    QTextStream dump(&dumpFile);
    size_t objectTypeCount = _v8Isolate->NumberOfTrackedHeapObjectTypes();
    for (size_t i = 0; i < objectTypeCount; i++) {
        v8::HeapObjectStatistics statistics;
        if (_v8Isolate->GetHeapObjectStatisticsAtLastGC(&statistics, i)) {
            dump << statistics.object_type() << " " << statistics.object_sub_type() << " " << statistics.object_count() << " "
                 << statistics.object_size() << "\n";
        }
    }
}
