// Evaluates the After Effects expressions against a mock thisComp and checks energy/offsets.
const fs = require("fs"), vm = require("vm"), path = require("path");
const code = fs.readFileSync(path.join(__dirname, "..", "aftereffects", "Timeyum.jsx"), "utf8");
const ctx = { TY_TEST: true, console, Math };
vm.createContext(ctx);
vm.runInContext(code, ctx);
const T = vm.runInContext("({exprCenter,exprOpacity,exprBlurLen,exprBlurDir,SHAKE_JS,CALC})", ctx);

function makeEnv(params, time, W, H) {
  const p = Object.assign({
    Opacity: 100, Profile: 1, Length: 60, Angle: 90, Symmetric: 0, Smear: 35, Falloff: 55, Decay: 4,
    "Timing Shift": 90, "Shutter Angle": 180, "Pulldown Angle": 180, "Claw Ease": 100, Blend: 1,
    Ghost: 0, "Ghost Offset": 12, "Ghost Strength": 25, "Ghost Decay": 50, Roll: 0, Shake: 0,
    "Shake Amount": 100, "Shake Freq": 12, "Shake Smooth": 30, "Shake Seed": 1234, "Shake Length": 35,
    "Shake Smear": 25, "Shake Angle": 0, "Shake Timing": 20, "Shake Roll": 0, "Weave X": 0, "Weave Y": 1.5,
  }, params);
  const env = {
    time, Math,
    thisLayer: { width: W, height: H },
    clamp: (x, a, b) => Math.min(Math.max(x, a), b),
    degreesToRadians: (d) => d * Math.PI / 180,
    seedRandom(seed) { let s = (seed >>> 0) || 1; env._s = s; },
    random(a, b) { let s = env._s; s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; env._s = s; return a + (b - a) * (s / 4294967296); },
    value: 100,
  };
  const get = (n) => (n in p ? p[n] : env._calc[n]);
  env._calc = {};
  env.thisComp = { layer: () => ({ effect: (n) => (() => ({ value: get(n) })) }) };
  // effect(n)(1) must return an object with .value
  env.thisComp.layer = () => ({ effect: (n) => () => ({ get value() { return get(n); } }) });
  return env;
}
const run = (src, env) => new Function(...Object.keys(env), "return eval(" + JSON.stringify(src) + ")")(...Object.values(env));

function calcAll(params, time) {
  const env = makeEnv(params, time, 1920, 1080);
  for (const k of Object.keys(T.CALC)) env._calc[k] = run(T.SHAKE_JS + "\n" + T.CALC[k] + ";", env);
  return env;
}
function rig(params, time, N) {
  const env = calcAll(params, time);
  const base = run(T.exprOpacity(N, 0, "base"), env);
  let sum = base, offs = [], ops = [];
  for (let i = 0; i < N; i++) {
    const o = run(T.exprOpacity(N, i, "copy"), env); sum += o; ops.push(o);
    offs.push(run(T.exprCenter(N, i, "copy"), env));
  }
  return { base, sum, ops, offs, env };
}
let ok = true; const check = (c, m) => { if (!c) { ok = false; console.log("FAIL", m); } else console.log("ok  ", m); };

let r = rig({}, 0, 32);
check(Math.abs(r.sum - 100) < 1e-6, "fade/exposure: base + copies = 100 (" + r.sum.toFixed(6) + ")");
check(Math.abs(r.base - 65) < 1e-6, "fade: base opacity = 100*(1-0.35)");
check(r.ops[0] > r.ops[31], "fade: weight decreases toward the tip");
const yCenter = r.offs[31][1], expected = 540 + 0.6 * 1080 * 31.5 / 32;
check(Math.abs(yCenter - expected) < 1e-6 && Math.abs(r.offs[31][0] - 960) < 1e-6, "angle 90: streak goes up (tile center y +" + (yCenter - 540).toFixed(1) + ")");
r = rig({ Profile: 2, Decay: 5 }, 0, 32); check(Math.abs(r.sum - 100) < 1e-6, "exponential: energy conserved");
r = rig({ Profile: 3, "Timing Shift": 100, Length: 100 }, 0, 48); check(Math.abs(r.sum - 100) < 1e-6, "camera: energy conserved (" + r.sum.toFixed(4) + ")");
r = rig({ Profile: 3, "Timing Shift": 0 }, 0, 48); check(Math.abs(r.base - 100) < 1e-9, "camera, timing shift 0: no effect");
r = rig({ Symmetric: 1, Length: 40 }, 0, 32); check(Math.abs(r.sum - 100) < 1e-6 && r.offs[0][1] < 540 && r.offs[31][1] > 540, "symmetric: streak on both sides");
r = rig({ Blend: 2 }, 0, 32); check(Math.abs(r.base - 100) < 1e-9, "add mode: base untouched");
let a = rig({ Shake: 1, "Shake Weave Y": 3 }, 0.37, 16), b = rig({ Shake: 1 }, 0.37, 16);
check(a.base === b.base && a.env._calc._Len === b.env._calc._Len, "shake deterministic for the same time");
let c = rig({ Shake: 1 }, 0.9, 16); check(c.env._calc._Len !== a.env._calc._Len, "shake changes with time");
const env = calcAll({ Ghost: 1 }, 0); const g1 = run(T.exprOpacity(32, 0, "ghost", 1), env), g2 = run(T.exprOpacity(32, 0, "ghost", 2), env);
check(Math.abs(g1 - 25) < 1e-9 && Math.abs(g2 - 12.5) < 1e-9, "ghosts: 25% then 12.5%");
const bl = run(T.exprBlurLen(32, 5), makeEnv({}, 0, 1920, 1080)); void bl;
process.exit(ok ? 0 : 1);
