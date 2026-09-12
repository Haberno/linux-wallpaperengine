"""Opt-in runtime layer configuration, cloning, and destruction regressions."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class DynamicLayers(unittest.TestCase):
    def test_init_created_script_waits_for_all_initializers(self):
        script = """
let calls = 0;
export function init() {
    shared.childCalls = 0;
    thisScene.createLayer({ name: 'child', image: 'models/util/solidlayer.json',
        size: '10 10', visible: { value: false, script:
            'export function init() { shared.childInitialized=true; } export function update(value) { if (!shared.allInitialized) throw Error("update preceded another layer init"); ++shared.childCalls; return value; }' } });
    if (!shared.childInitialized || shared.childCalls !== 0)
        throw Error('init-created child updated synchronously');
}
export function update(value) {
    if (++calls === 1 && shared.childCalls !== 0) throw Error('startup update order changed');
    if (calls === 2) {
        if (shared.childCalls !== 1) throw Error('child missed startup update pass');
        console.log('INIT_CREATION_OK');
    }
    return value;
}
"""
        scene = {
            'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False},
            'objects': [{'id': 1, 'name': 'creator', 'solid': True,
                         'visible': {'value': True, 'script': script}},
                        {'id': 2, 'name': 'later', 'solid': True, 'visible': {'value': True,
                         'script': 'export function init() { shared.allInitialized=true; }'}}]}
        with tempfile.TemporaryDirectory(prefix='lwe-init-creation-') as directory:
            _, output = render_scene(self, Path(directory), scene)
        self.assertIn('INIT_CREATION_OK', output)

    def test_created_script_updates_once_then_can_destroy_itself(self):
        script = """
let calls = 0;
export function update(value) {
    ++calls;
    if (calls === 2) {
        shared.childCalls = 0;
        shared.childInitialized = false;
        thisScene.createLayer({ name: 'child', image: 'models/util/solidlayer.json',
            size: '10 10', visible: { value: false, script:
                'export function init() { shared.childInitialized=true; } export function update(value) { if (++shared.childCalls === 2) thisScene.destroyLayer(thisLayer); return value; }' } });
        if (!shared.childInitialized || shared.childCalls !== 0)
            throw Error('child update ran synchronously during creation');
    }
    if (calls === 3 && shared.childCalls !== 1)
        throw Error('child did not update exactly once in creation tick');
    if (calls === 4) {
        if (thisScene.getLayer('child')) throw Error('child self-destruction was not applied');
        console.log('CALLBACK_CREATION_OK');
    }
    return value;
}
"""
        scene = {
            'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False},
            'objects': [{'id': 1, 'name': 'creator', 'solid': True,
                         'visible': {'value': True, 'script': script}}]}
        with tempfile.TemporaryDirectory(prefix='lwe-callback-creation-') as directory:
            _, output = render_scene(self, Path(directory), scene)
        self.assertIn('CALLBACK_CREATION_OK', output)

    def test_updates_follow_registration_order(self):
        script = """
export function update(value) {
    shared.ordered.toFixed(2);
    return new Vec3(0, 1, 0);
}
"""
        image = {'image': 'models/util/solidlayer.json', 'origin': '160 90 0', 'size': '40 40'}
        scene = {
            'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False},
            'objects': [dict(image, id=900, name='controller', visible={
                'value': True, 'script': 'export function update(value) { shared.ordered=1; return value; }'}),
                dict(image, id=100, name='dependent', color={'value': '1 0 0', 'script': script})]}
        with tempfile.TemporaryDirectory(prefix='lwe-update-order-') as directory:
            frame, _ = render_scene(self, Path(directory), scene)
        self.assertEqual(frame.getpixel((160, 90)), (0, 255, 0))

    def test_config_vectors_and_nested_script_properties(self):
        script = """
export function init() {
    const config = {
        image: 'models/util/solidlayer.json', name: 'vector clone',
        origin: { value: new Vec3(160, 90, 0),
            script: 'export var scriptProperties = createScriptProperties(); export function init(value) { if (scriptProperties.probe.w !== 4 || Math.abs(scriptProperties.probe.x - 1e-8) > 1e-14) throw Error("nested vector lost"); return value; }',
            scriptproperties: { probe: new Vec4(1e-8, 2, 3, 4) } },
        size: new Vec2(40, 30), scale: new Vec3(1, 1, 1),
        angles: new Vec3(0, 0, Math.PI / 2), color: new Vec3(1, 0, 0)
    };
    const clone = thisScene.createLayer(config);
    if (!clone || clone.origin.x !== 160 || clone.origin.y !== 90 ||
        clone.scale.x !== 1 || Math.abs(clone.angles.z - 90) > .01)
        throw Error('layer vector configuration lost');
    if (!(config.origin.value instanceof Vec3) || config.origin.value.x !== 160 ||
        !(config.origin.scriptproperties.probe instanceof Vec4))
        throw Error('caller configuration mutated');
    console.log('CONFIG_VECTORS_OK');
}
"""
        scene = {
            'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False},
            'objects': [{'id': 1, 'name': 'controller', 'solid': True,
                         'visible': {'value': True, 'script': script}}]}
        with tempfile.TemporaryDirectory(prefix='lwe-config-vectors-') as directory:
            frame, output = render_scene(self, Path(directory), scene)
        self.assertIn('CONFIG_VECTORS_OK', output)
        self.assertEqual(frame.getpixel((160, 90)), (255, 0, 0))

    def test_saved_vectors_expire_after_layer_destruction(self):
        script = """
let origin, snapshot, copy, savedCopy, savedLength, checked = false;
export function init() {
    const config = thisScene.getInitialLayerConfig(thisScene.getLayer('template'));
    config.origin = { value: '160 90 0', script: 'export function init(value) { shared.borrowed = value; return value; }' };
    const clone = thisScene.createLayer(config);
    origin = shared.borrowed;
    snapshot = clone.origin;
    copy = origin.copy();
    savedCopy = origin.copy;
    savedLength = origin.length;
    thisScene.destroyLayer(clone);
}
export function update(value) {
    if (checked || engine.runtime < .2) return value;
    const attempts = [
        () => origin.x, () => { origin.x = 5; },
        () => origin.copy(), () => savedCopy.call(origin),
        () => savedLength.call(origin), () => new Vec2(origin),
        () => new Vec3(origin), () => new Vec3().equals(origin),
        () => new Vec3().add(origin)
    ];
    for (let i = 0; i < attempts.length; ++i) {
        let expired = false;
        try { attempts[i](); } catch (error) { expired = true; }
        if (!expired) throw Error('destroyed vector remained accessible at ' + i);
    }
    if (copy.x !== 160 || copy.y !== 90 || copy.copy().x !== 160)
        throw Error('independent vector copy was invalidated');
    if (snapshot.x !== 160 || snapshot.y !== 90)
        throw Error('layer getter snapshot was invalidated');
    checked = true;
    console.log('DELETED_VECTORS_OK');
    return value;
}
"""
        scene = {
            'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False},
            'objects': [{'id': 1, 'name': 'template', 'image': 'models/util/solidlayer.json',
                         'origin': '160 90 0', 'size': '40 40', 'visible': False},
                        {'id': 2, 'name': 'controller', 'solid': True,
                         'visible': {'value': True, 'script': script}}]}
        with tempfile.TemporaryDirectory(prefix='lwe-deleted-vectors-') as directory:
            _, output = render_scene(self, Path(directory), scene)
        self.assertIn('DELETED_VECTORS_OK', output)

    def test_particle_asset_handles_keep_instance_controls(self):
        script = '''
const asset = engine.registerAsset('particles/probe.json');
export function init() {
    const layer = thisScene.createLayer(asset);
    layer.origin = new Vec3(160, 90, 0);
    layer.instance.colorn = new Vec3(1, 0, 0);
    layer.instance.size = 2;
    if (layer.instance.colorn.y !== 0 || layer.instance.size !== 2)
        throw Error('instance assignment lost');
    console.log('PARTICLE_ASSET_OK');
}
'''
        with tempfile.TemporaryDirectory(prefix='lwe-dynamic-particle-') as directory:
            root = Path(directory)
            (root / 'particles').mkdir()
            (root / 'materials').mkdir()
            (root / 'materials/probe.json').write_text(json.dumps({'passes': [{
                'shader': 'genericparticle', 'textures': ['util/white'], 'blending': 'normal',
                'cullmode': 'nocull', 'depthtest': 'disabled', 'depthwrite': 'disabled'}]}))
            (root / 'particles/probe.json').write_text(json.dumps({
                'maxcount': 10, 'material': 'materials/probe.json',
                'emitter': [{'name': 'sphererandom', 'rate': 20, 'controlpoint': -1,
                             'distancemin': 0, 'distancemax': 0}],
                'initializer': [{'name': 'lifetimerandom', 'min': 10, 'max': 10},
                                {'name': 'sizerandom', 'min': 10, 'max': 10}]}))
            frame, output = render_scene(self, root, {
                'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
                'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                            'clearcolor': '0 0 0', 'camerafade': False},
                'objects': [{'id': 1, 'name': 'controller', 'solid': True,
                             'visible': {'value': True, 'script': script}}]})
            self.assertIn('PARTICLE_ASSET_OK', output)
            pixel = frame.getpixel((160, 90))
            self.assertGreaterEqual(pixel[0], 250)
            self.assertEqual(pixel[1:], (0, 0))

    def test_cloned_script_initializes_after_layer_registration(self):
        initializer = """
export function init() {
    if (thisScene.getLayer(thisLayer.name) !== thisLayer)
        throw Error('clone initialized before registration');
    if (thisLayer.pointsize !== 32) throw Error('clone initialized before text fields');
    console.log('READY_' + thisLayer.name);
}
"""
        creator = """
export function init() {
    const config = thisScene.getInitialLayerConfig(thisScene.getLayer('template'));
    config.name = 'clone';
    thisScene.createLayer(config);
}
"""
        scene = {
            'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False},
            'objects': [{'id': 1, 'name': 'template', 'text': 'Clone', 'pointsize': 32,
                         'origin': '160 90 0', 'visible': {'value': False, 'script': initializer}},
                        {'id': 2, 'name': 'creator', 'solid': True,
                         'visible': {'value': True, 'script': creator}}]}
        with tempfile.TemporaryDirectory(prefix='lwe-clone-init-') as directory:
            _, output = render_scene(self, Path(directory), scene)
        self.assertIn('READY_clone', output)

    def test_saved_animation_controller_expires_after_layer_destruction(self):
        script = """
let animation, requested = false;
export function init() {
    const clone = thisScene.createLayer(thisScene.getInitialLayerConfig(thisScene.getLayer('template')));
    animation = clone.getAnimation();
    thisScene.destroyLayer(clone);
}
export function update(value) {
    if (engine.runtime > .2) {
        if (animation.isPlaying() !== undefined) throw Error('destroyed animation still accesses layer');
        console.log('DELETED_ANIMATION_OK');
    }
    return value;
}
"""
        scene = {
            'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False},
            'objects': [{'id': 1, 'name': 'template', 'image': 'models/util/solidlayer.json',
                         'origin': '160 90 0', 'size': '40 40', 'visible': False},
                        {'id': 2, 'name': 'controller', 'solid': True,
                         'visible': {'value': True, 'script': script}}]}
        with tempfile.TemporaryDirectory(prefix='lwe-deleted-animation-') as directory:
            _, output = render_scene(self, Path(directory), scene)
        self.assertIn('DELETED_ANIMATION_OK', output)

    def test_initial_config_cloning_and_deferred_destruction(self):
        script = '''
let clone, requested = false;
export function init() {
    const template = thisScene.getLayer('template');
    template.origin = new Vec3(5, 6, 7);
    const config = thisScene.getInitialLayerConfig(template);
    if (config.origin !== '160 90 0') throw Error('initial config changed');
    config.name = 'cloned target';
    config.visible = true;
    clone = thisScene.createLayer(config);
    if (!clone || clone.name !== 'cloned target' || clone.origin.x !== 160)
        throw Error('clone configuration lost');
    if (thisScene.getLayer('cloned target') !== clone) throw Error('clone identity');
}
export function update(value) {
    if (!clone || engine.runtime < .1) return value;
    if (!requested) {
        if (!thisScene.destroyLayer(clone)) throw Error('destroy failed');
        if (thisScene.getLayer('cloned target') !== clone) throw Error('destroy was immediate');
        requested = true;
    } else {
        if (thisScene.getLayer('cloned target')) throw Error('destroy not applied');
        let invalid = false;
        try { clone.origin; } catch (e) { invalid = true; }
        if (!invalid) throw Error('deleted handle remained live');
        console.log('DYNAMIC_LAYERS_OK');
    }
    return value;
}
'''
        scene = {
            'camera': {'eye': '0 0 500', 'center': '0 0 0', 'up': '0 1 0'},
            'general': {'orthogonalprojection': {'width': 320, 'height': 180},
                        'clearcolor': '0 0 0', 'camerafade': False},
            'objects': [{'id': 1, 'name': 'template', 'image': 'models/util/solidlayer.json',
                         'origin': '160 90 0', 'size': '40 40', 'visible': False},
                        {'id': 2, 'name': 'controller', 'solid': True,
                         'visible': {'value': True, 'script': script}}]}
        with tempfile.TemporaryDirectory(prefix='lwe-dynamic-layers-') as directory:
            _, output = render_scene(self, Path(directory), scene)
        self.assertIn('DYNAMIC_LAYERS_OK', output)


if __name__ == '__main__':
    unittest.main()
