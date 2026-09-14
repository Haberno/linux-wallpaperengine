"""Control-point angle values, property callbacks and native rate-cache refreshes."""
import json
import itertools
import math
import os
from pathlib import Path
import tempfile
import unittest

from scene_render import render_scene
from test_particle_turbulence_time import write_time_probe, read_time_sample, verify_time_sample
from test_script_property_animation import animation


API_CASES = ('defaults', 'authored', 'scalar', 'null', 'copy', 'reset',
             'init_mutate', 'init_return', 'property_mutate', 'property_return',
             'timeline_absolute', 'timeline_relative', 'timeline_false', 'timeline_partial', 'event_mutate')
CACHE_CASES = tuple('refresh'+str(i) for i in range(8))+('refresh_before_rate','refresh_timeline')
CASES = API_CASES+CACHE_CASES


def time_case(case):
    return 'refresh_controlpoint0' if case in CACHE_CASES else 'zero'


def write_angle_probe(root, case, candidate=False):
    scene = write_time_probe(root,time_case(case),candidate)
    layer=scene['objects'][0]
    settings=layer['instanceoverride']
    name='controlpointangle1'
    init=''
    check='true'
    sentinel='3.4028234663852886e38'
    if case=='defaults':
        check="[0,1,2,3,4,5,6,7].every(i=>p['controlpointangle'+i]&&p['controlpointangle'+i].x==="+sentinel+")"
    if case=='authored':
        for i in range(8):settings['controlpointangle'+str(i)] = f'{i+.25} {i+.5} {i+.75}'
        check="[0,1,2,3,4,5,6,7].every(i=>equal(p['controlpointangle'+i],i+.25,i+.5,i+.75))"
    if case=='scalar':settings[name]=.25;check='equal(p.controlpointangle1,.25,.25,.25)'
    if case=='null':settings[name]=None;check='p.controlpointangle1&&p.controlpointangle1.x==='+sentinel
    if case=='copy':
        init='''p.controlpointangle1=new Vec3(.125,.25,.5);let a=p.controlpointangle1;
 p.controlpointangle1=new Vec3(.5,.75,1);let b=p.controlpointangle1;
 if(b)b.x=2;shared.copyOK=equal(a,.125,.25,.5)&&equal(p.controlpointangle1,.5,.75,1);'''
        check='shared.copyOK===true'
    if case=='reset':
        init='let original=p.controlpointangle1;p.controlpointangle1=new Vec3(.5,.75,1);p.controlpointangle1=original;'
        check='p.controlpointangle1&&p.controlpointangle1.x==='+sentinel
    if case in ('init_mutate','init_return','property_mutate','property_return'):
        hook='init' if case.startswith('init_') else 'update'
        returned=case.endswith('_return')
        body='return new Vec3(.5,.75,1);' if returned else 'v.x=.5;v.y=.75;v.z=1;'
        settings[name]=dict(value='.125 .25 .5',script='export function '+hook+'(v){'+body+'}')
        check='equal(p.controlpointangle1,'+('.5,.75,1)' if returned else '.125,.25,.5)')
    if case.startswith('timeline_') or case=='event_mutate':
        curve=animation(.125,.5,name='angle-motion')
        curve.update(c1=[dict(frame=0,value=.25),dict(frame=10,value=.75)],
                     c2=[dict(frame=0,value=.5),dict(frame=10,value=1)])
        if case in ('timeline_relative','timeline_false'):curve['relative']=case=='timeline_relative'
        else:curve.pop('relative',None)
        curve['options']['startpaused']=True
        settings[name]=dict(value='.125 .25 .5',animation=curve)
        if case=='timeline_partial':curve.pop('c2')
        init="let a=p.getAnimation('angle-motion');if(a)a.setFrame(10);shared.ownerOK=thisLayer.getAnimation('angle-motion')===undefined;"
        check='shared.ownerOK&&equal(p.controlpointangle1,'+('.125,.25,.5)' if case=='timeline_partial' else '.625,1,1.5)' if case in ('timeline_relative','timeline_false') else '.5,.75,1)')
        if case=='event_mutate':
            curve['options'].update(startpaused=False,events=[dict(frame=1,name='probe')])
            for key,value in [('c0',.125),('c1',.25),('c2',.5)]:curve[key]=[dict(frame=0,value=value),dict(frame=10,value=value)]
            settings[name]['script']='export function animationEvent(e,v){shared.eventOK=true;v.x=2;}'
            init='';check='shared.eventOK&&equal(p.controlpointangle1,.125,.25,.5)'
    script=layer['visible']['script']
    prefix='''function equal(v,x,y,z){return v&&Math.abs(v.x-x)<1e-5&&Math.abs(v.y-y)<1e-5&&Math.abs(v.z-z)<1e-5;}
'''
    script=script.replace('export function init(v){','export function init(v){let p=thisLayer.instance;'+init)
    script=script.replace('shared.noiseTime=engine.runtime;','let p=thisLayer.instance;shared.angleOK=!!('+check+');shared.noiseTime=engine.runtime;')
    if case in CACHE_CASES:
        index=7 if case=='refresh_timeline' else 0 if case=='refresh_before_rate' else int(case[-1])
        assignment=f'thisLayer.instance.controlpointangle{index}=thisLayer.instance.controlpointangle{index};'
        if case=='refresh_timeline':
            settings['controlpointangle7']=dict(value='0 0 0',animation=dict(c0=[dict(frame=0,value=0)],c1=[dict(frame=0,value=0)],c2=[dict(frame=0,value=0)],options=dict(fps=10,length=10,startpaused=True)))
            scene['objects'][1]['instanceoverride']['controlpointangle7']=json.loads(json.dumps(settings['controlpointangle7']))
            assignment=''
        script=script.replace('thisLayer.instance.controlpoint0=new Vec3(0,0,0);',assignment)
        if case=='refresh_before_rate':script=script.replace('thisLayer.instance.rate=rate;'+assignment,assignment+'thisLayer.instance.rate=rate;')
        script=script.replace('shared.angleOK=!!(true);',f'shared.angleOK=thisLayer.instance.controlpointangle{index}!==undefined;')
    if case=='refresh_timeline':
        # Native continuous refresh clamps a zero rate; stop subsequent forces
        # after the measured step without introducing a pre-step dirty setter.
        script=script.replace('ticks++;','ticks++;if(steps>0)thisLayer.instance.speed=0;')
        clock_script=scene['objects'][1]['visible']['script']
        scene['objects'][1]['visible']['script']=clock_script.replace('ticks++;','ticks++;if(steps>0)thisLayer.instance.speed=0;')
    layer['visible']['script']=prefix+script
    scene['objects'][2]['color']['script']=scene['objects'][2]['color']['script'].replace('&&shared.noiseSteps==1?', '&&shared.noiseSteps==1&&shared.angleOK?')
    (root/'scene.json').write_text(json.dumps(scene))
    return scene


def verify_angle_sample(frame,case,candidate=False):
    markers=(frame.getpixel((300,10)),frame.getpixel((300,170)))
    assert (0,255,0) in markers,('Angle API/callback or step synchronization failed',case,markers)
    sample=read_time_sample(frame,time_case(case))
    if case in CACHE_CASES:verify_time_sample(time_case(case),sample,requested_clock=candidate)
    return sample


from test_particle_inherited_velocity import write_probe as write_velocity_probe, read_velocity_color
from test_particle_control_points import write_particle
from test_text_effect_targets import base_scene

ORIENTATION_CASES = ('zero','radians','degrees','mixed','definition','script','world',
    'layer_rotated','layer_world','cp0_local','cp0_world','reset_after','immediate',
    'child_authored','child_script','child_same','child_cross','child_alpha','child_rate','child_linked','child_linked_same','child_linked_emitter','child_linked_initializer','child_linked_operator','child_cross_timeline','cp0_omitted_world','cp0_null_world','cp7_negative','linked_fallback')

def write_orientation_probe(root,case):
 variant=case
 case={'cp0_omitted_world':'cp0_world','cp0_null_world':'cp0_world','cp7_negative':'radians'}.get(case,case)
 points=[dict(id=i,flags=0,offset='20 10 0' if i==1 else '0 0 0') for i in range(8)]
 write_particle(root,points,emitter_point=1)
 p=root/'particles/probe.json';d=json.loads(p.read_text());d['emitter'][0].update(name='boxrandom',rate=0,origin='30 10 20',distancemin='20 0 0',distancemax='20 0 0')
 d['operator']=[dict(name='angularmovement',drag=0,force='0 0 0')]
 value='0 0 0' if case=='zero' else '0 0 '+str(math.pi/2) if case!='degrees' else '0 0 90'
 layer=dict(id=1,name='Angle orientation',origin='160 90 0',particle='particles/probe.json',instanceoverride=dict(controlpointangle1=value),visible=dict(value=True,script='''let t=0,done=false;export function update(v){t+=engine.frametime;if(t>1&&!done){thisLayer.emitParticles(1);done=true;}return v;}'''))
 if case=='linked_fallback':d['controlpoint'][1]['flags']=4
 if case=='definition':
  d['controlpoint'][1]['angles']=value;layer.pop('instanceoverride')
 if case=='script':
  layer.pop('instanceoverride');layer['visible']['script']='export function init(v){thisLayer.instance.controlpointangle1=new Vec3(0,0,Math.PI/2);return v;}'+layer['visible']['script']
 if case in ('mixed','world','layer_rotated','layer_world','cp0_local','cp0_world','reset_after','immediate'):
  if case=='mixed':
   layer['instanceoverride']['controlpointangle1']='.4 .7 .9'
   d['emitter'][0].update(distancemin='20 13 17',distancemax='20 13 17',directions='1 1 1')
  if case in ('world','layer_world'):d['controlpoint'][1].update(flags=2,offset='180 100 0')
  if case in ('layer_rotated','layer_world'):layer.update(angles='0 0 .6',scale='2 1 1')
  if case=='layer_rotated':layer['origin']='160 70 0'
  if case.startswith('cp0_'):
   d['emitter'][0]['controlpoint']=0
   d['controlpoint'][0]['offset']='20 10 0'
   layer['instanceoverride']={'controlpointangle0':value}
   if case=='cp0_world':d['flags']=1
  if case in ('reset_after','immediate'):
   layer.pop('instanceoverride')
   if case=='reset_after':
    layer['visible']['script']='let original;export function init(v){original=thisLayer.instance.controlpointangle1;thisLayer.instance.controlpointangle1=new Vec3(0,0,Math.PI/2);return v;}'+layer['visible']['script'].replace('t+=engine.frametime;', 't+=engine.frametime;if(t>.5)thisLayer.instance.controlpointangle1=original;')
   else:layer['visible']['script']=layer['visible']['script'].replace('thisLayer.emitParticles(1);','thisLayer.instance.controlpointangle1=new Vec3(0,0,Math.PI/2);thisLayer.emitParticles(1);')
 if case.startswith('child_'):
  d['emitter'][0].update(rate=20,delay=.75)
  parent={'maxcount':1,'material':'materials/probe.json','controlpoint':points,'operator':[dict(name='angularmovement',drag=0,force='0 0 0')],'children':[dict(type='static',name='particles/probe.json')]}
  layer['particle']='particles/root.json';layer.pop('visible')
  if case in ('child_script','child_same'):
   if case=='child_script':layer.pop('instanceoverride')
   body='thisLayer.instance.controlpointangle1=new Vec3(0,0,Math.PI/2);' if case=='child_script' else 'thisLayer.instance.controlpointangle1=thisLayer.instance.controlpointangle1;'
   layer['visible']=dict(value=True,script='export function init(v){'+body+'return v;}')
  if case in ('child_cross','child_alpha','child_rate'):
   field={'child_cross':'controlpointangle7','child_alpha':'alpha','child_rate':'rate'}[case]
   layer['visible']=dict(value=True,script='export function init(v){thisLayer.instance.'+field+'=thisLayer.instance.'+field+';return v;}')
  if case=='child_cross_timeline':
   layer['instanceoverride']['controlpointangle7']=dict(value='0 0 0',animation=dict(c0=[dict(frame=0,value=0)],c1=[dict(frame=0,value=0)],c2=[dict(frame=0,value=0)],options=dict(fps=10,length=10,startpaused=True)))
  if case.startswith('child_linked'):

   d['controlpoint'][1].update(flags=4,parentcontrolpoint=1)
   if case=='child_linked_same':layer['visible']=dict(value=True,script='export function init(v){thisLayer.instance.controlpointangle1=thisLayer.instance.controlpointangle1;return v;}')
   if case=='child_linked_emitter':parent['emitter']=[dict(name='boxrandom',rate=0,controlpoint=1)]
   if case=='child_linked_initializer':parent['initializer']=[dict(name='inheritcontrolpointvelocity',controlpoint=1,min=0,max=0)]
   if case=='child_linked_operator':parent['operator'].append(dict(name='controlpointattract',controlpoint=1,scale=0))
  (root/'particles/root.json').write_text(json.dumps(parent))
 if variant=='cp0_omitted_world':d['emitter'][0].pop('controlpoint')
 if variant=='cp0_null_world':d['emitter'][0]['controlpoint']=None
 if variant=='cp7_negative':
  d['emitter'][0]['controlpoint']=-1;d['controlpoint'][7]['offset']='20 10 0'
  layer['instanceoverride']={'controlpointangle7':value}
 p.write_text(json.dumps(d));(root/'scene.json').write_text(json.dumps(base_scene([layer])));(root/'project.json').write_text(json.dumps(dict(type='scene',file='scene.json',title='Angle orientation')))
 return base_scene([layer])


def orientation_centers(case):
    # Native 14022bd40 uses Rz*Ry*Rx; random box signs are independent.
    # Compare the finite support, without assuming matching random seeds.
    def rotate(v,angles):
        x,y,z=v
        a,b,c=angles
        y,z=math.cos(a)*y-math.sin(a)*z,math.sin(a)*y+math.cos(a)*z
        x,z=math.cos(b)*x+math.sin(b)*z,-math.sin(b)*x+math.cos(b)*z
        return (math.cos(c)*x-math.sin(c)*y,math.sin(c)*x+math.cos(c)*y,z)
    unrotated=case in ('zero','definition','cp0_local','immediate','child_authored','child_rate','child_linked','child_linked_same','linked_fallback')
    angles=(0,0,0) if unrotated else (.4,.7,.9) if case=='mixed' else (0,0,90 if case=='degrees' else math.pi/2)
    extent=(20,13,17) if case=='mixed' else (20,0,0)
    centers=[]
    for signs in itertools.product((-1,1),repeat=3 if case=='mixed' else 1):
        displacement=rotate(tuple(extent[i]*(signs[i] if len(signs)>1 else signs[0]) for i in range(3)),angles)
        if case=='layer_world':
            origin=rotate((60,10,20),(0,0,.6))
            point=(180+origin[0]+displacement[0],100+origin[1]+displacement[1])
        elif case=='layer_rotated':
            local=(2*(50+displacement[0]),20+displacement[1],20+displacement[2])
            delta=rotate(local,(0,0,.6));point=(160+delta[0],70+delta[1])
        else:point=(210+displacement[0],110+displacement[1])
        centers.append((point[0],180-point[1]))
    return centers


def verify_orientation_sample(frame,case):
    box=frame.getbbox()
    assert box is not None,('No emitted particle',case)
    center=((box[0]+box[2])/2,(box[1]+box[3])/2)
    expected=orientation_centers(case)
    error=min(max(abs(x-a),abs(y-b)) for x,y in [center] for a,b in expected)
    assert error<=1,(case,center,expected,error)
    return dict(center=center,expected=expected,error=error)


SPHERE_CASES = ('sphere_zero','sphere_rotated','sphere_local_zero','sphere_world_zero')


def write_sphere_probe(root,case,candidate=False):
    scene=write_velocity_probe(root,'static',candidate)
    layer=scene['objects'][0]
    point=0 if case in ('sphere_local_zero','sphere_world_zero') else 1
    angle=0 if case=='sphere_zero' else math.pi/4
    layer['instanceoverride']={'controlpointangle'+str(point):f'0 0 {angle}'}
    path=root/'particles/probe.json'
    definition=json.loads(path.read_text())
    definition['flags']=1 if case=='sphere_world_zero' else 0
    definition['controlpoint']=[dict(id=i,flags=0,offset='0 0 0') for i in range(8)]
    definition['emitter'][0].update(controlpoint=point,directions='1 0 0',speedmin=40,speedmax=40)
    definition['initializer'].pop()
    path.write_text(json.dumps(definition))
    (root/'scene.json').write_text(json.dumps(scene))
    return scene


def verify_sphere_sample(frame,case):
    angle=0 if case in ('sphere_zero','sphere_local_zero') else math.pi/4
    velocity=(40*math.cos(angle),40*math.sin(angle),0)
    expected=[tuple(255*(.5+sign*v/200) for v in velocity) for sign in (-1,1)]
    rgb=read_velocity_color(frame)
    assert rgb is not None,('No velocity readback',case)
    error=min(max(abs(a-b) for a,b in zip(rgb,target)) for target in expected)
    assert error<=1,(case,rgb,expected,error)
    return dict(rgb=rgb,expected=expected,error=error)


@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'),'Set LWE_TEST_BINARY for graphics integration tests')
class ParticleControlPointAngles(unittest.TestCase):
    def check_cases(self,cases):
        for case in cases:
            with self.subTest(case=case),tempfile.TemporaryDirectory(prefix='lwe-cp-angle-'+case+'-') as directory:
                root=Path(directory)
                frame,_=render_scene(self,root,write_angle_probe(root,case,candidate=True),frames=60,fps=60)
                verify_angle_sample(frame,case,candidate=True)

    def test_all_angle_fields_keep_native_defaults_and_authored_values(self):
        self.check_cases(['defaults','authored','scalar','null'])

    def test_angle_getters_are_copies_and_sentinel_can_be_restored(self):
        self.check_cases(['copy','reset'])

    def test_angle_property_callbacks_require_returned_vectors(self):
        self.check_cases(['init_mutate','init_return','property_mutate','property_return','event_mutate'])

    def test_complete_angle_timelines_use_instance_scope_and_native_relative_flag(self):
        self.check_cases(['timeline_absolute','timeline_relative','timeline_false','timeline_partial'])

    def test_each_equal_angle_write_refreshes_the_cached_turbulence_rate(self):
        self.check_cases(CACHE_CASES)

    def test_emitter_volume_uses_native_control_point_orientation(self):
        for case in ORIENTATION_CASES:
            with self.subTest(case=case),tempfile.TemporaryDirectory(prefix='lwe-cp-orientation-'+case+'-') as directory:
                root=Path(directory)
                frame,_=render_scene(self,root,write_orientation_probe(root,case),frames=15)
                verify_orientation_sample(frame,case)

    def test_collapsed_sphere_velocity_uses_random_direction_and_control_point_basis(self):
        for case in SPHERE_CASES:
            with self.subTest(case=case),tempfile.TemporaryDirectory(prefix='lwe-cp-sphere-'+case+'-') as directory:
                root=Path(directory)
                frame,_=render_scene(self,root,write_sphere_probe(root,case,candidate=True),frames=12)
                verify_sphere_sample(frame,case)
