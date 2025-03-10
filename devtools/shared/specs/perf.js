/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
"use strict";

const { Arg, Option, RetVal, generateActorSpec } = require("devtools/shared/protocol");

const perfDescription = {
  typeName: "perf",

  events: {
    "profiler-started": {
      type: "profiler-started",
      entries: Arg(0, "number"),
      interval: Arg(1, "number"),
      features: Arg(2, "number"),
    },
    "profiler-stopped": {
      type: "profiler-stopped",
    },
    "profile-locked-by-private-browsing": {
      type: "profile-locked-by-private-browsing",
    },
    "profile-unlocked-from-private-browsing": {
      type: "profile-unlocked-from-private-browsing",
    },
  },

  methods: {
    startProfiler: {
      request: {
        entries: Option(0, "number"),
        interval: Option(0, "number"),
        features: Option(0, "array:string"),
        threads: Option(0, "array:string"),
      },
      response: { value: RetVal("boolean") },
    },

    /**
     * Returns null when unable to return the profile.
     */
    getProfileAndStopProfiler: {
      request: {},
      response: RetVal("nullable:json"),
    },

    stopProfilerAndDiscardProfile: {
      request: {},
      response: {},
    },

    getSymbolTable: {
      request: {
        debugPath: Arg(0, "string"),
        breakpadId: Arg(1, "string"),
      },
      response: { value: RetVal("array:array:number") },
    },

    isActive: {
      request: {},
      response: { value: RetVal("boolean") },
    },

    isSupportedPlatform: {
      request: {},
      response: { value: RetVal("boolean") },
    },

    isLockedForPrivateBrowsing: {
      request: {},
      response: { value: RetVal("boolean") },
    },
  },
};

exports.perfDescription = perfDescription;

const perfSpec = generateActorSpec(perfDescription);

exports.perfSpec = perfSpec;
