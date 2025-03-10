/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_GenericFlingAnimation_h_
#define mozilla_layers_GenericFlingAnimation_h_

#include "APZUtils.h"
#include "AsyncPanZoomAnimation.h"
#include "AsyncPanZoomController.h"
#include "FrameMetrics.h"
#include "Layers.h"
#include "Units.h"
#include "OverscrollHandoffState.h"
#include "gfxPrefs.h"
#include "mozilla/Assertions.h"
#include "mozilla/Monitor.h"
#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"
#include "nsThreadUtils.h"

#define FLING_LOG(...)
// #define FLING_LOG(...) printf_stderr("FLING: " __VA_ARGS__)

namespace mozilla {
namespace layers {

/**
 * The FlingPhysics template parameter determines the physics model
 * that the fling animation follows. It must have the following methods:
 *
 *   - Default constructor.
 *
 *   - Init(const ParentLayerPoint& aStartingVelocity, float aPLPPI).
 *     Called at the beginning of the fling, with the fling's starting velocity,
 *     and the number of ParentLayer pixels per (Screen) inch at the point of
 *     the fling's start in the fling's direction.
 *
 *   - Sample(const TimeDuration& aDelta,
 *            ParentLayerPoint* aOutVelocity,
 *            ParentLayerPoint* aOutOffset);
 *     Called on each sample of the fling.
 *     |aDelta| is the time elapsed since the last sample.
 *     |aOutVelocity| should be the desired velocity after the current sample,
 *                    in ParentLayer pixels per millisecond.
 *     |aOutOffset| should be the desired _delta_ to the scroll offset after
 *     the current sample. |aOutOffset| should _not_ be clamped to the APZC's
 *     scrollable bounds; the caller will do the clamping, and it needs to
 *     know the unclamped value to handle handoff/overscroll correctly.
 */
template <typename FlingPhysics>
class GenericFlingAnimation: public AsyncPanZoomAnimation, public FlingPhysics {
public:
  GenericFlingAnimation(AsyncPanZoomController& aApzc,
                        const RefPtr<const OverscrollHandoffChain>& aOverscrollHandoffChain,
                        bool aFlingIsHandedOff,
                        const RefPtr<const AsyncPanZoomController>& aScrolledApzc,
                        float aPLPPI)
    : mApzc(aApzc)
    , mOverscrollHandoffChain(aOverscrollHandoffChain)
    , mScrolledApzc(aScrolledApzc)
  {
    MOZ_ASSERT(mOverscrollHandoffChain);
    TimeStamp now = aApzc.GetFrameTime();

    // Drop any velocity on axes where we don't have room to scroll anyways
    // (in this APZC, or an APZC further in the handoff chain).
    // This ensures that we don't take the 'overscroll' path in Sample()
    // on account of one axis which can't scroll having a velocity.
    if (!mOverscrollHandoffChain->CanScrollInDirection(&mApzc, ScrollDirection::eHorizontal)) {
      RecursiveMutexAutoLock lock(mApzc.mRecursiveMutex);
      mApzc.mX.SetVelocity(0);
    }
    if (!mOverscrollHandoffChain->CanScrollInDirection(&mApzc, ScrollDirection::eVertical)) {
      RecursiveMutexAutoLock lock(mApzc.mRecursiveMutex);
      mApzc.mY.SetVelocity(0);
    }

    ParentLayerPoint velocity = mApzc.GetVelocityVector();

    // If the last fling was very recent and in the same direction as this one,
    // boost the velocity to be the sum of the two. Check separate axes separately
    // because we could have two vertical flings with small horizontal components
    // on the opposite side of zero, and we still want the y-fling to get accelerated.
    // Note that the acceleration code is only applied on the APZC that initiates
    // the fling; the accelerated velocities are then handed off using the
    // normal DispatchFling codepath.
    // Acceleration is only applied in the APZC that originated the fling,
    // not in APZCs further down the handoff chain during handoff.
    bool applyAcceleration = !aFlingIsHandedOff;
    if (applyAcceleration && !mApzc.mLastFlingTime.IsNull()
        && (now - mApzc.mLastFlingTime).ToMilliseconds() < gfxPrefs::APZFlingAccelInterval()
        && velocity.Length() >= gfxPrefs::APZFlingAccelMinVelocity()) {
      if (SameDirection(velocity.x, mApzc.mLastFlingVelocity.x)) {
        velocity.x = Accelerate(velocity.x, mApzc.mLastFlingVelocity.x);
        FLING_LOG("%p Applying fling x-acceleration from %f to %f (delta %f)\n",
                  &mApzc, mApzc.mX.GetVelocity(), velocity.x, mApzc.mLastFlingVelocity.x);
        mApzc.mX.SetVelocity(velocity.x);
      }
      if (SameDirection(velocity.y, mApzc.mLastFlingVelocity.y)) {
        velocity.y = Accelerate(velocity.y, mApzc.mLastFlingVelocity.y);
        FLING_LOG("%p Applying fling y-acceleration from %f to %f (delta %f)\n",
                  &mApzc, mApzc.mY.GetVelocity(), velocity.y, mApzc.mLastFlingVelocity.y);
        mApzc.mY.SetVelocity(velocity.y);
      }
    }

    mApzc.mLastFlingTime = now;
    mApzc.mLastFlingVelocity = velocity;

    FlingPhysics::Init(mApzc.GetVelocityVector(), aPLPPI);
  }

  /**
   * Advances a fling by an interpolated amount based on the passed in |aDelta|.
   * This should be called whenever sampling the content transform for this
   * frame. Returns true if the fling animation should be advanced by one frame,
   * or false if there is no fling or the fling has ended.
   */
  virtual bool DoSample(FrameMetrics& aFrameMetrics,
                        const TimeDuration& aDelta) override
  {
    ParentLayerPoint velocity;
    ParentLayerPoint offset;
    FlingPhysics::Sample(aDelta, &velocity, &offset);

    mApzc.SetVelocityVector(velocity);

    // If we shouldn't continue the fling, let's just stop and repaint.
    if (IsZero(velocity)) {
      FLING_LOG("%p ending fling animation. overscrolled=%d\n", &mApzc, mApzc.IsOverscrolled());
      // This APZC or an APZC further down the handoff chain may be be overscrolled.
      // Start a snap-back animation on the overscrolled APZC.
      // Note:
      //   This needs to be a deferred task even though it can safely run
      //   while holding mRecursiveMutex, because otherwise, if the overscrolled APZC
      //   is this one, then the SetState(NOTHING) in UpdateAnimation will
      //   stomp on the SetState(SNAP_BACK) it does.
      mDeferredTasks.AppendElement(NewRunnableMethod<AsyncPanZoomController*>(
        "layers::OverscrollHandoffChain::SnapBackOverscrolledApzc",
        mOverscrollHandoffChain.get(),
        &OverscrollHandoffChain::SnapBackOverscrolledApzc,
        &mApzc));
      return false;
    }

    // Ordinarily we might need to do a ScheduleComposite if either of
    // the following AdjustDisplacement calls returns true, but this
    // is already running as part of a FlingAnimation, so we'll be compositing
    // per frame of animation anyway.
    ParentLayerPoint overscroll;
    ParentLayerPoint adjustedOffset;
    mApzc.mX.AdjustDisplacement(offset.x, adjustedOffset.x, overscroll.x);
    mApzc.mY.AdjustDisplacement(offset.y, adjustedOffset.y, overscroll.y);

    mApzc.ScrollBy(adjustedOffset / aFrameMetrics.GetZoom());

    // The fling may have caused us to reach the end of our scroll range.
    if (!IsZero(overscroll)) {
      // Hand off the fling to the next APZC in the overscroll handoff chain.

      // We may have reached the end of the scroll range along one axis but
      // not the other. In such a case we only want to hand off the relevant
      // component of the fling.
      if (FuzzyEqualsAdditive(overscroll.x, 0.0f, COORDINATE_EPSILON)) {
        velocity.x = 0;
      } else if (FuzzyEqualsAdditive(overscroll.y, 0.0f, COORDINATE_EPSILON)) {
        velocity.y = 0;
      }

      // To hand off the fling, we attempt to find a target APZC and start a new
      // fling with the same velocity on that APZC. For simplicity, the actual
      // overscroll of the current sample is discarded rather than being handed
      // off. The compositor should sample animations sufficiently frequently
      // that this is not noticeable. The target APZC is chosen by seeing if
      // there is an APZC further in the handoff chain which is pannable; if
      // there isn't, we take the new fling ourselves, entering an overscrolled
      // state.
      // Note: APZC is holding mRecursiveMutex, so directly calling
      // HandleFlingOverscroll() (which acquires the tree lock) would violate
      // the lock ordering. Instead we schedule HandleFlingOverscroll() to be
      // called after mRecursiveMutex is released.
      FLING_LOG("%p fling went into overscroll, handing off with velocity %s\n", &mApzc, Stringify(velocity).c_str());
      mDeferredTasks.AppendElement(
        NewRunnableMethod<ParentLayerPoint,
                          RefPtr<const OverscrollHandoffChain>,
                          RefPtr<const AsyncPanZoomController>>(
          "layers::AsyncPanZoomController::HandleFlingOverscroll",
          &mApzc,
          &AsyncPanZoomController::HandleFlingOverscroll,
          velocity,
          mOverscrollHandoffChain,
          mScrolledApzc));

      // If there is a remaining velocity on this APZC, continue this fling
      // as well. (This fling and the handed-off fling will run concurrently.)
      // Note that AdjustDisplacement() will have zeroed out the velocity
      // along the axes where we're overscrolled.
      return !IsZero(mApzc.GetVelocityVector());
    }

    return true;
  }

private:
  static bool SameDirection(float aVelocity1, float aVelocity2)
  {
    return (aVelocity1 == 0.0f)
        || (aVelocity2 == 0.0f)
        || (IsNegative(aVelocity1) == IsNegative(aVelocity2));
  }

  static float Accelerate(float aBase, float aSupplemental)
  {
    return (aBase * gfxPrefs::APZFlingAccelBaseMultiplier())
         + (aSupplemental * gfxPrefs::APZFlingAccelSupplementalMultiplier());
  }

  AsyncPanZoomController& mApzc;
  RefPtr<const OverscrollHandoffChain> mOverscrollHandoffChain;
  RefPtr<const AsyncPanZoomController> mScrolledApzc;
};

} // namespace layers
} // namespace mozilla

#endif // mozilla_layers_GenericFlingAnimation_h_
