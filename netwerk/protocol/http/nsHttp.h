/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHttp_h__
#define nsHttp_h__

#include <stdint.h>
#include "prtime.h"
#include "nsAutoPtr.h"
#include "nsString.h"
#include "nsError.h"
#include "nsTArray.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtr.h"

class nsICacheEntry;

namespace mozilla {

class Mutex;

namespace net {
    class nsHttpResponseHead;
    class nsHttpRequestHead;
    class CacheControlParser;

    enum class HttpVersion {
        UNKNOWN = 0,
        v0_9 = 9,
        v1_0 = 10,
        v1_1 = 11,
        v2_0 = 20
    };

    enum class SpdyVersion {
        NONE = 0,
        // SPDY_VERSION_2 = 2, REMOVED
        // SPDY_VERSION_3 = 3, REMOVED
        // SPDY_VERSION_31 = 4, REMOVED
        HTTP_2 = 5

        // leave room for official versions. telem goes to 48
        // 24 was a internal spdy/3.1
        // 25 was spdy/4a2
        // 26 was http/2-draft08 and http/2-draft07 (they were the same)
        // 27 was http/2-draft09, h2-10, and h2-11
        // 28 was http/2-draft12
        // 29 was http/2-draft13
        // 30 was h2-14 and h2-15
        // 31 was h2-16
    };

//-----------------------------------------------------------------------------
// http connection capabilities
//-----------------------------------------------------------------------------

#define NS_HTTP_ALLOW_KEEPALIVE      (1<<0)
#define NS_HTTP_LARGE_KEEPALIVE      (1<<1)

// a transaction with this caps flag will continue to own the connection,
// preventing it from being reclaimed, even after the transaction completes.
#define NS_HTTP_STICKY_CONNECTION    (1<<2)

// a transaction with this caps flag will, upon opening a new connection,
// bypass the local DNS cache
#define NS_HTTP_REFRESH_DNS          (1<<3)

// a transaction with this caps flag will not pass SSL client-certificates
// to the server (see bug #466080), but is may also be used for other things
#define NS_HTTP_LOAD_ANONYMOUS       (1<<4)

// a transaction with this caps flag keeps timing information
#define NS_HTTP_TIMING_ENABLED       (1<<5)

// a transaction with this flag blocks the initiation of other transactons
// in the same load group until it is complete
#define NS_HTTP_LOAD_AS_BLOCKING     (1<<6)

// Disallow the use of the SPDY protocol. This is meant for the contexts
// such as HTTP upgrade which are nonsensical for SPDY, it is not the
// SPDY configuration variable.
#define NS_HTTP_DISALLOW_SPDY        (1<<7)

// a transaction with this flag loads without respect to whether the load
// group is currently blocking on some resources
#define NS_HTTP_LOAD_UNBLOCKED       (1<<8)

// This flag indicates the transaction should accept associated pushes
#define NS_HTTP_ONPUSH_LISTENER      (1<<9)

// Transactions with this flag should react to errors without side effects
// First user is to prevent clearing of alt-svc cache on failed probe
#define NS_HTTP_ERROR_SOFTLY         (1<<10)

// This corresponds to nsIHttpChannelInternal.beConservative
// it disables any cutting edge features that we are worried might result in
// interop problems with critical infrastructure
#define NS_HTTP_BE_CONSERVATIVE      (1<<11)

// Transactions with this flag should be processed first.
#define NS_HTTP_URGENT_START         (1<<12)

// A sticky connection of the transaction is explicitly allowed to be restarted
// on ERROR_NET_RESET.
#define NS_HTTP_CONNECTION_RESTARTABLE  (1<<13)

// Disallow name resolutions for this transaction to use TRR - primarily
// for use with TRR implementations themselves
#define NS_HTTP_DISABLE_TRR (1<<14)

// Allow re-using a spdy/http2 connection with NS_HTTP_ALLOW_KEEPALIVE not set.
// This is primarily used to allow connection sharing for websockets over http/2
// without accidentally allowing it for websockets not over http/2
#define NS_HTTP_ALLOW_SPDY_WITHOUT_KEEPALIVE (1<<15)

// Only permit CONNECTing to a proxy. A channel with this flag will not send an
// http request after CONNECT or setup tls. An http upgrade handler MUST be
// set. An ALPN header is set using the upgrade protocol.
#define NS_HTTP_CONNECT_ONLY            (1<<16)

//-----------------------------------------------------------------------------
// some default values
//-----------------------------------------------------------------------------

#define NS_HTTP_DEFAULT_PORT  80
#define NS_HTTPS_DEFAULT_PORT 443

#define NS_HTTP_HEADER_SEPS ", \t"

//-----------------------------------------------------------------------------
// http atoms...
//-----------------------------------------------------------------------------

struct nsHttpAtom
{
    operator const char *() const { return _val; }
    const char *get() const { return _val; }

    void operator=(const char *v) { _val = v; }
    void operator=(const nsHttpAtom &a) { _val = a._val; }

    // private
    const char *_val;
};

namespace nsHttp
{
    MOZ_MUST_USE nsresult CreateAtomTable();
    void DestroyAtomTable();

    // The mutex is valid any time the Atom Table is valid
    // This mutex is used in the unusual case that the network thread and
    // main thread might access the same data
    Mutex *GetLock();

    // will dynamically add atoms to the table if they don't already exist
    nsHttpAtom ResolveAtom(const char *);
    inline nsHttpAtom ResolveAtom(const nsACString &s)
    {
        return ResolveAtom(PromiseFlatCString(s).get());
    }

    // returns true if the specified token [start,end) is valid per RFC 2616
    // section 2.2
    bool IsValidToken(const char *start, const char *end);

    inline bool IsValidToken(const nsACString &s) {
        return IsValidToken(s.BeginReading(), s.EndReading());
    }

    // Strip the leading or trailing HTTP whitespace per fetch spec section 2.2.
    void TrimHTTPWhitespace(const nsACString& aSource,
                                   nsACString& aDest);

    // Returns true if the specified value is reasonable given the defintion
    // in RFC 2616 section 4.2.  Full strict validation is not performed
    // currently as it would require full parsing of the value.
    bool IsReasonableHeaderValue(const nsACString &s);

    // find the first instance (case-insensitive comparison) of the given
    // |token| in the |input| string.  the |token| is bounded by elements of
    // |separators| and may appear at the beginning or end of the |input|
    // string.  null is returned if the |token| is not found.  |input| may be
    // null, in which case null is returned.
    const char *FindToken(const char *input, const char *token,
                                 const char *separators);

    // This function parses a string containing a decimal-valued, non-negative
    // 64-bit integer.  If the value would exceed INT64_MAX, then false is
    // returned.  Otherwise, this function returns true and stores the
    // parsed value in |result|.  The next unparsed character in |input| is
    // optionally returned via |next| if |next| is non-null.
    //
    // TODO(darin): Replace this with something generic.
    //
    MOZ_MUST_USE bool ParseInt64(const char *input, const char **next,
                                        int64_t *result);

    // Variant on ParseInt64 that expects the input string to contain nothing
    // more than the value being parsed.
    inline MOZ_MUST_USE bool ParseInt64(const char *input,
                                               int64_t *result) {
        const char *next;
        return ParseInt64(input, &next, result) && *next == '\0';
    }

    // Return whether the HTTP status code represents a permanent redirect
    bool IsPermanentRedirect(uint32_t httpStatus);

    // Returns the APLN token which represents the used protocol version.
    const char* GetProtocolVersion(HttpVersion pv);

    bool ValidationRequired(bool isForcedValid, nsHttpResponseHead *cachedResponseHead,
                   uint32_t loadFlags, bool allowStaleCacheContent,
                   bool isImmutable, bool customConditionalRequest,
                   nsHttpRequestHead &requestHead,
                   nsICacheEntry *entry, CacheControlParser &cacheControlRequest,
                   bool fromPreviousSession);

    nsresult GetHttpResponseHeadFromCacheEntry(nsICacheEntry *entry,
                                               nsHttpResponseHead *cachedResponseHead);

    nsresult CheckPartial(nsICacheEntry* aEntry, int64_t *aSize,
                          int64_t *aContentLength,
                          nsHttpResponseHead *responseHead);

    void DetermineFramingAndImmutability(nsICacheEntry *entry, nsHttpResponseHead *cachedResponseHead,
                                         bool isHttps, bool *weaklyFramed,
                                         bool *isImmutable);

    // Called when an optimization feature affecting active vs background tab load
    // took place.  Called only on the parent process and only updates
    // mLastActiveTabLoadOptimizationHit timestamp to now.
    void NotifyActiveTabLoadOptimization();
    TimeStamp const GetLastActiveTabLoadOptimizationHit();
    void SetLastActiveTabLoadOptimizationHit(TimeStamp const &when);
    bool IsBeforeLastActiveTabLoadOptimization(TimeStamp const &when);

    HttpVersion GetHttpVersionFromSpdy(SpdyVersion sv);

    // Declare all atoms
    //
    // The atom names and values are stored in nsHttpAtomList.h and are brought
    // to you by the magic of C preprocessing.  Add new atoms to nsHttpAtomList
    // and all support logic will be auto-generated.
    //
#define HTTP_ATOM(_name, _value) extern nsHttpAtom _name;
#include "nsHttpAtomList.h"
#undef HTTP_ATOM
}

//-----------------------------------------------------------------------------
// utilities...
//-----------------------------------------------------------------------------

static inline uint32_t
PRTimeToSeconds(PRTime t_usec)
{
    return uint32_t( t_usec / PR_USEC_PER_SEC );
}

#define NowInSeconds() PRTimeToSeconds(PR_Now())

// Round q-value to 2 decimal places; return 2 most significant digits as uint.
#define QVAL_TO_UINT(q) ((unsigned int) ((q + 0.005) * 100.0))

#define HTTP_LWS " \t"
#define HTTP_HEADER_VALUE_SEPS HTTP_LWS ","

void EnsureBuffer(UniquePtr<char[]> &buf, uint32_t newSize,
                  uint32_t preserve, uint32_t &objSize);
void EnsureBuffer(UniquePtr<uint8_t[]> &buf, uint32_t newSize,
                  uint32_t preserve, uint32_t &objSize);

// h2=":443"; ma=60; single
// results in 3 mValues = {{h2, :443}, {ma, 60}, {single}}

class ParsedHeaderPair
{
public:
    ParsedHeaderPair(const char *name, int32_t nameLen,
                     const char *val, int32_t valLen, bool isQuotedValue);

    ParsedHeaderPair(ParsedHeaderPair const &copy)
        : mName(copy.mName)
        , mValue(copy.mValue)
        , mUnquotedValue(copy.mUnquotedValue)
        , mIsQuotedValue(copy.mIsQuotedValue)
    {
        if (mIsQuotedValue) {
            mValue.Rebind(mUnquotedValue.BeginReading(), mUnquotedValue.Length());
        }
    }

    nsDependentCSubstring mName;
    nsDependentCSubstring mValue;

private:
    nsCString mUnquotedValue;
    bool mIsQuotedValue;

    void RemoveQuotedStringEscapes(const char *val, int32_t valLen);
};

class ParsedHeaderValueList
{
public:
    ParsedHeaderValueList(const char *t, uint32_t len, bool allowInvalidValue);
    nsTArray<ParsedHeaderPair> mValues;

private:
    void ParseNameAndValue(const char *input, bool allowInvalidValue);
};

class ParsedHeaderValueListList
{
public:
    // RFC 7231 section 3.2.6 defines the syntax of the header field values.
    // |allowInvalidValue| indicates whether the rule will be used to check
    // the input text.
    // Note that ParsedHeaderValueListList is currently used to parse
    // Alt-Svc and Server-Timing header. |allowInvalidValue| is set to true
    // when parsing Alt-Svc for historical reasons.
    explicit ParsedHeaderValueListList(const nsCString &txt,
                                       bool allowInvalidValue = true);
    nsTArray<ParsedHeaderValueList> mValues;

private:
    nsCString mFull;
};

} // namespace net
} // namespace mozilla

#endif // nsHttp_h__
