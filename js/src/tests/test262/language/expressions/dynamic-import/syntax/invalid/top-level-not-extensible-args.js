// |reftest| skip error:SyntaxError -- dynamic-import is not supported
// This file was procedurally generated from the following sources:
// - src/dynamic-import/not-extensible-args.case
// - src/dynamic-import/syntax/invalid/top-level.template
/*---
description: ImportCall is not extensible - no arguments list (top level syntax)
esid: sec-import-call-runtime-semantics-evaluation
features: [dynamic-import]
flags: [generated]
negative:
  phase: parse
  type: SyntaxError
info: |
    ImportCall :
        import( AssignmentExpression )


    ImportCall :
        import( AssignmentExpression[+In, ?Yield] )

    Forbidden Extensions

    - ImportCall must not be extended.
---*/

$DONOTEVALUATE();

import('', '');
