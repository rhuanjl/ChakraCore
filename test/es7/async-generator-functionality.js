//-------------------------------------------------------------------------------------------------------
// Copyright (C) Microsoft. All rights reserved.
// Licensed under the MIT license. See LICENSE.txt file in the project root for full license information.
//-------------------------------------------------------------------------------------------------------

const promises = [];

// async tests without a baseline by using an array of promises
function AddPromise(test, Msg, promise, result, shouldFail = false)
{
    const resultPromise = shouldFail ?
    promise.then(
        ()=>{throw new Error(`Test ${test} failed -  ${Msg}`);},
        (x)=>{if (! equal(result, x)) {throw new Error(`Test ${test} failed - ${Msg} - ${JSON.stringify(x)} should equal ${JSON.stringify(result)}`);}}
        ) :
    promise.then(
        (x)=>{if (! equal(result, x)) {throw new Error(`Test ${test} failed - ${Msg} - ${JSON.stringify(x)} should equal ${JSON.stringify(result)}`);}},
        ()=>{throw new Error(`Test ${test} failed -  ${Msg}`);}
    );
    promises.push(resultPromise);
}

function equal(expected, actual) {
    if (typeof expected === 'object')
    {
        return expected.value === actual.value && expected.done === actual.done;
    }
    return expected === actual;
}

const tests = [
    {
        name : "Basic functionality",
        body() {
            async function* agf () {}
            let gen = agf();
            AddPromise(this.name, "throw method on SuspendedStart", gen.throw("test value"), "test value", true);
            AddPromise(this.name, "throw method on completed", gen.throw("test value"), "test value", true);
            AddPromise(this.name, "next method on completed", gen.next("test value"), {done : true}, false);
            gen = agf();
            AddPromise(this.name, "next method on SuspendedStart", gen.next("test value"), {done : true});
            AddPromise(this.name, "throw method on completed", gen.throw("test value"), "test value", true);
            AddPromise(this.name, "next method on completed", gen.next("test value"), {done : true});
        }
    },
    {
        name : "Simple yield",
        body() {
            async function* agf (a) {
                for (let i = 0; i < 3; ++i) {
                    yield a + i;
                }
                return a;
            }
            let gen = agf(2);
            AddPromise(this.name, "yielded values 1", gen.next(), {value : 2, done : false});
            AddPromise(this.name, "yielded values 2", gen.next(), {value : 3, done : false});
            AddPromise(this.name, "yielded values 3", gen.next(), {value : 4, done : false});
            AddPromise(this.name, "yielded values 4", gen.next(), {value : 2, done : true});
            gen = agf(3);
            AddPromise(this.name, "yielded values 5", gen.next(), {value : 3, done : false});
            AddPromise(this.name, "yielded value with return", gen.return(), {done : true});
            AddPromise(this.name, "next after .return() call", gen.next(), {done : true});
        }
    },
    {
        name : "Yield and await",
        body() {
            async function* agf (a) {
                for (let i = 0; i < 3; ++i) {
                    yield a + await i;
                }
                return a;
            }
            const gen = agf(2);
            AddPromise(this.name, "yielded values with await 1", gen.next(), {value : 2, done : false});
            AddPromise(this.name, "yielded values with await 2", gen.next(), {value : 3, done : false});
            AddPromise(this.name, "yielded values with await 3", gen.next(), {value : 4, done : false});
            AddPromise(this.name, "yielded values with await 4", gen.next(), {value : 2, done : true});
        }
    },
    {
        name : "Yield and early return",
        body() {
            async function* agf (a) {
                for (let i = 0; i < 3; ++i) {
                    yield a;
                    return i;
                }
            }
            const gen = agf(2);
            AddPromise(this.name, "yielded value before return", gen.next(), {value : 2, done : false});
            AddPromise(this.name, "returned value", gen.next(), {value : 0, done : true});
            AddPromise(this.name, "value after return", gen.next(), {done : true});
            AddPromise(this.name, "value after return", gen.next(), {done : true});
        }
    },
    {
        name : "Yield*",
        body() {
            async function* agf (a) {
                yield* a;
            }
            const gen = agf([3,2,1,5]);
            AddPromise(this.name, "yield* from array 1", gen.next(), {value : 3, done : false});
            AddPromise(this.name, "yield* from array 2", gen.next(), {value : 2, done : false});
            AddPromise(this.name, "yield* from array 3", gen.next(), {value : 1, done : false});
            AddPromise(this.name, "yield* from array 4", gen.next(), {value : 5, done : false});
            AddPromise(this.name, "yield* from array 5", gen.next(), {done : true});
        }
    }
];


for(const test of tests) {
    test.body();
}

Promise.all(promises).then(()=>{print("pass")}, x => print (x));
