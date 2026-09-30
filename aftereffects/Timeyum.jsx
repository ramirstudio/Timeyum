/*
  Timeyum Timeshift for After Effects
  Costruisce sul livello selezionato un rig di copie con Motion Tile (wrap-around),
  Directional Blur ed espressioni, pilotato da un null "TY Controls".
  Installazione: File > Scripts > Install ScriptUI Panel, oppure copia il file in
  Scripts/ScriptUI Panels e aprilo da Window > Timeyum.jsx.
*/

var TY = {
    name: "Timeyum Timeshift",
    ctl: "TY Controls",
    ghosts: 2
};

// ---------------------------------------------------------------------------
// Espressioni
// ---------------------------------------------------------------------------

var SHAKE_JS = [
    'var C=thisComp.layer("TY Controls");',
    'function v(n){return C.effect(n)(1).value;}',
    'var on=v("Shake")>0?1:0,amt=v("Shake Amount")/100;',
    'function nz(s){var f=time*v("Shake Freq"),i=Math.floor(f),fr=f-i,sm=v("Shake Smooth")/100;',
    'fr=sm<0.001?0:clamp((fr-(1-sm))/sm,0,1);fr=fr*fr*(3-2*fr);',
    'var b=v("Shake Seed")*131+s*7919;seedRandom(b+i,true);var a=random(-1,1);',
    'seedRandom(b+i+1,true);var c=random(-1,1);return on*amt*(a+(c-a)*fr);}'
].join("\n");

var CALC = {
    "_Len": 'Math.max(0,v("Length")/100*(1+v("Shake Length")/100*nz(1)))',
    "_Ang": 'v("Angle")+v("Shake Angle")*nz(2)',
    "_Smear": 'clamp(v("Smear")/100*(1+v("Shake Smear")/100*nz(3)),0,1)',
    "_Timing": 'v("Timing Shift")+v("Shake Timing")*nz(4)',
    "_Roll": 'v("Roll")/100+v("Shake Roll")/100*nz(5)',
    "_WX": 'v("Weave X")*nz(6)',
    "_WY": 'v("Weave Y")*nz(7)'
};

function exprPrefix(N, i) {
    return [
        'var C=thisComp.layer("TY Controls");',
        'function v(n){return C.effect(n)(1).value;}',
        'var N=' + N + ',i=' + i + ',H=thisLayer.height,W=thisLayer.width;',
        'var L=v("_Len"),A=degreesToRadians(v("_Ang")),dx=Math.cos(A),dy=-Math.sin(A);',
        'var prof=v("Profile"),sym=v("Symmetric")>0,om=v("Opacity")/100;',
        'function weight(t){return prof==2?Math.exp(-v("Decay")*t):Math.max(0,1-v("Falloff")/100*t);}',
        'function pos(k){var t=(k+0.5)/N;return sym?(2*t-1):t;}',
        'function cam(k){var sa=clamp(v("Shutter Angle"),1,360),pd=clamp(v("Pulldown Angle"),1,359),ease=v("Claw Ease")/100;',
        'var ph=v("_Timing")+(k+0.5)/N*sa,cy=Math.floor(ph/360),q=ph-360*cy,rest=360-pd,u=clamp((q-rest)/pd,0,1);',
        'var e=(1-ease)*u+ease*(u-Math.sin(2*Math.PI*u)/(2*Math.PI));',
        'return [(cy==0&&q<rest)?0:1,(cy+e)*L*H];}',
        'function shift(k){return prof==3?cam(k)[1]:pos(k)*L*H;}',
        'function frac(k){if(prof==3)return cam(k)[0]/N;var S=0;for(var j=0;j<N;j++)S+=weight(Math.abs(pos(j)));',
        'return S>0?v("_Smear")*weight(Math.abs(pos(k)))/S:0;}',
        'function share(){var s=0;for(var j=0;j<N;j++)s+=frac(j);return s;}',
        'function ghostShare(){if(!(v("Ghost")>0))return 0;var g=0,d=v("Ghost Strength")/100;',
        'for(var k=0;k<' + TY.ghosts + ';k++)g+=d*Math.pow(1-v("Ghost Decay")/100,k);return g;}'
    ].join("\n");
}

function exprCenter(N, i, kind, g) {
    var s;
    if (kind == "base") s = "0";
    else if (kind == "ghost") s = g + '*v("Ghost Offset")/100*H';
    else s = "shift(i)";
    return exprPrefix(N, i) + "\nvar s=" + s + ';\n[W/2-s*dx-v("_WX"),H/2-s*dy-v("_WY")-v("_Roll")*H];';
}

function exprOpacity(N, i, kind, g) {
    var p = exprPrefix(N, i) + "\n";
    if (kind == "base") {
        return p + 'var f=v("Blend")==2?0:om*(share()+ghostShare());\nvalue*Math.max(0,1-f);';
    }
    if (kind == "ghost") {
        return p + 'value*om*(v("Ghost")>0?v("Ghost Strength")/100*Math.pow(1-v("Ghost Decay")/100,' + (g - 1) + '):0);';
    }
    return p + "value*om*frac(i);";
}

function exprBlurLen(N, i) {
    return exprPrefix(N, i) + '\nvar a=shift(i),b=shift(i<N-1?i+1:i-1);Math.abs(b-a)+0.001;';
}

function exprBlurDir() {
    return '-thisComp.layer("TY Controls").effect("_Ang")(1).value;';
}

// ---------------------------------------------------------------------------
// Costruzione
// ---------------------------------------------------------------------------

function fxParade(layer) { return layer.property("ADBE Effect Parade"); }

function addCtl(layer, type, name, def, items) {
    var fx = fxParade(layer).addProperty(type);
    fx.name = name;
    if (items) {
        fx.property(1).setPropertyParameters(items);
        fx = fxParade(layer).property(name);
    }
    if (def !== null && def !== undefined) fx.property(1).setValue(def);
    return fx;
}

function addTile(layer) {
    var fx = fxParade(layer).addProperty("ADBE Tile");
    fx.name = "TY Tile";
    return fx;
}

function buildRig(comp, src, N) {
    var ctl = comp.layers.addNull(comp.duration);
    ctl.name = TY.ctl;
    ctl.label = 11;

    var S = "ADBE Slider Control", A = "ADBE Angle Control", K = "ADBE Checkbox Control", D = "ADBE Dropdown Control";
    addCtl(ctl, S, "Opacity", 100);
    addCtl(ctl, D, "Profile", 1, ["Fade", "Exponential", "Camera (Timing Shift)"]);
    addCtl(ctl, S, "Length", 60);
    addCtl(ctl, A, "Angle", 90);
    addCtl(ctl, K, "Symmetric", 0);
    addCtl(ctl, S, "Smear", 35);
    addCtl(ctl, S, "Falloff", 55);
    addCtl(ctl, S, "Decay", 4);
    addCtl(ctl, S, "Timing Shift", 90);
    addCtl(ctl, S, "Shutter Angle", 180);
    addCtl(ctl, S, "Pulldown Angle", 180);
    addCtl(ctl, S, "Claw Ease", 100);
    addCtl(ctl, D, "Blend", 1, ["Exposure", "Add"]);
    addCtl(ctl, K, "Ghost", 0);
    addCtl(ctl, S, "Ghost Offset", 12);
    addCtl(ctl, S, "Ghost Strength", 25);
    addCtl(ctl, S, "Ghost Decay", 50);
    addCtl(ctl, S, "Roll", 0);
    addCtl(ctl, K, "Shake", 0);
    addCtl(ctl, S, "Shake Amount", 100);
    addCtl(ctl, S, "Shake Freq", 12);
    addCtl(ctl, S, "Shake Smooth", 30);
    addCtl(ctl, S, "Shake Seed", 1234);
    addCtl(ctl, S, "Shake Length", 35);
    addCtl(ctl, S, "Shake Smear", 25);
    addCtl(ctl, S, "Shake Angle", 0);
    addCtl(ctl, S, "Shake Timing", 20);
    addCtl(ctl, S, "Shake Roll", 0);
    addCtl(ctl, S, "Weave X", 0);
    addCtl(ctl, S, "Weave Y", 1.5);
    for (var key in CALC) {
        if (CALC.hasOwnProperty(key)) {
            var c = addCtl(ctl, S, key, 0);
            c.property(1).expression = SHAKE_JS + "\n" + CALC[key] + ";";
        }
    }

    var base = src;
    addTile(base);
    fxParade(base).property("TY Tile").property(1).expression = exprCenter(N, 0, "base");
    base.property("ADBE Transform Group").property("ADBE Opacity").expression = exprOpacity(N, 0, "base");

    var copies = [], i, g, d, tile, blur;
    for (i = 0; i < N; i++) {
        d = base.duplicate();
        d.name = "TY Copy " + (i + 1);
        d.blendingMode = BlendingMode.ADD;
        d.property("ADBE Transform Group").property("ADBE Opacity").expression = exprOpacity(N, i, "copy");
        fxParade(d).property("TY Tile").property(1).expression = exprCenter(N, i, "copy");
        blur = fxParade(d).addProperty("ADBE Motion Blur");
        blur.name = "TY Blur";
        blur.property(1).expression = exprBlurDir();
        blur.property(2).expression = exprBlurLen(N, i);
        copies.push(d);
    }
    for (g = 1; g <= TY.ghosts; g++) {
        d = base.duplicate();
        d.name = "TY Ghost " + g;
        d.blendingMode = BlendingMode.ADD;
        d.property("ADBE Transform Group").property("ADBE Opacity").expression = exprOpacity(N, 0, "ghost", g);
        fxParade(d).property("TY Tile").property(1).expression = exprCenter(N, 0, "ghost", g);
        copies.push(d);
    }
    for (i = 0; i < copies.length; i++) copies[i].shy = true;
    comp.hideShyLayers = true;
    ctl.moveToBeginning();
    ctl.selected = true;
    return ctl;
}

function run(N, precomp) {
    var comp = app.project.activeItem;
    if (!(comp instanceof CompItem)) { alert("Apri una composizione."); return; }
    if (comp.selectedLayers.length !== 1) { alert("Seleziona un solo livello."); return; }
    var layer = comp.selectedLayers[0];
    if (comp.layer(TY.ctl)) { alert("In questa composizione esiste gia' un rig Timeyum."); return; }
    if (!(layer instanceof AVLayer)) { alert("Il livello deve essere video, immagine o precomp."); return; }
    app.beginUndoGroup(TY.name);
    try {
        if (precomp) {
            var idx = layer.index;
            comp.layers.precompose([idx], layer.name + " TY", true);
            layer = comp.layer(idx);
        }
        buildRig(comp, layer, N);
    } catch (e) {
        alert("Timeyum: " + e.toString() + (e.line ? " (riga " + e.line + ")" : ""));
    }
    app.endUndoGroup();
}

function buildUI(thisObj) {
    var w = (thisObj instanceof Panel) ? thisObj : new Window("palette", TY.name, undefined, { resizeable: true });
    w.orientation = "column";
    w.alignChildren = "fill";
    w.margins = 12;
    var g = w.add("group");
    g.add("statictext", undefined, "Copie");
    var dd = g.add("dropdownlist", undefined, ["16", "24", "32", "48", "64"]);
    dd.selection = 2;
    var pre = w.add("checkbox", undefined, "Precomponi prima il livello");
    pre.value = true;
    var btn = w.add("button", undefined, "Applica al livello selezionato");
    btn.onClick = function () { run(parseInt(dd.selection.text, 10), pre.value); };
    w.layout.layout(true);
    if (w instanceof Window) w.show();
}

if (typeof TY_TEST === "undefined") { buildUI(this); }
