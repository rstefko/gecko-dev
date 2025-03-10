/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/Move.h"
#include "AnimationFrameBuffer.h"

using namespace mozilla;
using namespace mozilla::image;

static already_AddRefed<imgFrame>
CreateEmptyFrame(const IntSize& aSize = IntSize(1, 1),
                 const IntRect& aFrameRect = IntRect(0, 0, 1, 1),
                 bool aCanRecycle = true)
{
  RefPtr<imgFrame> frame = new imgFrame();
  AnimationParams animParams { aFrameRect, FrameTimeout::Forever(),
                               /* aFrameNum */ 1, BlendMethod::OVER,
                               DisposalMethod::NOT_SPECIFIED };
  nsresult rv =
    frame->InitForDecoder(aSize, IntRect(IntPoint(0, 0), aSize),
                          SurfaceFormat::B8G8R8A8, 0, false,
                          Some(animParams), true, aCanRecycle);
  EXPECT_TRUE(NS_SUCCEEDED(rv));
  RawAccessFrameRef frameRef = frame->RawAccessRef();
  frame->SetRawAccessOnly();
  frame->Finish();
  return frame.forget();
}

static void
PrepareForDiscardingQueue(AnimationFrameRetainedBuffer& aQueue)
{
  ASSERT_EQ(size_t(0), aQueue.Size());
  ASSERT_LT(size_t(1), aQueue.Batch());

  AnimationFrameBuffer::InsertStatus status =
    aQueue.Insert(CreateEmptyFrame());
  EXPECT_EQ(AnimationFrameBuffer::InsertStatus::CONTINUE, status);

  while (true) {
    status = aQueue.Insert(CreateEmptyFrame());
    bool restartDecoder = aQueue.AdvanceTo(aQueue.Size() - 1);
    EXPECT_FALSE(restartDecoder);

    if (status == AnimationFrameBuffer::InsertStatus::DISCARD_CONTINUE) {
      break;
    }
    EXPECT_EQ(AnimationFrameBuffer::InsertStatus::CONTINUE, status);
  }

  EXPECT_EQ(aQueue.Threshold(), aQueue.Size());
}

static void
VerifyDiscardingQueueContents(AnimationFrameDiscardingQueue& aQueue)
{
  auto frames = aQueue.Display();
  for (auto i : frames) {
    EXPECT_TRUE(i != nullptr);
  }
}

static void
VerifyInsertInternal(AnimationFrameBuffer& aQueue,
                     imgFrame* aFrame)
{
  // Determine the frame index where we just inserted the frame.
  size_t frameIndex;
  if (aQueue.MayDiscard()) {
    const AnimationFrameDiscardingQueue& queue =
      *static_cast<AnimationFrameDiscardingQueue*>(&aQueue);
    frameIndex = queue.PendingInsert() == 0 ? queue.Size() - 1
                                            : queue.PendingInsert() - 1;
  } else {
    ASSERT_FALSE(aQueue.SizeKnown());
    frameIndex = aQueue.Size() - 1;
  }

  // Make sure we can get the frame from that index.
  RefPtr<imgFrame> frame = aQueue.Get(frameIndex, false);
  EXPECT_EQ(aFrame, frame.get());
}

static void
VerifyAdvance(AnimationFrameBuffer& aQueue,
              size_t aExpectedFrame,
              bool aExpectedRestartDecoder)
{
  RefPtr<imgFrame> oldFrame;
  size_t totalRecycled;
  if (aQueue.IsRecycling()) {
    AnimationFrameRecyclingQueue& queue =
      *static_cast<AnimationFrameRecyclingQueue*>(&aQueue);
    oldFrame = queue.Get(queue.Displayed(), false);
    totalRecycled = queue.Recycle().size();
  }

  bool restartDecoder = aQueue.AdvanceTo(aExpectedFrame);
  EXPECT_EQ(aExpectedRestartDecoder, restartDecoder);

  if (aQueue.IsRecycling()) {
    const AnimationFrameRecyclingQueue& queue =
      *static_cast<AnimationFrameRecyclingQueue*>(&aQueue);
    if (oldFrame->ShouldRecycle()) {
      EXPECT_EQ(oldFrame.get(), queue.Recycle().back().mFrame.get());
      EXPECT_FALSE(queue.Recycle().back().mDirtyRect.IsEmpty());
      EXPECT_FALSE(queue.Recycle().back().mRecycleRect.IsEmpty());
      EXPECT_EQ(totalRecycled + 1, queue.Recycle().size());
    } else {
      EXPECT_EQ(totalRecycled, queue.Recycle().size());
      if (!queue.Recycle().empty()) {
        EXPECT_NE(oldFrame.get(), queue.Recycle().back().mFrame.get());
      }
    }
  }
}

static void
VerifyInsertAndAdvance(AnimationFrameBuffer& aQueue,
                       size_t aExpectedFrame,
                       AnimationFrameBuffer::InsertStatus aExpectedStatus)
{
  // Insert the decoded frame.
  RefPtr<imgFrame> frame = CreateEmptyFrame();
  AnimationFrameBuffer::InsertStatus status =
    aQueue.Insert(RefPtr<imgFrame>(frame));
  EXPECT_EQ(aExpectedStatus, status);
  EXPECT_TRUE(aQueue.IsLastInsertedFrame(frame));
  VerifyInsertInternal(aQueue, frame);

  // Advance the display frame.
  bool expectedRestartDecoder =
    aExpectedStatus == AnimationFrameBuffer::InsertStatus::YIELD;
  VerifyAdvance(aQueue, aExpectedFrame, expectedRestartDecoder);
}

static void
VerifyMarkComplete(AnimationFrameBuffer& aQueue,
                   bool aExpectedContinue,
                   const IntRect& aRefreshArea = IntRect(0, 0, 1, 1))
{
  if (aQueue.IsRecycling() && !aQueue.SizeKnown()) {
    const AnimationFrameRecyclingQueue& queue =
      *static_cast<AnimationFrameRecyclingQueue*>(&aQueue);
    EXPECT_TRUE(queue.FirstFrameRefreshArea().IsEmpty());
  }

  bool keepDecoding = aQueue.MarkComplete(aRefreshArea);
  EXPECT_EQ(aExpectedContinue, keepDecoding);

  if (aQueue.IsRecycling()) {
    const AnimationFrameRecyclingQueue& queue =
      *static_cast<AnimationFrameRecyclingQueue*>(&aQueue);
    EXPECT_EQ(aRefreshArea, queue.FirstFrameRefreshArea());
  }
}

static void
VerifyInsert(AnimationFrameBuffer& aQueue,
             AnimationFrameBuffer::InsertStatus aExpectedStatus)
{
  RefPtr<imgFrame> frame = CreateEmptyFrame();
  AnimationFrameBuffer::InsertStatus status =
    aQueue.Insert(RefPtr<imgFrame>(frame));
  EXPECT_EQ(aExpectedStatus, status);
  EXPECT_TRUE(aQueue.IsLastInsertedFrame(frame));
  VerifyInsertInternal(aQueue, frame);
}

static void
VerifyReset(AnimationFrameBuffer& aQueue,
            bool aExpectedContinue,
            const imgFrame* aFirstFrame)
{
  bool keepDecoding = aQueue.Reset();
  EXPECT_EQ(aExpectedContinue, keepDecoding);
  EXPECT_EQ(aQueue.Batch() * 2, aQueue.PendingDecode());
  EXPECT_EQ(aFirstFrame, aQueue.Get(0, true));

  if (!aQueue.MayDiscard()) {
    const AnimationFrameRetainedBuffer& queue =
      *static_cast<AnimationFrameRetainedBuffer*>(&aQueue);
    EXPECT_EQ(aFirstFrame, queue.Frames()[0].get());
    EXPECT_EQ(aFirstFrame, aQueue.Get(0, false));
  } else {
    const AnimationFrameDiscardingQueue& queue =
      *static_cast<AnimationFrameDiscardingQueue*>(&aQueue);
    EXPECT_EQ(size_t(0), queue.PendingInsert());
    EXPECT_EQ(size_t(0), queue.Display().size());
    EXPECT_EQ(aFirstFrame, queue.FirstFrame());
    EXPECT_EQ(nullptr, aQueue.Get(0, false));
  }

  if (aQueue.IsRecycling()) {
    const AnimationFrameRecyclingQueue& queue =
      *static_cast<AnimationFrameRecyclingQueue*>(&aQueue);
    EXPECT_EQ(size_t(0), queue.Recycle().size());
  }
}

class ImageAnimationFrameBuffer : public ::testing::Test
{
public:
  ImageAnimationFrameBuffer()
  { }

private:
  AutoInitializeImageLib mInit;
};

TEST_F(ImageAnimationFrameBuffer, RetainedInitialState)
{
  const size_t kThreshold = 800;
  const size_t kBatch = 100;
  AnimationFrameRetainedBuffer buffer(kThreshold, kBatch, 0);

  EXPECT_EQ(kThreshold, buffer.Threshold());
  EXPECT_EQ(kBatch, buffer.Batch());
  EXPECT_EQ(size_t(0), buffer.Displayed());
  EXPECT_EQ(kBatch * 2, buffer.PendingDecode());
  EXPECT_EQ(size_t(0), buffer.PendingAdvance());
  EXPECT_FALSE(buffer.MayDiscard());
  EXPECT_FALSE(buffer.SizeKnown());
  EXPECT_EQ(size_t(0), buffer.Size());
}

TEST_F(ImageAnimationFrameBuffer, ThresholdTooSmall)
{
  const size_t kThreshold = 0;
  const size_t kBatch = 10;
  AnimationFrameRetainedBuffer buffer(kThreshold, kBatch, 0);

  EXPECT_EQ(kBatch * 2 + 1, buffer.Threshold());
  EXPECT_EQ(kBatch, buffer.Batch());
  EXPECT_EQ(kBatch * 2, buffer.PendingDecode());
  EXPECT_EQ(size_t(0), buffer.PendingAdvance());
}

TEST_F(ImageAnimationFrameBuffer, BatchTooSmall)
{
  const size_t kThreshold = 10;
  const size_t kBatch = 0;
  AnimationFrameRetainedBuffer buffer(kThreshold, kBatch, 0);

  EXPECT_EQ(kThreshold, buffer.Threshold());
  EXPECT_EQ(size_t(1), buffer.Batch());
  EXPECT_EQ(size_t(2), buffer.PendingDecode());
  EXPECT_EQ(size_t(0), buffer.PendingAdvance());
}

TEST_F(ImageAnimationFrameBuffer, BatchTooBig)
{
  const size_t kThreshold = 50;
  const size_t kBatch = SIZE_MAX;
  AnimationFrameRetainedBuffer buffer(kThreshold, kBatch, 0);

  // The rounding is important here (e.g. SIZE_MAX/4 * 2 != SIZE_MAX/2).
  EXPECT_EQ(SIZE_MAX/4, buffer.Batch());
  EXPECT_EQ(buffer.Batch() * 2 + 1, buffer.Threshold());
  EXPECT_EQ(buffer.Batch() * 2, buffer.PendingDecode());
  EXPECT_EQ(size_t(0), buffer.PendingAdvance());
}

TEST_F(ImageAnimationFrameBuffer, FinishUnderBatchAndThreshold)
{
  const size_t kThreshold = 30;
  const size_t kBatch = 10;
  AnimationFrameRetainedBuffer buffer(kThreshold, kBatch, 0);
  const auto& frames = buffer.Frames();

  EXPECT_EQ(kBatch * 2, buffer.PendingDecode());

  RefPtr<imgFrame> firstFrame;
  for (size_t i = 0; i < 5; ++i) {
    RefPtr<imgFrame> frame = CreateEmptyFrame();
    auto status = buffer.Insert(RefPtr<imgFrame>(frame));
    EXPECT_EQ(status, AnimationFrameBuffer::InsertStatus::CONTINUE);
    EXPECT_FALSE(buffer.SizeKnown());
    EXPECT_EQ(buffer.Size(), i + 1);

    if (i == 4) {
      EXPECT_EQ(size_t(15), buffer.PendingDecode());
      bool keepDecoding = buffer.MarkComplete(IntRect(0, 0, 1, 1));
      EXPECT_FALSE(keepDecoding);
      EXPECT_TRUE(buffer.SizeKnown());
      EXPECT_EQ(size_t(0), buffer.PendingDecode());
      EXPECT_FALSE(buffer.HasRedecodeError());
    }

    EXPECT_FALSE(buffer.MayDiscard());

    imgFrame* gotFrame = buffer.Get(i, false);
    EXPECT_EQ(frame.get(), gotFrame);
    ASSERT_EQ(i + 1, frames.Length());
    EXPECT_EQ(frame.get(), frames[i].get());

    if (i == 0) {
      firstFrame = std::move(frame);
      EXPECT_EQ(size_t(0), buffer.Displayed());
    } else {
      EXPECT_EQ(i - 1, buffer.Displayed());
      bool restartDecoder = buffer.AdvanceTo(i);
      EXPECT_FALSE(restartDecoder);
      EXPECT_EQ(i, buffer.Displayed());
    }

    gotFrame = buffer.Get(0, false);
    EXPECT_EQ(firstFrame.get(), gotFrame);
  }

  // Loop again over the animation and make sure it is still all there.
  for (size_t i = 0; i < frames.Length(); ++i) {
    EXPECT_TRUE(buffer.Get(i, false) != nullptr);

    bool restartDecoder = buffer.AdvanceTo(i);
    EXPECT_FALSE(restartDecoder);
  }
}

TEST_F(ImageAnimationFrameBuffer, FinishMultipleBatchesUnderThreshold)
{
  const size_t kThreshold = 30;
  const size_t kBatch = 2;
  AnimationFrameRetainedBuffer buffer(kThreshold, kBatch, 0);
  const auto& frames = buffer.Frames();

  EXPECT_EQ(kBatch * 2, buffer.PendingDecode());

  // Add frames until it tells us to stop.
  AnimationFrameBuffer::InsertStatus status;
  do {
    status = buffer.Insert(CreateEmptyFrame());
    EXPECT_FALSE(buffer.SizeKnown());
    EXPECT_FALSE(buffer.MayDiscard());
  } while (status == AnimationFrameBuffer::InsertStatus::CONTINUE);

  EXPECT_EQ(size_t(0), buffer.PendingDecode());
  EXPECT_EQ(size_t(4), frames.Length());
  EXPECT_EQ(status, AnimationFrameBuffer::InsertStatus::YIELD);

  // Progress through the animation until it lets us decode again.
  bool restartDecoder = false;
  size_t i = 0;
  do {
    EXPECT_TRUE(buffer.Get(i, false) != nullptr);
    if (i > 0) {
      restartDecoder = buffer.AdvanceTo(i);
    }
    ++i;
  } while (!restartDecoder);

  EXPECT_EQ(size_t(2), buffer.PendingDecode());
  EXPECT_EQ(size_t(2), buffer.Displayed());

  // Add the last frame.
  status = buffer.Insert(CreateEmptyFrame());
  EXPECT_EQ(status, AnimationFrameBuffer::InsertStatus::CONTINUE);
  bool keepDecoding = buffer.MarkComplete(IntRect(0, 0, 1, 1));
  EXPECT_FALSE(keepDecoding);
  EXPECT_TRUE(buffer.SizeKnown());
  EXPECT_EQ(size_t(0), buffer.PendingDecode());
  EXPECT_EQ(size_t(5), frames.Length());
  EXPECT_FALSE(buffer.HasRedecodeError());

  // Finish progressing through the animation.
  for ( ; i < frames.Length(); ++i) {
    EXPECT_TRUE(buffer.Get(i, false) != nullptr);
    restartDecoder = buffer.AdvanceTo(i);
    EXPECT_FALSE(restartDecoder);
  }

  // Loop again over the animation and make sure it is still all there.
  for (i = 0; i < frames.Length(); ++i) {
    EXPECT_TRUE(buffer.Get(i, false) != nullptr);
    restartDecoder = buffer.AdvanceTo(i);
    EXPECT_FALSE(restartDecoder);
  }

  // Loop to the third frame and then reset the animation.
  for (i = 0; i < 3; ++i) {
    EXPECT_TRUE(buffer.Get(i, false) != nullptr);
    restartDecoder = buffer.AdvanceTo(i);
    EXPECT_FALSE(restartDecoder);
  }

  // Since we are below the threshold, we can reset the get index only.
  // Nothing else should have changed.
  restartDecoder = buffer.Reset();
  EXPECT_FALSE(restartDecoder);
  for (i = 0; i < 5; ++i) {
    EXPECT_TRUE(buffer.Get(i, false) != nullptr);
  }
  EXPECT_EQ(size_t(0), buffer.PendingDecode());
  EXPECT_EQ(size_t(0), buffer.PendingAdvance());
  EXPECT_EQ(size_t(0), buffer.Displayed());
}

TEST_F(ImageAnimationFrameBuffer, StartAfterBeginning)
{
  const size_t kThreshold = 30;
  const size_t kBatch = 2;
  const size_t kStartFrame = 7;
  AnimationFrameRetainedBuffer buffer(kThreshold, kBatch, kStartFrame);

  EXPECT_EQ(kStartFrame, buffer.PendingAdvance());

  // Add frames until it tells us to stop. It should be later than before,
  // because it auto-advances until its displayed frame is kStartFrame.
  AnimationFrameBuffer::InsertStatus status;
  size_t i = 0;
  do {
    status = buffer.Insert(CreateEmptyFrame());
    EXPECT_FALSE(buffer.SizeKnown());
    EXPECT_FALSE(buffer.MayDiscard());

    if (i <= kStartFrame) {
      EXPECT_EQ(i, buffer.Displayed());
      EXPECT_EQ(kStartFrame - i, buffer.PendingAdvance());
    } else {
      EXPECT_EQ(kStartFrame, buffer.Displayed());
      EXPECT_EQ(size_t(0), buffer.PendingAdvance());
    }

    i++;
  } while (status == AnimationFrameBuffer::InsertStatus::CONTINUE);

  EXPECT_EQ(size_t(0), buffer.PendingDecode());
  EXPECT_EQ(size_t(0), buffer.PendingAdvance());
  EXPECT_EQ(size_t(10), buffer.Size());
}

TEST_F(ImageAnimationFrameBuffer, StartAfterBeginningAndReset)
{
  const size_t kThreshold = 30;
  const size_t kBatch = 2;
  const size_t kStartFrame = 7;
  AnimationFrameRetainedBuffer buffer(kThreshold, kBatch, kStartFrame);

  EXPECT_EQ(kStartFrame, buffer.PendingAdvance());

  // Add frames until it tells us to stop. It should be later than before,
  // because it auto-advances until its displayed frame is kStartFrame.
  for (size_t i = 0; i < 5; ++i) {
    AnimationFrameBuffer::InsertStatus status =
      buffer.Insert(CreateEmptyFrame());
    EXPECT_EQ(status, AnimationFrameBuffer::InsertStatus::CONTINUE);
    EXPECT_FALSE(buffer.SizeKnown());
    EXPECT_FALSE(buffer.MayDiscard());
    EXPECT_EQ(i, buffer.Displayed());
    EXPECT_EQ(kStartFrame - i, buffer.PendingAdvance());
  }

  // When we reset the animation, it goes back to the beginning. That means
  // we can forget about what we were told to advance to at the start. While
  // we have plenty of frames in our buffer, we still need one more because
  // in the real scenario, the decoder thread is still running and it is easier
  // to let it insert its last frame than to coordinate quitting earlier.
  buffer.Reset();
  EXPECT_EQ(size_t(0), buffer.Displayed());
  EXPECT_EQ(size_t(1), buffer.PendingDecode());
  EXPECT_EQ(size_t(0), buffer.PendingAdvance());
  EXPECT_EQ(size_t(5), buffer.Size());
}

static void TestDiscardingQueueLoop(AnimationFrameDiscardingQueue& aQueue,
                                    const imgFrame* aFirstFrame,
                                    size_t aThreshold,
                                    size_t aBatch,
                                    size_t aStartFrame)
{
  // We should be advanced right up to the last decoded frame.
  EXPECT_TRUE(aQueue.MayDiscard());
  EXPECT_FALSE(aQueue.SizeKnown());
  EXPECT_EQ(aBatch, aQueue.Batch());
  EXPECT_EQ(aThreshold, aQueue.PendingInsert());
  EXPECT_EQ(aThreshold, aQueue.Size());
  EXPECT_EQ(aFirstFrame, aQueue.FirstFrame());
  EXPECT_EQ(size_t(1), aQueue.Display().size());
  EXPECT_EQ(size_t(3), aQueue.PendingDecode());
  VerifyDiscardingQueueContents(aQueue);

  // Make sure frames get removed as we advance.
  VerifyInsertAndAdvance(aQueue, 5, AnimationFrameBuffer::InsertStatus::CONTINUE);
  EXPECT_EQ(size_t(1), aQueue.Display().size());
  VerifyInsertAndAdvance(aQueue, 6, AnimationFrameBuffer::InsertStatus::CONTINUE);
  EXPECT_EQ(size_t(1), aQueue.Display().size());

  // We actually will yield if we are recycling instead of continuing because
  // the pending calculation is slightly different. We will actually request one
  // less frame than we have to recycle.
  if (aQueue.IsRecycling()) {
    VerifyInsertAndAdvance(aQueue, 7, AnimationFrameBuffer::InsertStatus::YIELD);
  } else {
    VerifyInsertAndAdvance(aQueue, 7, AnimationFrameBuffer::InsertStatus::CONTINUE);
  }
  EXPECT_EQ(size_t(1), aQueue.Display().size());

  // We should get throttled if we insert too much.
  VerifyInsert(aQueue, AnimationFrameBuffer::InsertStatus::CONTINUE);
  EXPECT_EQ(size_t(2), aQueue.Display().size());
  EXPECT_EQ(size_t(1), aQueue.PendingDecode());
  VerifyInsert(aQueue, AnimationFrameBuffer::InsertStatus::YIELD);
  EXPECT_EQ(size_t(3), aQueue.Display().size());
  EXPECT_EQ(size_t(0), aQueue.PendingDecode());

  // We should get restarted if we advance.
  VerifyAdvance(aQueue, 8, true);
  EXPECT_EQ(size_t(2), aQueue.PendingDecode());
  VerifyAdvance(aQueue, 9, false);
  EXPECT_EQ(size_t(2), aQueue.PendingDecode());

  // We should continue decoding if we completed, since we are discarding.
  VerifyMarkComplete(aQueue, true);
  EXPECT_EQ(size_t(2), aQueue.PendingDecode());
  EXPECT_EQ(size_t(10), aQueue.Size());
  EXPECT_TRUE(aQueue.SizeKnown());
  EXPECT_FALSE(aQueue.HasRedecodeError());

  // Insert the first frames of the animation.
  VerifyInsert(aQueue, AnimationFrameBuffer::InsertStatus::CONTINUE);
  VerifyInsert(aQueue, AnimationFrameBuffer::InsertStatus::YIELD);
  EXPECT_EQ(size_t(0), aQueue.PendingDecode());
  EXPECT_EQ(size_t(10), aQueue.Size());

  // Advance back at the beginning. The first frame should only match for
  // display purposes.
  VerifyAdvance(aQueue, 0, true);
  EXPECT_EQ(size_t(2), aQueue.PendingDecode());
  EXPECT_TRUE(aQueue.FirstFrame() != nullptr);
  EXPECT_TRUE(aQueue.Get(0, false) != nullptr);
  EXPECT_NE(aQueue.FirstFrame(), aQueue.Get(0, false));
  EXPECT_EQ(aQueue.FirstFrame(), aQueue.Get(0, true));

  // Reiterate one more time and make it loops back.
  VerifyInsertAndAdvance(aQueue, 1, AnimationFrameBuffer::InsertStatus::CONTINUE);
  VerifyInsertAndAdvance(aQueue, 2, AnimationFrameBuffer::InsertStatus::YIELD);
  VerifyInsertAndAdvance(aQueue, 3, AnimationFrameBuffer::InsertStatus::CONTINUE);
  VerifyInsertAndAdvance(aQueue, 4, AnimationFrameBuffer::InsertStatus::YIELD);
  VerifyInsertAndAdvance(aQueue, 5, AnimationFrameBuffer::InsertStatus::CONTINUE);
  VerifyInsertAndAdvance(aQueue, 6, AnimationFrameBuffer::InsertStatus::YIELD);
  VerifyInsertAndAdvance(aQueue, 7, AnimationFrameBuffer::InsertStatus::CONTINUE);
  VerifyInsertAndAdvance(aQueue, 8, AnimationFrameBuffer::InsertStatus::YIELD);

  EXPECT_EQ(size_t(10), aQueue.PendingInsert());
  VerifyMarkComplete(aQueue, true);
  EXPECT_EQ(size_t(0), aQueue.PendingInsert());

  VerifyInsertAndAdvance(aQueue, 9, AnimationFrameBuffer::InsertStatus::CONTINUE);
  VerifyInsertAndAdvance(aQueue, 0, AnimationFrameBuffer::InsertStatus::YIELD);
  VerifyInsertAndAdvance(aQueue, 1, AnimationFrameBuffer::InsertStatus::CONTINUE);
}

TEST_F(ImageAnimationFrameBuffer, DiscardingLoop)
{
  const size_t kThreshold = 5;
  const size_t kBatch = 2;
  const size_t kStartFrame = 0;
  AnimationFrameRetainedBuffer retained(kThreshold, kBatch, kStartFrame);
  PrepareForDiscardingQueue(retained);
  const imgFrame* firstFrame = retained.Frames()[0].get();
  AnimationFrameDiscardingQueue buffer(std::move(retained));
  TestDiscardingQueueLoop(buffer, firstFrame, kThreshold, kBatch, kStartFrame);
}

TEST_F(ImageAnimationFrameBuffer, RecyclingLoop)
{
  const size_t kThreshold = 5;
  const size_t kBatch = 2;
  const size_t kStartFrame = 0;
  AnimationFrameRetainedBuffer retained(kThreshold, kBatch, kStartFrame);
  PrepareForDiscardingQueue(retained);
  const imgFrame* firstFrame = retained.Frames()[0].get();
  AnimationFrameRecyclingQueue buffer(std::move(retained));

  // We should not start with any recycled frames.
  ASSERT_TRUE(buffer.Recycle().empty());

  TestDiscardingQueueLoop(buffer, firstFrame, kThreshold, kBatch, kStartFrame);

  // All the frames we inserted should have been recycleable.
  ASSERT_FALSE(buffer.Recycle().empty());
  while (!buffer.Recycle().empty()) {
    IntRect expectedRect = buffer.Recycle().front().mRecycleRect;
    RefPtr<imgFrame> expectedFrame = buffer.Recycle().front().mFrame;
    EXPECT_FALSE(expectedRect.IsEmpty());
    EXPECT_TRUE(expectedFrame.get() != nullptr);

    IntRect gotRect;
    RawAccessFrameRef gotFrame = buffer.RecycleFrame(gotRect);
    EXPECT_EQ(expectedFrame.get(), gotFrame.get());
    EXPECT_EQ(expectedRect, gotRect);
  }

  // Trying to pull a recycled frame when we have nothing should be safe too.
  IntRect gotRect;
  RawAccessFrameRef gotFrame = buffer.RecycleFrame(gotRect);
  EXPECT_TRUE(gotFrame.get() == nullptr);
  EXPECT_TRUE(gotRect.IsEmpty());
}

static void TestDiscardingQueueReset(AnimationFrameDiscardingQueue& aQueue,
                                     const imgFrame* aFirstFrame,
                                     size_t aThreshold,
                                     size_t aBatch,
                                     size_t aStartFrame)
{
  // We should be advanced right up to the last decoded frame.
  EXPECT_TRUE(aQueue.MayDiscard());
  EXPECT_FALSE(aQueue.SizeKnown());
  EXPECT_EQ(aBatch, aQueue.Batch());
  EXPECT_EQ(aThreshold, aQueue.PendingInsert());
  EXPECT_EQ(aThreshold, aQueue.Size());
  EXPECT_EQ(aFirstFrame, aQueue.FirstFrame());
  EXPECT_EQ(size_t(1), aQueue.Display().size());
  EXPECT_EQ(size_t(4), aQueue.PendingDecode());
  VerifyDiscardingQueueContents(aQueue);

  // Reset should clear everything except the first frame.
  VerifyReset(aQueue, false, aFirstFrame);
}

TEST_F(ImageAnimationFrameBuffer, DiscardingReset)
{
  const size_t kThreshold = 8;
  const size_t kBatch = 3;
  const size_t kStartFrame = 0;
  AnimationFrameRetainedBuffer retained(kThreshold, kBatch, kStartFrame);
  PrepareForDiscardingQueue(retained);
  const imgFrame* firstFrame = retained.Frames()[0].get();
  AnimationFrameDiscardingQueue buffer(std::move(retained));
  TestDiscardingQueueReset(buffer, firstFrame, kThreshold, kBatch, kStartFrame);
}

TEST_F(ImageAnimationFrameBuffer, RecyclingReset)
{
  const size_t kThreshold = 8;
  const size_t kBatch = 3;
  const size_t kStartFrame = 0;
  AnimationFrameRetainedBuffer retained(kThreshold, kBatch, kStartFrame);
  PrepareForDiscardingQueue(retained);
  const imgFrame* firstFrame = retained.Frames()[0].get();
  AnimationFrameRecyclingQueue buffer(std::move(retained));
  TestDiscardingQueueReset(buffer, firstFrame, kThreshold, kBatch, kStartFrame);
}
