/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set et cin ts=4 sw=4 sts=4: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHttpChannelAuthProvider_h__
#define nsHttpChannelAuthProvider_h__

#include "nsIHttpChannelAuthProvider.h"
#include "nsIAuthPromptCallback.h"
#include "nsIHttpAuthenticatorCallback.h"
#include "nsString.h"
#include "nsCOMPtr.h"
#include "nsHttpAuthCache.h"
#include "nsProxyInfo.h"
#include "nsCRT.h"
#include "nsICancelableRunnable.h"

class nsIHttpAuthenticableChannel;
class nsIHttpAuthenticator;
class nsIURI;

namespace mozilla { namespace net {

class nsHttpHandler;

class nsHttpChannelAuthProvider final
  : public nsIHttpChannelAuthProvider
  , public nsIAuthPromptCallback
  , public nsIHttpAuthenticatorCallback
{
public:
    NS_DECL_ISUPPORTS
    NS_DECL_NSICANCELABLE
    NS_DECL_NSIHTTPCHANNELAUTHPROVIDER
    NS_DECL_NSIAUTHPROMPTCALLBACK
    NS_DECL_NSIHTTPAUTHENTICATORCALLBACK

    nsHttpChannelAuthProvider();
private:
    virtual ~nsHttpChannelAuthProvider();

    const char *ProxyHost() const
    { return mProxyInfo ? mProxyInfo->Host().get() : nullptr; }

    int32_t     ProxyPort() const
    { return mProxyInfo ? mProxyInfo->Port() : -1; }

    const char *Host() const      { return mHost.get(); }
    int32_t     Port() const      { return mPort; }
    bool        UsingSSL() const  { return mUsingSSL; }

    bool        UsingHttpProxy() const
    { return mProxyInfo && (mProxyInfo->IsHTTP() || mProxyInfo->IsHTTPS()); }

    MOZ_MUST_USE nsresult PrepareForAuthentication(bool proxyAuth);
    MOZ_MUST_USE nsresult
    GenCredsAndSetEntry(nsIHttpAuthenticator *, bool proxyAuth,
                        const char *scheme, const char *host, int32_t port,
                        const char *dir, const char *realm,
                        const char *challenge, const nsHttpAuthIdentity &ident,
                        nsCOMPtr<nsISupports> &session, char **result);
    MOZ_MUST_USE nsresult GetAuthenticator(const char *challenge,
                                           nsCString &scheme,
                                           nsIHttpAuthenticator **auth);
    void     ParseRealm(const char *challenge, nsACString &realm);
    void     GetIdentityFromURI(uint32_t authFlags, nsHttpAuthIdentity&);

    /**
     * Following three methods return NS_ERROR_IN_PROGRESS when
     * nsIAuthPrompt2.asyncPromptAuth method is called. This result indicates
     * the user's decision will be gathered in a callback and is not an actual
     * error.
     */
    MOZ_MUST_USE nsresult GetCredentials(const char *challenges, bool proxyAuth,
                                         nsCString& creds);
    MOZ_MUST_USE nsresult
    GetCredentialsForChallenge(const char *challenge, const char *scheme,
                               bool proxyAuth, nsIHttpAuthenticator *auth,
                               nsCString& creds);
    MOZ_MUST_USE nsresult PromptForIdentity(uint32_t level, bool proxyAuth,
                                            const char *realm,
                                            const char *authType,
                                            uint32_t authFlags,
                                            nsHttpAuthIdentity &);

    bool     ConfirmAuth(const char* bundleKey, bool doYesNoPrompt);
    void     SetAuthorizationHeader(nsHttpAuthCache *, nsHttpAtom header,
                                    const char *scheme, const char *host,
                                    int32_t port, const char *path,
                                    nsHttpAuthIdentity &ident);
    MOZ_MUST_USE nsresult GetCurrentPath(nsACString &);
    /**
     * Return all information needed to build authorization information,
     * all parameters except proxyAuth are out parameters. proxyAuth specifies
     * with what authorization we work (WWW or proxy).
     */
    MOZ_MUST_USE nsresult
    GetAuthorizationMembers(bool proxyAuth, nsACString& scheme,
                            const char*& host, int32_t& port,
                            nsACString& path, nsHttpAuthIdentity*& ident,
                            nsISupports**& continuationState);
    /**
     * Method called to resume suspended transaction after we got credentials
     * from the user. Called from OnAuthAvailable callback or OnAuthCancelled
     * when credentials for next challenge were obtained synchronously.
     */
    MOZ_MUST_USE nsresult ContinueOnAuthAvailable(const nsACString& creds);

    MOZ_MUST_USE nsresult DoRedirectChannelToHttps();

    /**
     * A function that takes care of reading STS headers and enforcing STS
     * load rules.  After a secure channel is erected, STS requires the channel
     * to be trusted or any STS header data on the channel is ignored.
     * This is called from ProcessResponse.
     */
    MOZ_MUST_USE nsresult ProcessSTSHeader();

    // Depending on the pref setting, the authentication dialog may be blocked
    // for all sub-resources, blocked for cross-origin sub-resources, or
    // always allowed for sub-resources.
    // For more details look at the bug 647010.
    bool BlockPrompt(bool proxyAuth);

    // Store credentials to the cache when appropriate aFlags are set.
    MOZ_MUST_USE nsresult UpdateCache(nsIHttpAuthenticator *aAuth,
                                      const char           *aScheme,
                                      const char           *aHost,
                                      int32_t               aPort,
                                      const char           *aDirectory,
                                      const char           *aRealm,
                                      const char           *aChallenge,
                                      const nsHttpAuthIdentity &aIdent,
                                      const char           *aCreds,
                                      uint32_t              aGenerateFlags,
                                      nsISupports          *aSessionState);

private:
    nsIHttpAuthenticableChannel      *mAuthChannel;  // weak ref

    nsCOMPtr<nsIURI>                  mURI;
    nsCOMPtr<nsProxyInfo>             mProxyInfo;
    nsCString                         mHost;
    int32_t                           mPort;
    bool                              mUsingSSL;
    bool                              mProxyUsingSSL;
    bool                              mIsPrivate;

    nsISupports                      *mProxyAuthContinuationState;
    nsCString                         mProxyAuthType;
    nsISupports                      *mAuthContinuationState;
    nsCString                         mAuthType;
    nsHttpAuthIdentity                mIdent;
    nsHttpAuthIdentity                mProxyIdent;

    // Reference to the prompt waiting in prompt queue. The channel is
    // responsible to call its cancel method when user in any way cancels
    // this request.
    nsCOMPtr<nsICancelable>           mAsyncPromptAuthCancelable;
    // Saved in GetCredentials when prompt is asynchronous, the first challenge
    // we obtained from the server with 401/407 response, will be processed in
    // OnAuthAvailable callback.
    nsCString                         mCurrentChallenge;
    // Saved in GetCredentials when prompt is asynchronous, remaning challenges
    // we have to process when user cancels the auth dialog for the current
    // challenge.
    nsCString                         mRemainingChallenges;

    // True when we need to authenticate to proxy, i.e. when we get 407
    // response. Used in OnAuthAvailable and OnAuthCancelled callbacks.
    uint32_t                          mProxyAuth                : 1;
    uint32_t                          mTriedProxyAuth           : 1;
    uint32_t                          mTriedHostAuth            : 1;
    uint32_t                          mSuppressDefensiveAuth    : 1;

    // If a cross-origin sub-resource is being loaded, this flag will be set.
    // In that case, the prompt text will be different to warn users.
    uint32_t                          mCrossOrigin : 1;
    uint32_t                          mConnectionBased : 1;

    RefPtr<nsHttpHandler>           mHttpHandler;  // keep gHttpHandler alive

    nsCOMPtr<nsICancelable>           mGenerateCredentialsCancelable;
};

} // namespace net
} // namespace mozilla

#endif // nsHttpChannelAuthProvider_h__
