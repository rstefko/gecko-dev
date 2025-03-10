/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_DeprecationReportBody_h
#define mozilla_dom_DeprecationReportBody_h

#include "mozilla/dom/ReportBody.h"
#include "mozilla/dom/Date.h"

namespace mozilla {
namespace dom {

class DeprecationReportBody final : public ReportBody
{
public:
  DeprecationReportBody(nsPIDOMWindowInner* aWindow,
                        const nsAString& aId,
                        const Nullable<Date>& aDate,
                        const nsAString& aMessage,
                        const nsAString& aSourceFile,
                        const Nullable<uint32_t>& aLineNumber,
                        const Nullable<uint32_t>& aColumnNumber);

  JSObject*
  WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  void
  GetId(nsAString& aId) const;

  Nullable<Date>
  GetAnticipatedRemoval() const;

  void
  GetMessage(nsAString& aMessage) const;

  void
  GetSourceFile(nsAString& aSourceFile) const;

  Nullable<uint32_t>
  GetLineNumber() const;

  Nullable<uint32_t>
  GetColumnNumber() const;

private:
  ~DeprecationReportBody();

  const nsString mId;
  const Nullable<Date> mDate;
  const nsString mMessage;
  const nsString mSourceFile;
  const Nullable<uint32_t> mLineNumber;
  const Nullable<uint32_t> mColumnNumber;
};

} // dom namespace
} // mozilla namespace

#endif // mozilla_dom_DeprecationReportBody_h
