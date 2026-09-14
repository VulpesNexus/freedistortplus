//  fixtures.jsx -- the artwork and measurements every probe shares.
//
//  Sent once per Illustrator session by Install-EfdFixtures (tools/ai.ps1);
//  Illustrator keeps a DoJavaScript call's globals for the next one, so the
//  probes afterwards make short calls into EFD.*.
//
//  Probes work only in their own document, found by name, never in whatever
//  happens to be documents[0]: this machine's Illustrator is shared with other
//  work, and a probe that clears "the" document can clear somebody else's.
//
//  Fixtures are asymmetric on purpose. A rectangle cannot tell a bilinear map
//  from a mirrored one.
//
//  Pure ASCII on purpose: the file is read in the system codepage.

var EFD = (function () {

    var api = {};
    var TAB = String.fromCharCode(9);
    var NL = String.fromCharCode(10);

    api.DOC_NAME = 'efd-probe.ai';

    /** The probes' own document: opened if it is already open, created and
        saved under the temp folder otherwise. */
    api.doc = function () {
        for (var i = 0; i < app.documents.length; i++) {
            if (app.documents[i].name === api.DOC_NAME) {
                app.activeDocument = app.documents[i];
                app.coordinateSystem = CoordinateSystem.DOCUMENTCOORDINATESYSTEM;
                return app.documents[i];
            }
        }
        var d = app.documents.add(DocumentColorSpace.RGB, 800, 600);
        var folder = new Folder(Folder.temp.fsName + '/efd-probes');
        if (!folder.exists) { folder.create(); }
        d.saveAs(new File(folder.fsName + '/' + api.DOC_NAME));
        app.coordinateSystem = CoordinateSystem.DOCUMENTCOORDINATESYSTEM;
        return d;
    };

    api.clear = function () {
        var d = api.doc();
        api.made = {};
        d.selection = null;
        while (d.pageItems.length > 0) {
            try { d.pageItems[0].locked = false; d.pageItems[0].hidden = false; } catch (e) {}
            d.pageItems[0].remove();
        }
        return d;
    };

    api.send = function (selector, args) {
        return app.sendScriptMessage('EnhancedFreeDistort', selector, args || '');
    };

    api.rgb = function (r, g, b) { var c = new RGBColor(); c.red = r; c.green = g; c.blue = b; return c; };

    /** Objects made by the fixtures below, by name. Within one DoJavaScript
        call, once page items have been removed, items added afterwards are
        missing from every collection until the next call -- pageItems,
        pathItems and getByName alike -- so a fixture built right after clear()
        cannot be found by name in that same call. This registry is the
        fallback for that case only: across calls, objects are found by name
        in the collections, because a held reference is resolved by position
        and quietly changes meaning when the stacking order does. */
    api.made = {};

    api.register = function (name, o) {
        o.name = name;
        api.made[name] = o;
        return name;
    };

    api.named = function (name) {
        var d = api.doc();
        for (var i = 0; i < d.pageItems.length; i++) {
            if (d.pageItems[i].name === name) { return d.pageItems[i]; }
        }
        var o = api.made[name];
        if (o) {
            try { if (o.name === name) { return o; } } catch (e) {}
        }
        return null;
    };

    api.selectOnly = function (name) {
        var d = api.doc();
        d.selection = null;
        var o = api.named(name);
        if (o) { o.selected = true; }
        return o ? 'selected ' + name : 'no ' + name;
    };

    api.paint = function (p) {
        p.filled = true;
        p.fillColor = api.rgb(200, 40, 40);
        p.stroked = false;
        return p;
    };

    // ---- fixtures ---------------------------------------------------------

    api.pentagon = function (name, dx, dy) {
        dx = dx || 0; dy = dy || 0;
        var p = api.doc().pathItems.add();
        p.setEntirePath([[100 + dx, 100 + dy], [300 + dx, 120 + dy], [340 + dx, 260 + dy], [180 + dx, 330 + dy], [90 + dx, 240 + dy]]);
        p.closed = true;
        api.paint(p);
        api.register(name, p);
        return name;
    };

    /** A closed serpentine through a 6 by 5 grid of anchors, bounds
        100..400 by 100..300. Interior anchors are what separate a bilinear map
        from a homography. */
    api.grid = function (name) {
        var xs = [100, 150, 210, 260, 330, 400], ys = [100, 140, 190, 250, 300];
        var pts = [];
        for (var j = 0; j < ys.length; j++) {
            for (var k = 0; k < xs.length; k++) {
                var i = (j % 2 === 0) ? k : (xs.length - 1 - k);
                pts.push([xs[i], ys[j]]);
            }
        }
        var p = api.doc().pathItems.add();
        p.setEntirePath(pts);
        p.closed = true;
        api.paint(p);
        api.register(name, p);
        return name;
    };

    /** Five corner points with independent handles, so handles are mapped
        where anchors are not. Its geometric bounds are not its control
        polygon's bounds. */
    api.curves = function (name) {
        var p = api.doc().pathItems.add();
        var a = [[100, 200], [220, 300], [400, 260], [330, 100], [160, 120]];
        var L = [[90, 150], [150, 300], [380, 300], [390, 110], [230, 90]];
        var R = [[110, 250], [290, 300], [420, 220], [270, 90], [120, 140]];
        for (var q = 0; q < a.length; q++) {
            var pp = p.pathPoints.add();
            pp.anchor = a[q]; pp.leftDirection = L[q]; pp.rightDirection = R[q];
            pp.pointType = PointType.CORNER;
        }
        p.closed = true;
        api.paint(p);
        api.register(name, p);
        return name;
    };

    api.stroked = function (name) {
        api.pentagon(name);
        var p = api.named(name);
        p.stroked = true;
        p.strokeWidth = 20;
        p.strokeColor = api.rgb(20, 20, 20);
        return name;
    };

    api.pointText = function (name) {
        var t = api.doc().textFrames.add();
        t.contents = 'Distort';
        t.textRange.characterAttributes.size = 72;
        t.position = [120, 400];
        api.register(name, t);
        return name;
    };

    api.compound = function (name) {
        var d = api.doc();
        var c = d.compoundPathItems.add();
        var outer = c.pathItems.add();
        outer.setEntirePath([[100, 100], [340, 110], [330, 320], [90, 300]]);
        outer.closed = true;
        var inner = c.pathItems.add();
        inner.setEntirePath([[160, 160], [260, 170], [250, 250], [170, 240]]);
        inner.closed = true;
        outer.filled = true; outer.fillColor = api.rgb(40, 120, 200);
        api.register(name, c);
        return name;
    };

    api.group = function (name) {
        var d = api.doc();
        var g = d.groupItems.add();
        api.pentagon(name + '-a');
        api.pentagon(name + '-b', 180, 60);
        api.named(name + '-b').move(g, ElementPlacement.PLACEATEND);
        api.named(name + '-a').move(g, ElementPlacement.PLACEATEND);
        api.register(name, g);
        return name;
    };

    /** A clip path larger than what it clips, so the clip's box and the
        union of the children are different rectangles. */
    api.clipGroup = function (name) {
        var d = api.doc();
        var g = d.groupItems.add();
        api.pentagon(name + '-art');
        var clip = d.pathItems.rectangle(360, 60, 330, 300);   // top, left, width, height
        clip.name = name + '-clip';
        clip.move(g, ElementPlacement.PLACEATBEGINNING);
        api.named(name + '-art').move(g, ElementPlacement.PLACEATEND);
        g.clipped = true;
        api.register(name, g);
        return name;
    };

    api.areaText = function (name) {
        var d = api.doc();
        var frame = d.pathItems.rectangle(420, 100, 260, 160);
        var t = d.textFrames.areaText(frame);
        t.contents = 'Enhanced Free Distort keeps type live across several lines of text';
        t.textRange.characterAttributes.size = 24;
        api.register(name, t);
        return name;
    };

    api.symbolInstance = function (name) {
        var d = api.doc();
        api.pentagon(name + '-master');
        var master = api.named(name + '-master');
        var s = d.symbols.add(master);
        master.remove();
        var inst = d.symbolItems.add(s);
        inst.position = [120, 380];
        api.register(name, inst);
        return name;
    };

    api.raster = function (name) {
        var d = api.doc();
        api.pentagon(name + '-src');
        var src = api.named(name + '-src');
        var opts = new RasterizeOptions();
        opts.resolution = 72;
        opts.transparency = true;
        var r = d.rasterize(src, src.geometricBounds, opts);
        api.register(name, r);
        try { src.remove(); } catch (e) {}
        return name;
    };

    // ---- measurement ------------------------------------------------------

    api.f = function (n) { return String(Math.round(n * 1e9) / 1e9); };

    api.bounds = function (name) {
        var o = api.named(name);
        if (!o) { return 'missing'; }
        var g = o.geometricBounds, v = o.visibleBounds;
        return [api.f(g[0]), api.f(g[1]), api.f(g[2]), api.f(g[3])].join(',') + ';' +
               [api.f(v[0]), api.f(v[1]), api.f(v[2]), api.f(v[3])].join(',');
    };

    function dumpPath(tag, item, rows) {
        for (var i = 0; i < item.pathPoints.length; i++) {
            var pp = item.pathPoints[i];
            rows.push([tag, i, api.f(pp.anchor[0]), api.f(pp.anchor[1]),
                       api.f(pp.leftDirection[0]), api.f(pp.leftDirection[1]),
                       api.f(pp.rightDirection[0]), api.f(pp.rightDirection[1])].join(TAB));
        }
    }

    function walk(tag, item, rows) {
        if (item.typename === 'PathItem') { dumpPath(tag, item, rows); return; }
        if (item.typename === 'CompoundPathItem') {
            for (var m = 0; m < item.pathItems.length; m++) { walk(tag, item.pathItems[m], rows); }
            return;
        }
        if (item.pageItems) {
            for (var k = 0; k < item.pageItems.length; k++) { walk(tag, item.pageItems[k], rows); }
        }
    }

    /** Anchors and handles of the named object (S rows), and of a copy of it
        with its appearance expanded (R rows) -- which is what the effect drew.
        The copy is removed again. */
    api.sourceAndResult = function (name) {
        var d = api.doc();
        var o = api.named(name);
        var rows = [];
        app.redraw();
        walk('S', o, rows);
        var copy = o.duplicate();
        copy.name = name + '-expanded';
        d.selection = null;
        copy.selected = true;
        app.redraw();
        app.executeMenuCommand('expandStyle');
        app.redraw();
        var expanded = d.selection[0];
        walk('R', expanded, rows);
        expanded.remove();
        // Leave the original selected, as it was: the next bridge call acts on
        // the selection, and an empty one answers "No selection." to all of it.
        d.selection = null;
        o.selected = true;
        return rows.join(NL);
    };

    return api;
})();
'fixtures installed';
