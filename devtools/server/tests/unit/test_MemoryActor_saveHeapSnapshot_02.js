/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test that we can properly stream heap snapshot files over the RDP as bulk
// data.

const { OS } = require("resource://gre/modules/osfile.jsm");

add_task(async () => {
  const { client, memoryFront } = await createTabMemoryFront();

  const snapshotFilePath = await memoryFront.saveHeapSnapshot({
    forceCopy: true,
  });
  ok(!!(await OS.File.stat(snapshotFilePath)),
     "Should have the heap snapshot file");
  const snapshot = ChromeUtils.readHeapSnapshot(snapshotFilePath);
  ok(snapshot instanceof HeapSnapshot,
     "And we should be able to read a HeapSnapshot instance from the file");

  await memoryFront.detach();
  await finishClient(client);
});
