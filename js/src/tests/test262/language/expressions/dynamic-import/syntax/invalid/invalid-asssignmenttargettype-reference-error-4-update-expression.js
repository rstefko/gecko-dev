// |reftest| skip error:ReferenceError -- dynamic-import is not supported
// Copyright (C) 2018 Leo Balter. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: >
    ImportCall is a valid CallExpression and UnaryExpression, but it is an invalid
    AssginmentTargetType then it should throw a ReferenceError if used in some
    UpdateExpressions
esid: prod-ImportCall
info: |
    Update Expressions
    Static Semantics: Early Errors

    UpdateExpression:
        ++UnaryExpression
        --UnaryExpression

    - It is an early Reference Error if AssignmentTargetType of UnaryExpression is invalid.

    LeftHandSideExpression:
        NewExpression
        CallExpression

    CallExpression:
        ImportCall

    Left-Hand-Side Expressions
    Static Semantics: AssignmentTargetType
    #sec-static-semantics-static-semantics-assignmenttargettype

    CallExpression :
        MemberExpressionArguments
        SuperCall
        ImportCall
        CallExpressionArguments
        CallExpressionTemplateLiteral

    1. Return invalid
negative:
    phase: parse
    type: ReferenceError
features: [dynamic-import]
---*/

$DONOTEVALUATE();

--import('')
