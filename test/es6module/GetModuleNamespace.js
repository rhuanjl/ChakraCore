//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

WScript.LoadScriptFile("..\\UnitTestFramework\\UnitTestFramework.js");

WScript.RegisterModuleSource("mod0.js", `
    export const export1 = 5;
    export default function export2 ()
    {
        return true;
    }
    export class export3
    {
        
    }
    export async function export4 ()
    {
        return await null;
    }
    export let export5 = "exporting";
    const notExported1 = "foo";
    let notExported2 = "bar";
    var notExported3 = 5;
`);

WScript.RegisterModuleSource("mod1.js",`
    export let export1 = 10;
    export default function export2 ()
    {
        return false;
    }
    export class export3
    {
    
    }
    export async function export4 ()
    {
        return await null;
    }
    export const export5 = "exported";
`);

let test1 = import("mod0.js").then(()=>{
    let mod0Namespace = WScript.GetModuleNamespace("mod0.js");
    assert.areEqual(mod0Namespace.export1, 5);
    assert.isTrue(mod0Namespace.default());
    assert.areEqual(typeof mod0Namespace.export3.constructor, "function");
    assert.areEqual(mod0Namespace.export4.constructor.name, "AsyncFunction");
    assert.areEqual(mod0Namespace.export5, "exporting");
});

let test2 = import("mod1.js").then(()=>{
    let mod1Namespace = WScript.GetModuleNamespace("mod1.js");
    assert.areEqual(mod1Namespace.export1, 10);
    assert.isFalse(mod1Namespace.default());
    assert.areEqual(typeof mod1Namespace.export3.constructor, "function");
    assert.areEqual(mod1Namespace.export4.constructor.name, "AsyncFunction");
});

assert.throws(()=>WScript.GetModuleNamespace("mod0.js"));
assert.throws(()=>WScript.GetModuleNamespace("mod3.js"));

Promise.all([test1,test2]).then(()=>print("pass")).catch(()=>print("fail"));