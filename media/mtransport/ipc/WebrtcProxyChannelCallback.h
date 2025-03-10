/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 ts=8 et tw=80 ft=cpp : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef webrtc_proxy_channel_callback_h__
#define webrtc_proxy_channel_callback_h__

#include "nsTArray.h"

namespace mozilla {
namespace net {

class WebrtcProxyChannelCallback
{
public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  virtual void OnClose(nsresult aReason) = 0;
  virtual void OnConnected() = 0;
  virtual void OnRead(nsTArray<uint8_t>&& aReadData) = 0;

protected:
  virtual ~WebrtcProxyChannelCallback() = default;
};

} // namespace net
} // namespace mozilla

#endif // webrtc_proxy_channel_callback_h__
