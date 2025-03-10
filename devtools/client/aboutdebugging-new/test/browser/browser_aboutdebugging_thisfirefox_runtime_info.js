/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/* import-globals-from mocks/head-client-wrapper-mock.js */
Services.scriptloader.loadSubScript(
  CHROME_URL_ROOT + "mocks/head-client-wrapper-mock.js", this);
/* import-globals-from mocks/head-runtime-client-factory-mock.js */
Services.scriptloader.loadSubScript(
  CHROME_URL_ROOT + "mocks/head-runtime-client-factory-mock.js", this);

/**
 * Check that the runtime info is correctly displayed for ThisFirefox.
 * Also acts as basic sanity check for the default mock of the this-firefox client.
 */

add_task(async function() {
  // Setup a mock for our runtime client factory to return the default THIS_FIREFOX client
  // when the client for the this-firefox runtime is requested.
  const runtimeClientFactoryMock = createRuntimeClientFactoryMock();
  const thisFirefoxClient = createThisFirefoxClientMock();
  runtimeClientFactoryMock.createClientForRuntime = runtime => {
    const { RUNTIMES } = require("devtools/client/aboutdebugging-new/src/constants");
    if (runtime.id === RUNTIMES.THIS_FIREFOX) {
      return { clientWrapper: thisFirefoxClient };
    }
    throw new Error("Unexpected runtime id " + runtime.id);
  };

  info("Enable mocks");
  enableRuntimeClientFactoryMock(runtimeClientFactoryMock);
  registerCleanupFunction(() => {
    disableRuntimeClientFactoryMock();
  });

  const { document, tab } = await openAboutDebugging();

  info("Check that the 'This Firefox' mock is properly displayed");
  const thisFirefoxRuntimeInfo = document.querySelector(".js-runtime-info");
  ok(thisFirefoxRuntimeInfo, "Runtime info for this-firefox runtime is displayed");
  const runtimeInfoText = thisFirefoxRuntimeInfo.textContent;
  ok(runtimeInfoText.includes("Firefox"),
    "this-firefox runtime info shows the correct runtime name: " + runtimeInfoText);
  ok(runtimeInfoText.includes("63.0"),
    "this-firefox runtime info shows the correct version number: " + runtimeInfoText);

  await removeTab(tab);
});
