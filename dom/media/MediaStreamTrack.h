/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MEDIASTREAMTRACK_H_
#define MEDIASTREAMTRACK_H_

#include "MediaTrackConstraints.h"
#include "PrincipalChangeObserver.h"
#include "StreamTracks.h"
#include "mozilla/CORSMode.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/dom/MediaStreamTrackBinding.h"
#include "mozilla/dom/MediaTrackSettingsBinding.h"
#include "mozilla/media/MediaUtils.h"
#include "mozilla/WeakPtr.h"
#include "nsError.h"
#include "nsID.h"
#include "nsIPrincipal.h"

namespace mozilla {

class DOMMediaStream;
class MediaEnginePhotoCallback;
class MediaInputPort;
class MediaStream;
class MediaStreamGraph;
class MediaStreamGraphImpl;
class MediaStreamTrackListener;
class DirectMediaStreamTrackListener;
class PeerConnectionImpl;
class PeerConnectionMedia;
class PeerIdentity;
class ProcessedMediaStream;
class RemoteSourceStreamInfo;
class SourceStreamInfo;

namespace dom {

class AudioStreamTrack;
class VideoStreamTrack;
class MediaStreamError;
class TrackSink;
enum class CallerType : uint32_t;

/**
 * Common interface through which a MediaStreamTrack can communicate with its
 * producer on the main thread.
 *
 * Kept alive by a strong ref in all MediaStreamTracks (original and clones)
 * sharing this source.
 */
class MediaStreamTrackSource : public nsISupports {
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(MediaStreamTrackSource)

 public:
  class Sink : public SupportsWeakPtr<Sink> {
   public:
    MOZ_DECLARE_WEAKREFERENCE_TYPENAME(MediaStreamTrackSource::Sink)

    /**
     * Must be constant throughout the Sink's lifetime.
     *
     * Return true to keep the MediaStreamTrackSource where this sink is
     * registered alive.
     * Return false to allow the source to stop.
     *
     * Typically MediaStreamTrack::Sink returns true and other Sinks
     * (like HTMLMediaElement::StreamCaptureTrackSource) return false.
     */
    virtual bool KeepsSourceAlive() const = 0;

    /**
     * Return true to ensure that the MediaStreamTrackSource where this Sink is
     * registered is kept turned on and active.
     * Return false to allow the source to pause, and any underlying devices to
     * temporarily stop.
     *
     * When the underlying enabled state of the sink changes,
     * call MediaStreamTrackSource::SinkEnabledStateChanged().
     *
     * Typically MediaStreamTrack returns the track's enabled state and other
     * Sinks (like HTMLMediaElement::StreamCaptureTrackSource) return false so
     * control over device state remains with tracks and their enabled state.
     */
    virtual bool Enabled() const = 0;

    /**
     * Called when the principal of the MediaStreamTrackSource where this sink
     * is registered has changed.
     */
    virtual void PrincipalChanged() = 0;

    /**
     * Called when the muted state of the MediaStreamTrackSource where this sink
     * is registered has changed.
     */
    virtual void MutedChanged(bool aNewState) = 0;

   protected:
    virtual ~Sink() = default;
  };

  MediaStreamTrackSource(nsIPrincipal* aPrincipal, const nsString& aLabel)
      : mPrincipal(aPrincipal), mLabel(aLabel), mStopped(false) {}

  /**
   * Use to clean up any resources that have to be cleaned before the
   * destructor is called. It is often too late in the destructor because
   * of garbage collection having removed the members already.
   */
  virtual void Destroy() {}

  /**
   * Gets the source's MediaSourceEnum for usage by PeerConnections.
   */
  virtual MediaSourceEnum GetMediaSource() const = 0;

  /**
   * Get this TrackSource's principal.
   */
  nsIPrincipal* GetPrincipal() const { return mPrincipal; }

  /**
   * Get the source's current CORSMode. If not applicable CORS_NONE is returned.
   * The sink will be notified of changes to our CORSMode through
   * PrincipalChanged().
   */
  virtual CORSMode GetCORSMode() const { return CORS_NONE; }

  /**
   * This is used in WebRTC. A peerIdentity constrained MediaStreamTrack cannot
   * be sent across the network to anything other than a peer with the provided
   * identity. If this is set, then GetPrincipal() should return an instance of
   * NullPrincipal.
   *
   * A track's PeerIdentity is immutable and will not change during the track's
   * lifetime.
   */
  virtual const PeerIdentity* GetPeerIdentity() const { return nullptr; }

  /**
   * MediaStreamTrack::GetLabel (see spec) calls through to here.
   */
  void GetLabel(nsAString& aLabel) { aLabel.Assign(mLabel); }

  /**
   * Forwards a photo request to backends that support it. Other backends return
   * NS_ERROR_NOT_IMPLEMENTED to indicate that a MediaStreamGraph-based fallback
   * should be used.
   */
  virtual nsresult TakePhoto(MediaEnginePhotoCallback*) const {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  typedef media::Pledge<bool, dom::MediaStreamError*> PledgeVoid;

  /**
   * We provide a fallback solution to ApplyConstraints() here.
   * Sources that support ApplyConstraints() will have to override it.
   */
  virtual already_AddRefed<PledgeVoid> ApplyConstraints(
      nsPIDOMWindowInner* aWindow,
      const dom::MediaTrackConstraints& aConstraints, CallerType aCallerType);

  /**
   * Same for GetSettings (no-op).
   */
  virtual void GetSettings(dom::MediaTrackSettings& aResult){};

  /**
   * Called by the source interface when all registered sinks with
   * KeepsSourceAlive() == true have unregistered.
   */
  virtual void Stop() = 0;

  /**
   * Called by the source interface when all registered sinks with
   * KeepsSourceAlive() == true become disabled.
   */
  virtual void Disable() = 0;

  /**
   * Called by the source interface when at least one registered sink with
   * KeepsSourceAlive() == true become enabled.
   */
  virtual void Enable() = 0;

  /**
   * Called when a Sink's Enabled() state changed. Will iterate through all
   * sinks and notify the source of the aggregated enabled state.
   *
   * Note that a Sink with KeepsSourceAlive() == false counts as disabled.
   */
  void SinkEnabledStateChanged() {
    if (IsEnabled()) {
      Enable();
    } else {
      Disable();
    }
  }

  /**
   * Called by each MediaStreamTrack clone on initialization.
   */
  void RegisterSink(Sink* aSink) {
    MOZ_ASSERT(NS_IsMainThread());
    if (mStopped) {
      return;
    }
    mSinks.AppendElement(aSink);
    while (mSinks.RemoveElement(nullptr)) {
      MOZ_ASSERT_UNREACHABLE("Sink was not explicitly removed");
    }
  }

  /**
   * Called by each MediaStreamTrack clone on Stop() if supported by the
   * source (us) or destruction.
   */
  void UnregisterSink(Sink* aSink) {
    MOZ_ASSERT(NS_IsMainThread());
    while (mSinks.RemoveElement(nullptr)) {
      MOZ_ASSERT_UNREACHABLE("Sink was not explicitly removed");
    }
    if (mSinks.RemoveElement(aSink) && !IsActive()) {
      MOZ_ASSERT(!aSink->KeepsSourceAlive() || !mStopped,
                 "When the last sink keeping the source alive is removed, "
                 "we should still be live");
      Stop();
      mStopped = true;
    }
    if (!mStopped) {
      SinkEnabledStateChanged();
    }
  }

 protected:
  virtual ~MediaStreamTrackSource() {}

  bool IsActive() {
    for (const WeakPtr<Sink>& sink : mSinks) {
      if (sink && sink->KeepsSourceAlive()) {
        return true;
      }
    }
    return false;
  }

  bool IsEnabled() {
    for (const WeakPtr<Sink>& sink : mSinks) {
      if (sink && sink->KeepsSourceAlive() && sink->Enabled()) {
        return true;
      }
    }
    return false;
  }

  /**
   * Called by a sub class when the principal has changed.
   * Notifies all sinks.
   */
  void PrincipalChanged() {
    MOZ_ASSERT(NS_IsMainThread());
    nsTArray<WeakPtr<Sink>> sinks(mSinks);
    for (auto& sink : sinks) {
      if (!sink) {
        MOZ_ASSERT_UNREACHABLE("Sink was not explicitly removed");
        mSinks.RemoveElement(sink);
        continue;
      }
      sink->PrincipalChanged();
    }
  }

  /**
   * Called by a sub class when the source's muted state has changed. Note that
   * the source is responsible for making the content black/silent during mute.
   * Notifies all sinks.
   */
  void MutedChanged(bool aNewState) {
    MOZ_ASSERT(NS_IsMainThread());
    nsTArray<WeakPtr<Sink>> sinks(mSinks);
    for (auto& sink : sinks) {
      if (!sink) {
        MOZ_ASSERT_UNREACHABLE("Sink was not explicitly removed");
        mSinks.RemoveElement(sink);
        continue;
      }
      sink->MutedChanged(aNewState);
    }
  }

  // Principal identifying who may access the contents of this source.
  nsCOMPtr<nsIPrincipal> mPrincipal;

  // Currently registered sinks.
  nsTArray<WeakPtr<Sink>> mSinks;

  // The label of the track we are the source of per the MediaStreamTrack spec.
  const nsString mLabel;

  // True if all MediaStreamTrack users have unregistered from this source and
  // Stop() has been called.
  bool mStopped;
};

/**
 * Basic implementation of MediaStreamTrackSource that doesn't forward Stop().
 */
class BasicTrackSource : public MediaStreamTrackSource {
 public:
  explicit BasicTrackSource(
      nsIPrincipal* aPrincipal,
      const MediaSourceEnum aMediaSource = MediaSourceEnum::Other)
      : MediaStreamTrackSource(aPrincipal, nsString()),
        mMediaSource(aMediaSource) {}

  MediaSourceEnum GetMediaSource() const override { return mMediaSource; }

  void Stop() override {}
  void Disable() override {}
  void Enable() override {}

 protected:
  ~BasicTrackSource() {}

  const MediaSourceEnum mMediaSource;
};

/**
 * Base class that consumers of a MediaStreamTrack can use to get notifications
 * about state changes in the track.
 */
class MediaStreamTrackConsumer
    : public SupportsWeakPtr<MediaStreamTrackConsumer> {
 public:
  MOZ_DECLARE_WEAKREFERENCE_TYPENAME(MediaStreamTrackConsumer)

  /**
   * Called when the track's readyState transitions to "ended".
   * Unlike the "ended" event exposed to script this is called for any reason,
   * including MediaStreamTrack::Stop().
   */
  virtual void NotifyEnded(MediaStreamTrack* aTrack){};
};

/**
 * Class representing a track in a DOMMediaStream.
 */
class MediaStreamTrack : public DOMEventTargetHelper,
                         public SupportsWeakPtr<MediaStreamTrack> {
  // DOMMediaStream owns MediaStreamTrack instances, and requires access to
  // some internal state, e.g., GetInputStream(), GetOwnedStream().
  friend class mozilla::DOMMediaStream;

  // PeerConnection and friends need to know our owning DOMStream and track id.
  friend class mozilla::PeerConnectionImpl;
  friend class mozilla::PeerConnectionMedia;
  friend class mozilla::SourceStreamInfo;
  friend class mozilla::RemoteSourceStreamInfo;

  class MSGListener;

 public:
  /**
   * aTrackID is the MediaStreamGraph track ID for the track in the
   * MediaStream owned by aStream.
   */
  MediaStreamTrack(
      DOMMediaStream* aStream, TrackID aTrackID, TrackID aInputTrackID,
      MediaStreamTrackSource* aSource,
      const MediaTrackConstraints& aConstraints = MediaTrackConstraints());

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(MediaStreamTrack,
                                           DOMEventTargetHelper)

  MOZ_DECLARE_WEAKREFERENCE_TYPENAME(MediaStreamTrack)

  nsPIDOMWindowInner* GetParentObject() const;
  JSObject* WrapObject(JSContext* aCx,
                       JS::Handle<JSObject*> aGivenProto) override;

  virtual AudioStreamTrack* AsAudioStreamTrack() { return nullptr; }
  virtual VideoStreamTrack* AsVideoStreamTrack() { return nullptr; }

  virtual const AudioStreamTrack* AsAudioStreamTrack() const { return nullptr; }
  virtual const VideoStreamTrack* AsVideoStreamTrack() const { return nullptr; }

  // WebIDL
  virtual void GetKind(nsAString& aKind) = 0;
  void GetId(nsAString& aID) const;
  virtual void GetLabel(nsAString& aLabel, CallerType /* aCallerType */) {
    GetSource().GetLabel(aLabel);
  }
  bool Enabled() const { return mEnabled; }
  void SetEnabled(bool aEnabled);
  bool Muted() { return mMuted; }
  void Stop();
  void GetConstraints(dom::MediaTrackConstraints& aResult);
  void GetSettings(dom::MediaTrackSettings& aResult, CallerType aCallerType);

  already_AddRefed<Promise> ApplyConstraints(
      const dom::MediaTrackConstraints& aConstraints, CallerType aCallerType,
      ErrorResult& aRv);
  already_AddRefed<MediaStreamTrack> Clone();
  MediaStreamTrackState ReadyState() { return mReadyState; }

  IMPL_EVENT_HANDLER(mute)
  IMPL_EVENT_HANDLER(unmute)
  IMPL_EVENT_HANDLER(ended)

  /**
   * Convenience (and legacy) method for when ready state is "ended".
   */
  bool Ended() const { return mReadyState == MediaStreamTrackState::Ended; }

  /**
   * Forces the ready state to a particular value, for instance when we're
   * cloning an already ended track.
   */
  void SetReadyState(MediaStreamTrackState aState);

  /**
   * Notified by the MediaStreamGraph, through our owning MediaStream on the
   * main thread.
   *
   * Note that this sets the track to ended and raises the "ended" event
   * synchronously.
   */
  void OverrideEnded();

  /**
   * Get this track's principal.
   */
  nsIPrincipal* GetPrincipal() const { return mPrincipal; }

  /**
   * Called by the MSGListener when this track's PrincipalHandle changes on
   * the MediaStreamGraph thread. When the PrincipalHandle matches the pending
   * principal we know that the principal change has propagated to consumers.
   */
  void NotifyPrincipalHandleChanged(const PrincipalHandle& aPrincipalHandle);

  /**
   * Called when this track's readyState transitions to "ended".
   * Notifies all MediaStreamTrackConsumers that this track ended.
   */
  void NotifyEnded();

  /**
   * Get this track's CORS mode.
   */
  CORSMode GetCORSMode() const { return GetSource().GetCORSMode(); }

  /**
   * Get this track's PeerIdentity.
   */
  const PeerIdentity* GetPeerIdentity() const {
    return GetSource().GetPeerIdentity();
  }

  MediaStreamGraph* Graph();
  MediaStreamGraphImpl* GraphImpl();

  MediaStreamTrackSource& GetSource() const {
    MOZ_RELEASE_ASSERT(mSource,
                       "The track source is only removed on destruction");
    return *mSource;
  }

  // Webrtc allows the remote side to name tracks whatever it wants, and we
  // need to surface this to content.
  void AssignId(const nsAString& aID) { mID = aID; }

  /**
   * Called when mSource's principal has changed.
   */
  void PrincipalChanged();

  /**
   * Called when mSource's muted state has changed.
   */
  void MutedChanged(bool aNewState);

  /**
   * Add a PrincipalChangeObserver to this track.
   *
   * Returns true if it was successfully added.
   *
   * Ownership of the PrincipalChangeObserver remains with the caller, and it's
   * the caller's responsibility to remove the observer before it dies.
   */
  bool AddPrincipalChangeObserver(
      PrincipalChangeObserver<MediaStreamTrack>* aObserver);

  /**
   * Remove an added PrincipalChangeObserver from this track.
   *
   * Returns true if it was successfully removed.
   */
  bool RemovePrincipalChangeObserver(
      PrincipalChangeObserver<MediaStreamTrack>* aObserver);

  /**
   * Add a MediaStreamTrackConsumer to this track.
   *
   * Adding the same consumer multiple times is prohibited.
   */
  void AddConsumer(MediaStreamTrackConsumer* aConsumer);

  /**
   * Remove an added MediaStreamTrackConsumer from this track.
   */
  void RemoveConsumer(MediaStreamTrackConsumer* aConsumer);

  /**
   * Adds a MediaStreamTrackListener to the MediaStreamGraph representation of
   * this track.
   */
  virtual void AddListener(MediaStreamTrackListener* aListener);

  /**
   * Removes a MediaStreamTrackListener from the MediaStreamGraph representation
   * of this track.
   */
  void RemoveListener(MediaStreamTrackListener* aListener);

  /**
   * Attempts to add a direct track listener to this track.
   * Callers must listen to the NotifyInstalled event to know if installing
   * the listener succeeded (tracks originating from SourceMediaStreams) or
   * failed (e.g., WebAudio originated tracks).
   */
  virtual void AddDirectListener(DirectMediaStreamTrackListener* aListener);
  void RemoveDirectListener(DirectMediaStreamTrackListener* aListener);

  /**
   * Sets up a MediaInputPort from the underlying track that this
   * MediaStreamTrack represents, to aStream, and returns it.
   */
  already_AddRefed<MediaInputPort> ForwardTrackContentsTo(
      ProcessedMediaStream* aStream, TrackID aDestinationTrackID = TRACK_ANY);

  /**
   * Returns true if this track is connected to aPort and forwarded to aPort's
   * output stream.
   */
  bool IsForwardedThrough(MediaInputPort* aPort);

  void SetMediaStreamSizeListener(DirectMediaStreamTrackListener* aListener);

  // Returns the original DOMMediaStream's underlying input stream.
  MediaStream* GetInputStream();

  TrackID GetInputTrackId() const { return mInputTrackID; }

 protected:
  virtual ~MediaStreamTrack();

  /**
   * Sets this track's muted state without raising any events.
   */
  void SetMuted(bool aMuted) { mMuted = aMuted; }

  void Destroy();

  // Returns the owning DOMMediaStream's underlying owned stream.
  ProcessedMediaStream* GetOwnedStream();

  // Returns the original DOMMediaStream. If this track is a clone,
  // the original track's owning DOMMediaStream is returned.
  DOMMediaStream* GetInputDOMStream();

  /**
   * Sets the principal and notifies PrincipalChangeObservers if it changes.
   */
  void SetPrincipal(nsIPrincipal* aPrincipal);

  /**
   * Creates a new MediaStreamTrack with the same type, input track ID and
   * source as this MediaStreamTrack.
   * aTrackID is the TrackID the new track will have in its owned stream.
   */
  virtual already_AddRefed<MediaStreamTrack> CloneInternal(
      DOMMediaStream* aOwningStream, TrackID aTrackID) = 0;

  nsTArray<PrincipalChangeObserver<MediaStreamTrack>*>
      mPrincipalChangeObservers;

  nsTArray<WeakPtr<MediaStreamTrackConsumer>> mConsumers;

  RefPtr<DOMMediaStream> mOwningStream;
  TrackID mTrackID;
  TrackID mInputTrackID;
  RefPtr<MediaStreamTrackSource> mSource;
  const UniquePtr<TrackSink> mSink;
  RefPtr<MediaStreamTrack> mOriginalTrack;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<nsIPrincipal> mPendingPrincipal;
  RefPtr<MSGListener> mMSGListener;
  // Keep tracking MediaStreamTrackListener and DirectMediaStreamTrackListener,
  // so we can remove them in |Destory|.
  nsTArray<RefPtr<MediaStreamTrackListener>> mTrackListeners;
  nsTArray<RefPtr<DirectMediaStreamTrackListener>> mDirectTrackListeners;
  nsString mID;
  MediaStreamTrackState mReadyState;
  bool mEnabled;
  bool mMuted;
  dom::MediaTrackConstraints mConstraints;
};

}  // namespace dom
}  // namespace mozilla

#endif /* MEDIASTREAMTRACK_H_ */
