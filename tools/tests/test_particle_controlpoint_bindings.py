"""Live control-point assignments and authored property scripts/timelines."""
import json
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_control_points import write_particle
from test_particle_inherited_velocity import write_probe, read_velocity_color
from test_script_property_animation import animation
from test_text_effect_targets import base_scene


def write_binding_probe(root, case, candidate=False):
    if case.startswith('velocity_'):
        scene = write_probe(root, 'static', candidate)
        layer = scene['objects'][0]
        layer['instanceoverride']['controlpoint1'] = {'value': '0 0 0'}
        point = layer['instanceoverride']['controlpoint1']
        if case in ('velocity_timeline', 'velocity_partial'):
            point['animation'] = {'c0': [{'frame': 0, 'value': 0}, {'frame': 100, 'value': 200}],
                                  'options': {'fps': 10, 'length': 100, 'mode': 'single'}}
            if case == 'velocity_timeline':
                for channel in ('c1', 'c2'):
                    point['animation'][channel] = [{'frame': 0, 'value': 0}, {'frame': 100, 'value': 0}]
        elif case == 'velocity_property':
            point['script'] = 'export function update(v){v.x+=20*engine.frametime;return v;}'
        else:
            layer['visible'] = {'value': True, 'script': '''export function update(v){
                let p=thisLayer.instance.controlpoint1;
                p.x+=20*engine.frametime;thisLayer.instance.controlpoint1=p;return v;}'''}
        (root / 'scene.json').write_text(json.dumps(scene))
        return scene
    points = [{'id': i, 'flags': 0, 'offset': '20 10 0' if i == 1 else '0 0 0'} for i in range(8)]
    write_particle(root, points, emitter_point=int(case[-1]) if case in ('point0', 'point7') else 1)
    path = root / 'particles/probe.json'
    definition = json.loads(path.read_text())
    definition['emitter'][0]['rate'] = 0
    definition['operator'] = [{'name': 'angularmovement', 'drag': 0, 'force': '0 0 0'}]
    path.write_text(json.dumps(definition))
    layer = {'id': 1, 'name': 'Binding probe', 'origin': '100 90 0', 'particle': 'particles/probe.json'}
    init = ''
    change = ''
    if case == 'defaults':
        init = '''for(let i=0;i<8;i++){
            let p=thisLayer.instance['controlpoint'+i];
            if(!p || p.x!==3.4028234663852886e38) throw Error('missing sentinel '+i);
        }'''
    if case in ('init', 'point0', 'point7'):
        init = 'thisLayer.instance.controlpointINDEX=new Vec3(60,20,0);'.replace(
            'INDEX', case[-1] if case.startswith('point') else '1')
    if case in ('update', 'immediate'):
        change = 'thisLayer.instance.controlpoint1=new Vec3(60,20,0);'
    if case == 'snapshot':
        init = '''thisLayer.instance.controlpoint1=new Vec3(20,10,0);
            let a=thisLayer.instance.controlpoint1;
            thisLayer.instance.controlpoint1=new Vec3(60,20,0);
            let b=thisLayer.instance.controlpoint1;b.x=80;
            if(a.x!==20 || thisLayer.instance.controlpoint1.x!==60) throw Error('aliased copy');'''
    if case == 'reset':
        init = '''let original=thisLayer.instance.controlpoint1;
            thisLayer.instance.controlpoint1=new Vec3(60,20,0);
            thisLayer.instance.controlpoint1=original;'''
    if case in ('manual_world', 'manual_local'):
        definition['flags'] = 1
        if case == 'manual_world':
            definition['controlpoint'][1].update(flags=2, offset='140 100 0')
        path.write_text(json.dumps(definition))
        change = 'thisLayer.origin=new Vec3(130,90,0);'
    if case.startswith('property_'):
        body = 'return new Vec3(60,20,0);' if case == 'property_return' else 'v.x=60;v.y=20;'
        layer['instanceoverride'] = {'controlpoint1': {'value': '20 10 0',
                                                       'script': 'export function update(v){' + body + '}'}}
    if case == 'event_mutate':
        curve = {'c0': [{'frame': 0, 'value': 0}], 'c1': [{'frame': 0, 'value': 0}],
                 'c2': [{'frame': 0, 'value': 0}], 'relative': True,
                 'options': {'fps': 10, 'length': 10, 'mode': 'single', 'name': 'point-event',
                             'events': [{'frame': 1, 'name': 'probe'}]}}
        layer['instanceoverride'] = {'controlpoint1': {'value': '20 10 0', 'animation': curve,
            'script': '''export function animationEvent(event,v){
                thisLayer.instance.colorn=new Vec3(0,1,0);v.x=60;
            }'''}}
    if case.startswith('timeline_'):
        relative = case == 'timeline_relative'
        curve = animation(0 if relative else 20, 40 if relative else 60, name='point-motion')
        curve.update(c1=[{'frame': 0, 'value': 0 if relative else 10},
                         {'frame': 10, 'value': 10 if relative else 20}], relative=relative)
        if not relative and case != 'timeline_explicit_false':
            curve.pop('relative')
        if case != 'timeline_partial':
            curve['c2'] = [{'frame': 0, 'value': 0}, {'frame': 10, 'value': 0}]
        if case == 'timeline_paused':
            curve['options']['startpaused'] = True
            init = "if(thisLayer.getAnimation('point-motion')!==undefined) throw Error('wrong animation owner');thisLayer.instance.getAnimation('point-motion').setFrame(10);"
        layer['instanceoverride'] = {'controlpoint1': {'value': '20 10 0', 'animation': curve}}
    script = '''let elapsed=0;let emitted=false;
export function init(v){thisLayer.pause();INIT return v;}
export function update(v){elapsed+=engine.frametime;
    if(!emitted && elapsed>1.05){CHANGE thisLayer.emitParticles(1);emitted=true;console.log('CP_BINDING_OK');}
    return v;
}'''.replace('INIT', init).replace('CHANGE', change)
    if case == 'update':
        script = script.replace('CHANGE ', '').replace('elapsed+=engine.frametime;',
            'elapsed+=engine.frametime;if(elapsed>.5) thisLayer.instance.controlpoint1=new Vec3(60,20,0);')
    layer['visible'] = {'value': True, 'script': script}
    if case.startswith('child_'):
        # A missing root override must not replace this child's own offset.
        definition['controlpoint'][1]['offset'] = '40 20 0'
        definition['emitter'][0].update(rate=20, delay=.25)
        path.write_text(json.dumps(definition))
        (root / 'particles/root.json').write_text(json.dumps({
            'maxcount': 1, 'material': 'materials/probe.json',
            'operator': [{'name': 'angularmovement', 'drag': 0, 'force': '0 0 0'}],
            'controlpoint': points, 'children': [{'type': 'static', 'name': 'particles/probe.json'}]}))
        layer['particle'] = 'particles/root.json'
        layer.pop('visible')
        if case == 'child_authored':
            layer['instanceoverride'] = {'controlpoint1': '60 20 0'}
        if case in ('child_script', 'child_same', 'child_reset'):
            layer['visible'] = {'value': True, 'script': '''export function init(v){
                thisLayer.instance.controlpoint1=new Vec3(60,20,0);return v;}'''}
        if case == 'child_same':
            layer['instanceoverride'] = {'controlpoint1': '60 20 0'}
            layer['visible']['script'] = 'export function init(v){thisLayer.instance.controlpoint1=thisLayer.instance.controlpoint1;return v;}'
        if case == 'child_reset':
            layer['visible']['script'] = '''export function init(v){let p=thisLayer.instance.controlpoint1;
                thisLayer.instance.controlpoint1=new Vec3(60,20,0);thisLayer.instance.controlpoint1=p;return v;}'''
    if case == 'child_timeline':
        curve = animation(20, 60)
        curve.update(c1=[{'frame': 0, 'value': 10}, {'frame': 10, 'value': 20}],
                     c2=[{'frame': 0, 'value': 0}, {'frame': 10, 'value': 0}])
        layer['instanceoverride'] = {'controlpoint1': {'value': '20 10 0', 'animation': curve}}
        definition['emitter'][0]['delay'] = 1.1
        path.write_text(json.dumps(definition))
    if case == 'linked_fallback':
        definition['controlpoint'][1]['flags'] = 4
        path.write_text(json.dumps(definition))
        layer['instanceoverride'] = {'controlpoint1': '60 20 0'}
    scene = base_scene([layer])
    (root / 'scene.json').write_text(json.dumps(scene))
    (root / 'project.json').write_text(json.dumps({'type': 'scene', 'file': 'scene.json',
                                                 'title': 'Control-point binding probe'}))
    return scene


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics tests')
class ParticleControlPointBindings(unittest.TestCase):
    def check_position(self, case, x, y):
        with tempfile.TemporaryDirectory(prefix='lwe-cp-binding-' + case + '-') as directory:
            root = Path(directory)
            frame, _ = render_scene(self, root, write_binding_probe(root, case), frames=15)
            if case == 'event_mutate':
                self.assertGreater(frame.getextrema()[1][1], 200, 'Event must turn the particle green')
                self.assertEqual(frame.getextrema()[0][1], 0)
            box = frame.getbbox()
            self.assertIsNotNone(box, 'The particle must be emitted')
            self.assertAlmostEqual((box[0] + box[2]) / 2, x, delta=1)
            self.assertAlmostEqual((box[1] + box[3]) / 2, y, delta=1)

    def test_missing_points_expose_native_sentinel_without_replacing_offsets(self):
        self.check_position('defaults', 120, 80)

    def test_init_assignment_moves_emission(self):
        self.check_position('init', 160, 70)

    def test_update_assignment_moves_emission(self):
        self.check_position('update', 160, 70)

    def test_same_callback_emission_uses_previously_resolved_point(self):
        self.check_position('immediate', 120, 80)

    def test_first_control_point_is_writable(self):
        self.check_position('point0', 160, 70)

    def test_last_control_point_is_writable(self):
        self.check_position('point7', 160, 70)

    def test_getters_return_independent_snapshots(self):
        self.check_position('snapshot', 160, 70)

    def test_restoring_sentinel_restores_definition_offset(self):
        self.check_position('reset', 120, 80)

    def test_property_script_return_changes_position(self):
        self.check_position('property_return', 160, 70)

    def test_property_script_requires_a_returned_value(self):
        self.check_position('property_mutate', 120, 80)

    def test_absolute_timeline_changes_position(self):
        self.check_position('timeline_absolute', 160, 70)

    def test_relative_timeline_preserves_base_and_other_channels(self):
        self.check_position('timeline_relative', 160, 70)

    def test_incomplete_vector_timeline_keeps_the_base_position(self):
        self.check_position('timeline_partial', 120, 80)

    def test_present_relative_flag_uses_native_presence_semantics(self):
        self.check_position('timeline_explicit_false', 180, 60)

    def test_named_paused_timeline_can_be_seeked(self):
        self.check_position('timeline_paused', 160, 70)

    def test_children_keep_definition_offsets_without_override(self):
        self.check_position('child_default', 140, 70)

    def test_authored_root_override_does_not_replace_child_offsets(self):
        self.check_position('child_authored', 140, 70)

    def test_children_receive_scripted_root_override(self):
        self.check_position('child_script', 160, 70)

    def test_reassigning_the_same_value_still_updates_children(self):
        self.check_position('child_same', 160, 70)

    def test_children_recover_definition_offset_when_override_is_reset(self):
        self.check_position('child_reset', 140, 70)

    def test_unresolved_linked_point_keeps_its_definition_offset(self):
        self.check_position('linked_fallback', 120, 80)

    def test_manual_emission_preserves_fixed_world_point_when_layer_moves(self):
        self.check_position('manual_world', 140, 80)

    def test_manual_world_birth_uses_the_previously_resolved_layer_transform(self):
        self.check_position('manual_local', 120, 80)

    def test_animation_event_argument_mutation_requires_a_return(self):
        self.check_position('event_mutate', 120, 80)

    def test_complete_root_timeline_updates_child_points(self):
        self.check_position('child_timeline', 160, 70)

    def test_scripted_and_animated_points_supply_inherited_velocity(self):
        for case in ('velocity_direct', 'velocity_property', 'velocity_timeline'):
            with self.subTest(case=case), tempfile.TemporaryDirectory(prefix='lwe-cp-binding-' + case + '-') as directory:
                root = Path(directory)
                scene = write_binding_probe(root, case, candidate=True)
                frame, _ = render_scene(self, root, scene, frames=12)
                color = read_velocity_color(frame)
                self.assertIsNotNone(color)
                for actual, expected in zip(color, (153, 127, 127)):
                    self.assertAlmostEqual(actual, expected, delta=2)


if __name__ == '__main__':
    unittest.main()
