/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test the flex item list is empty when there are no flex items for the selected flex
// container.

const TEST_URI = `
  <div id="container" style="display:flex">
  </div>
`;

add_task(async function() {
  await addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  const { inspector, flexboxInspector } = await openLayoutView();
  const { document: doc } = flexboxInspector;
  const { highlighters } = inspector;

  const onFlexHeaderRendered = waitForDOM(doc, ".flex-header");
  await selectNode("#container", inspector);
  const [flexHeader] = await onFlexHeaderRendered;
  const flexHighlighterToggle = flexHeader.querySelector("#flexbox-checkbox-toggle");
  const flexItemList = doc.querySelector(".flex-item-list");

  info("Checking the state of the Flexbox Inspector.");
  ok(flexHeader, "The flex container header is rendered.");
  ok(flexHighlighterToggle, "The flexbox highlighter toggle is rendered.");
  ok(!flexItemList, "The flex item list is not rendered.");
  ok(!flexHighlighterToggle.checked, "The flexbox highlighter toggle is unchecked.");
  ok(!highlighters.flexboxHighlighterShown, "No flexbox highlighter is shown.");
});
