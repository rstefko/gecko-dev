/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_CLIPMANAGER_H
#define GFX_CLIPMANAGER_H

#include <stack>
#include <unordered_map>

#include "mozilla/Attributes.h"
#include "mozilla/webrender/WebRenderAPI.h"

class nsDisplayItem;

namespace mozilla {

struct ActiveScrolledRoot;
struct DisplayItemClipChain;

namespace wr {
class DisplayListBuilder;
}

namespace layers {

class StackingContextHelper;
class WebRenderLayerManager;

/**
 * This class manages creating and assigning scroll layers and clips in WebRender
 * based on the gecko display list. It has a few public functions that are
 * intended to be invoked while traversing the Gecko display list, and it uses
 * the ASR and clip information from the display list to create the necessary
 * clip state in WebRender.
 *
 * The structure of the clip state in WebRender ends up quite similar to how
 * it is in Gecko. For each ASR in Gecko, we create a scroll layer (i.e. a
 * scrolling clip) in WebRender; these form a tree structure similar to the
 * ASR tree structure. Ancestors of scroll layers are always other scroll
 * layers, or the root scroll node.
 * The DisplayItemClipChain list of clips from the gecko display list is
 * converted to a WR clip chain and pushed on the stack prior to creating
 * any WR commands for that item, and is popped afterwards. In addition,
 * the WR clip chain has a parent pointer, which points to the clip chain for
 * any enclosing stacking context. This again results in a strucuture very
 * similar to that in Gecko, where the clips from container display items get
 * applied to the contained display items.
 */
class ClipManager
{
public:
  ClipManager();

  void BeginBuild(WebRenderLayerManager* aManager,
                  wr::DisplayListBuilder& aBuilder);
  void EndBuild();

  void BeginList(const StackingContextHelper& aStackingContext);
  void EndList(const StackingContextHelper& aStackingContext);

  void BeginItem(nsDisplayItem* aItem,
                 const StackingContextHelper& aStackingContext);
  ~ClipManager();

  void PushOverrideForASR(const ActiveScrolledRoot* aASR,
                          const wr::WrClipId& aClipId);
  void PopOverrideForASR(const ActiveScrolledRoot* aASR);

private:
  Maybe<wr::WrClipId> ClipIdAfterOverride(const Maybe<wr::WrClipId>& aClipId);

  Maybe<wr::WrClipId>
  GetScrollLayer(const ActiveScrolledRoot* aASR);

  Maybe<wr::WrClipId>
  DefineScrollLayers(const ActiveScrolledRoot* aASR,
                     nsDisplayItem* aItem,
                     const StackingContextHelper& aSc);

  Maybe<wr::WrClipChainId>
  DefineClipChain(const DisplayItemClipChain* aChain,
                  int32_t aAppUnitsPerDevPixel,
                  const StackingContextHelper& aSc);

  WebRenderLayerManager* MOZ_NON_OWNING_REF mManager;
  wr::DisplayListBuilder* mBuilder;

  // Stack of clip caches. Each cache contains a map from gecko
  // DisplayItemClipChain objects to webrender WrClipIds, which allows us to
  // avoid redefining identical clips in WR. However, the gecko
  // DisplayItemClipChain items get deduplicated quite aggressively, without
  // regard to things like the enclosing reference frame or mask. On the WR
  // side, we cannot deduplicate clips that aggressively. So what we do is
  // any time we enter a new reference frame (for example) we create a new clip
  // cache on mCacheStack. This ensures we continue caching stuff within a given
  // reference frame, but disallow caching stuff across reference frames. In
  // general we need to do this anytime PushOverrideForASR is called, as that is
  // called for the same set of conditions for which we cannot deduplicate clips.
  typedef std::unordered_map<const DisplayItemClipChain*, wr::WrClipId> ClipIdMap;
  std::stack<ClipIdMap> mCacheStack;

  // A map that holds the cache overrides created by (a) "out of band" clips,
  // i.e. clips that are generated by display items but that ClipManager
  // doesn't know about and (b) stacking contexts that affect clip positioning.
  // These are called "cache overrides" because while we're inside these things,
  // we cannot use the ASR from the gecko display list as-is. Fundamentally this
  // results from a mismatch between the ASR+clip items on the gecko side and
  // the ClipScrollTree on the WR side; the WR side incorporates things like
  // transforms and stacking context origins while the gecko side manages those
  // differently.
  // Any time ClipManager wants to define a new clip as a child of ASR X, it
  // should first check the cache overrides to see if there is a cache override
  // item ((a) or (b) above) that is already a child of X, and then define that
  // clip as a child of Y instead. This map stores X -> Y, which allows
  // ClipManager to do the necessary lookup. Note that there theoretically might
  // be multiple different "Y" clips (in case of nested cache overrides), which
  // is why we need a stack.
  std::unordered_map<wr::WrClipId,
                     std::stack<wr::WrClipId>> mASROverride;

  // This holds some clip state for a single nsDisplayItem
  struct ItemClips {
    ItemClips(const ActiveScrolledRoot* aASR,
              const DisplayItemClipChain* aChain,
              bool aSeparateLeaf);

    // These are the "inputs" - they come from the nsDisplayItem
    const ActiveScrolledRoot* mASR;
    const DisplayItemClipChain* mChain;
    bool mSeparateLeaf;

    // These are the "outputs" - they are pushed to WR as needed
    Maybe<wr::WrClipId> mScrollId;
    Maybe<wr::WrClipChainId> mClipChainId;

    // State tracking
    bool mApplied;

    void Apply(wr::DisplayListBuilder* aBuilder,
               int32_t aAppUnitsPerDevPixel);
    void Unapply(wr::DisplayListBuilder* aBuilder);
    bool HasSameInputs(const ItemClips& aOther);
    void CopyOutputsFrom(const ItemClips& aOther);
  };

  // A stack of ItemClips corresponding to the nsDisplayItem ancestry. Each
  // time we recurse into a nsDisplayItem's child list, this stack size
  // increases by one. The topmost item on the stack is for the display item
  // we are currently processing and items deeper on the stack are for that
  // display item's ancestors.
  std::stack<ItemClips> mItemClipStack;
};

} // namespace layers
} // namespace mozilla

#endif
