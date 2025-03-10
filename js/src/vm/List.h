/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_List_h
#define vm_List_h

#include "NamespaceImports.h"
#include "js/Value.h"
#include "vm/NativeObject.h"

namespace js {

/**
 * The List specification type, ECMA-262 6.2.1.
 * <https://tc39.github.io/ecma262/#sec-list-and-record-specification-type>
 *
 * Lists are simple mutable sequences of values. Many standards use them.
 * Abstractly, they're not objects; they don't have properties or prototypes;
 * they're for internal specification use only. ListObject is our most direct
 * implementation of a List: store the values in the slots of a JSObject.
 *
 * We often implement Lists in other ways. For example, builtin/Utilities.js
 * contains a completely unrelated List constructor that's used in self-hosted
 * code. And AsyncGeneratorObject optimizes away the ListObject in the common
 * case where its internal queue never holds more than one element.
 *
 * ListObjects must not be exposed to content scripts.
 */
class ListObject : public NativeObject {
  public:
    static const Class class_;

    inline static MOZ_MUST_USE ListObject* create(JSContext* cx);

    uint32_t length() const { return getDenseInitializedLength(); }

    const Value& get(uint32_t index) const { return getDenseElement(index); }

    template <class T>
    T& getAs(uint32_t index) const { return get(index).toObject().as<T>(); }

    /**
     * Add an element to the end of the list. Returns false on OOM.
     */
    inline MOZ_MUST_USE bool append(JSContext* cx, HandleValue value);

    /**
     * Remove and return the first element of the list.
     *
     * Precondition: This list is not empty.
     */
    inline JS::Value popFirst(JSContext* cx);

    /**
     * Remove and return the first element of the list.
     *
     * Precondition: This list is not empty, and the first element
     * is an object of class T.
     */
    template <class T>
    inline T& popFirstAs(JSContext* cx);
};

} // namespace js

#endif // vm_List_h
