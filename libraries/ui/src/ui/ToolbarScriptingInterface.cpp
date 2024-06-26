//
//  Created by Bradley Austin Davis on 2016-06-16
//  Copyright 2013-2016 High Fidelity, Inc.
//  Copyright 2022-2023 Overte e.V.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//  SPDX-License-Identifier: Apache-2.0
//

#include "ToolbarScriptingInterface.h"

#include <QtCore/QThread>
#include <QtQuick/QQuickItem>
#include <ScriptValue.h>
#include <ScriptEngine.h>
#include <ScriptEngineCast.h>
#include <ScriptManager.h>

#include <shared/QtHelpers.h>
#include "../OffscreenUi.h"

STATIC_SCRIPT_TYPES_INITIALIZER((+[](ScriptManager* manager){
    auto scriptEngine = manager->engine().get();

    scriptRegisterMetaType<ToolbarProxy*, wrapperToScriptValue<ToolbarProxy>, wrapperFromScriptValue<ToolbarProxy> >(scriptEngine);
    scriptRegisterMetaType<ToolbarButtonProxy*, wrapperToScriptValue<ToolbarButtonProxy>, wrapperFromScriptValue<ToolbarButtonProxy> >(scriptEngine);
}));

ScriptValue toolbarToScriptValue(ScriptEngine* engine, ToolbarProxy* const &in) {
    if (!in) {
        return engine->undefinedValue();
    }
    return engine->newQObject(in, ScriptEngine::QtOwnership);
}

void toolbarFromScriptValue(const ScriptValue& value, ToolbarProxy* &out) {
    out = qobject_cast<ToolbarProxy*>(value.toQObject());
    Q_ASSERT(out != nullptr);
}

ScriptValue toolbarButtonToScriptValue(ScriptEngine* engine, ToolbarButtonProxy* const &in) {
    if (!in) {
        return engine->undefinedValue();
    }
    return engine->newQObject(in, ScriptEngine::QtOwnership);
}

void toolbarButtonFromScriptValue(const ScriptValue& value, ToolbarButtonProxy* &out) {
    out = qobject_cast<ToolbarButtonProxy*>(value.toQObject());
    Q_ASSERT(out != nullptr);
}


ToolbarButtonProxy::ToolbarButtonProxy(QObject* qmlObject, QObject* parent) : QmlWrapper(qmlObject, parent) {
    Q_ASSERT(QThread::currentThread() == qApp->thread());
    _qmlButton = qobject_cast<QQuickItem*>(qmlObject);
    connect(qmlObject, SIGNAL(clicked()), this, SIGNAL(clicked()));
}

void ToolbarButtonProxy::editProperties(const QVariantMap& properties) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "editProperties", Q_ARG(QVariantMap, properties));
        return;
    }

    QVariantMap::const_iterator iter = properties.constBegin();
    while (iter != properties.constEnd()) {
        _properties[iter.key()] = iter.value();
        if (_qmlButton) {
            // [01/25 14:26:20] [WARNING] [default] QMetaObject::invokeMethod: No such method ToolbarButton_QMLTYPE_195::changeProperty(QVariant,QVariant)
            QMetaObject::invokeMethod(_qmlButton, "changeProperty", Qt::AutoConnection,
                                        Q_ARG(QVariant, QVariant(iter.key())), Q_ARG(QVariant, iter.value()));
        }
        ++iter;
    }
}

ToolbarProxy::ToolbarProxy(QObject* qmlObject, QObject* parent) : QmlWrapper(qmlObject, parent) { 
    Q_ASSERT(QThread::currentThread() == qApp->thread());
}

ToolbarButtonProxy* ToolbarProxy::addButton(const QVariant& properties) {
    if (QThread::currentThread() != thread()) {
        ToolbarButtonProxy* result = nullptr;
        BLOCKING_INVOKE_METHOD(this, "addButton", Q_RETURN_ARG(ToolbarButtonProxy*, result), Q_ARG(QVariant, properties));
        return result;
    }

    QVariant resultVar;
    bool invokeResult = QMetaObject::invokeMethod(_qmlObject, "addButton", Q_RETURN_ARG(QVariant, resultVar), Q_ARG(QVariant, properties));
    if (!invokeResult) {
        return nullptr;
    }

    QObject* rawButton = qvariant_cast<QObject *>(resultVar);
    if (!rawButton) {
        return nullptr;
    }

    return new ToolbarButtonProxy(rawButton, this);
}

void ToolbarProxy::removeButton(const QVariant& name) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, "removeButton", Q_ARG(QVariant, name));
        return;
    }

    QMetaObject::invokeMethod(_qmlObject, "removeButton", Q_ARG(QVariant, name));
}


ToolbarProxy* ToolbarScriptingInterface::getToolbar(const QString& toolbarId) {
    if (QThread::currentThread() != thread()) {
        ToolbarProxy* result = nullptr;
        BLOCKING_INVOKE_METHOD(this, "getToolbar", Q_RETURN_ARG(ToolbarProxy*, result), Q_ARG(QString, toolbarId));
        return result;
    }

    auto offscreenUi = DependencyManager::get<OffscreenUi>();
    auto desktop = offscreenUi->getDesktop();
    QVariant resultVar;
    bool invokeResult = QMetaObject::invokeMethod(desktop, "getToolbar", Q_RETURN_ARG(QVariant, resultVar), Q_ARG(QVariant, toolbarId));
    if (!invokeResult) {
        return nullptr;
    }

    QObject* rawToolbar = qvariant_cast<QObject *>(resultVar);
    if (!rawToolbar) {
        return nullptr;
    }

    return new ToolbarProxy(rawToolbar);
}
