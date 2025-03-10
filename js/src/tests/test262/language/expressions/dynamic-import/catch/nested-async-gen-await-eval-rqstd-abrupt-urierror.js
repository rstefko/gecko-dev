// |reftest| skip -- dynamic-import is not supported
// This file was procedurally generated from the following sources:
// - src/dynamic-import/eval-rqstd-abrupt-urierror.case
// - src/dynamic-import/catch/nested-async-generator-await.template
/*---
description: Abrupt completion during module evaluation precludes further evaluation (nested in async generator, awaited)
esid: sec-import-call-runtime-semantics-evaluation
features: [dynamic-import, async-iteration]
flags: [generated, async]
info: |
    ImportCall :
        import( AssignmentExpression )

    1. Let referencingScriptOrModule be ! GetActiveScriptOrModule().
    2. Assert: referencingScriptOrModule is a Script Record or Module Record (i.e. is not null).
    3. Let argRef be the result of evaluating AssignmentExpression.
    4. Let specifier be ? GetValue(argRef).
    5. Let promiseCapability be ! NewPromiseCapability(%Promise%).
    6. Let specifierString be ToString(specifier).
    7. IfAbruptRejectPromise(specifierString, promiseCapability).
    8. Perform ! HostImportModuleDynamically(referencingScriptOrModule, specifierString, promiseCapability).
    9. Return promiseCapability.[[Promise]].


    [...]
    6. For each String required that is an element of
       module.[[RequestedModules]] do,
       a. Let requiredModule be ? HostResolveImportedModule(module, required).
       b. Perform ? requiredModule.ModuleEvaluation().

---*/

async function * f() {
  await import('./eval-rqstd-abrupt-err-uri_FIXTURE.js');
}

f().next().catch(error => {

  assert.sameValue(error.name, 'URIError');

}).then($DONE, $DONE);
