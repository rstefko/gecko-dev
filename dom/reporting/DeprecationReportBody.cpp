/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/DeprecationReportBody.h"
#include "mozilla/dom/ReportingBinding.h"

namespace mozilla {
namespace dom {

DeprecationReportBody::DeprecationReportBody(nsPIDOMWindowInner* aWindow,
                                             const nsAString& aId,
                                             const Nullable<Date>& aDate,
                                             const nsAString& aMessage,
                                             const nsAString& aSourceFile,
                                             const Nullable<uint32_t>& aLineNumber,
                                             const Nullable<uint32_t>& aColumnNumber)
  : ReportBody(aWindow)
  , mId(aId)
  , mDate(aDate)
  , mMessage(aMessage)
  , mSourceFile(aSourceFile)
  , mLineNumber(aLineNumber)
  , mColumnNumber(aColumnNumber)
{
  MOZ_ASSERT(aWindow);
}

DeprecationReportBody::~DeprecationReportBody() = default;

JSObject*
DeprecationReportBody::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return DeprecationReportBody_Binding::Wrap(aCx, this, aGivenProto);
}

void
DeprecationReportBody::GetId(nsAString& aId) const
{
  aId = mId;
}

Nullable<Date>
DeprecationReportBody::GetAnticipatedRemoval() const
{
  return mDate;
}

void
DeprecationReportBody::GetMessage(nsAString& aMessage) const
{
  aMessage = mMessage;
}

void
DeprecationReportBody::GetSourceFile(nsAString& aSourceFile) const
{
  aSourceFile = mSourceFile;
}

Nullable<uint32_t>
DeprecationReportBody::GetLineNumber() const
{
  return mLineNumber;
}

Nullable<uint32_t>
DeprecationReportBody::GetColumnNumber() const
{
  return mColumnNumber;
}

} // dom namespace
} // mozilla namespace
