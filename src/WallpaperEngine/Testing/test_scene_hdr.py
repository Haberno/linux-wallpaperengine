"""HDR quality gating and native constant-color bloom equations on a real GPU."""
import argparse
import json
import re
from pathlib import Path
import tempfile

from test_scene_reflections import capture, material, prepare, write_json


def expected(color, strength, threshold, feather, scatter, levels, tint):
    knee = threshold * feather
    brightness = max(color)
    soft = max(0, min(2 * knee, brightness - threshold + knee))
    contribution = max(soft * soft / (4 * (knee + 0.00001)), brightness - threshold)
    contribution /= max(brightness, 0.00001)
    gain = sum(scatter ** i for i in range(levels))
    gain *= strength / (1 + scatter ** (max(2, levels) - 2))
    return tuple(round(255 * max(0, min(1, c + c * contribution * gain * t))) for c, t in zip(color, tint))


def run(engine, root, baseline=False):
    root.mkdir(parents=True, exist_ok=True)
    checks = []
    cases = [
        ('ramp-zero', (.5, .25, .04), 0, 1, .1, 1.619, 8, (1,1,1)),
        ('soft-knee', (.4, .2, .1), .25, .3, .1, 1, 4, (1,1,1)),
        ('hdr-energy', (2, .3, .1), .5, 1.5, .1, 1, 2, (1,1,1)),
        ('tinted-scatter', (.65,.35,.2), .35,.3,.25,1.619,5,(.25,.5,1)),
        ('single-level', (.65,.35,.2), .35,.3,.25,1.619,1,(.25,.5,1)),
        ('clamp-iterations', (.65,.35,.2), .35,.3,.25,1.619,99,(.25,.5,1)),
    ]
    for name, color, strength, threshold, feather, scatter, levels, tint in cases:
        probe=root/name
        scene=prepare(probe, 'void main() { gl_FragColor = vec4(%s,1); }' % ','.join(map(str,color)))
        scene['general'].update(hdr=True,bloom=True,bloomhdrstrength=strength,
            bloomhdrthreshold=threshold,bloomhdrfeather=feather,bloomhdrscatter=scatter,
            bloomhdriterations=levels,bloomtint=' '.join(map(str,tint)))
        write_json(probe/'materials/probe.json',material('probe',['single']))
        image,stats=capture(engine,probe,scene,() if baseline else ('--post-processing','ultra'))
        want=expected(color,strength,threshold,feather,scatter,min(levels,7),tint)
        actual=image.getpixel((160,90))
        passed=max(abs(a-b) for a,b in zip(actual,want))<=2
        checks.append(dict(name=name,actual=actual,expected=want,passed=passed))
        print('PASS' if passed else 'FAIL',checks[-1],flush=True)
    if not baseline:
        def check(name, actual, want, tolerance=2):
            passed = (max(abs(a-b) for a,b in zip(actual,want)) <= tolerance
                      if isinstance(want, tuple) else actual == want)
            checks.append(dict(name=name,actual=actual,expected=want,passed=passed))
            print('PASS' if passed else 'FAIL',checks[-1],flush=True)

        # The HDR shader macro and scene storage are chosen together at load time.
        # Strength zero excludes bloom as a cause of any output differences.
        for quality, hdr, bloom, mode in [
            ('disabled',True,True,False), ('enabled',True,True,False),
            ('ultra',False,True,False), ('ultra',True,False,False),
            ('ultra',True,True,True),
        ]:
            name=f'gate-{quality}-{hdr}-{bloom}'
            probe=root/name
            scene=prepare(probe, """
void main() {
#if HDR
    gl_FragColor = vec4(0.2,0.6,0.1,1);
#else
    gl_FragColor = vec4(0.6,0.1,0.2,1);
#endif
}
""")
            scene['general'].update(hdr=hdr,bloom=bloom,bloomstrength=0,bloomhdrstrength=0)
            write_json(probe/'materials/probe.json',material('probe',['single']))
            image,stats=capture(engine,probe,scene,('--post-processing',quality,'--msaa','off'))
            check(name,image.getpixel((160,90)),(51,153,26) if mode else (153,26,51))
            check(name+' allocates HDR output','_rt_HDROutput@' in stats,mode)

        for samples in ('off','2','4','8'):
            probe=root/('energy-msaa-'+samples)
            scene=prepare(probe,'void main() { gl_FragColor=vec4(2,0.3,0.1,1); }')
            scene['general'].update(hdr=True,bloom=True,bloomhdrstrength=.5,
                bloomhdrthreshold=1.5,bloomhdrscatter=1,bloomhdriterations=2)
            write_json(probe/'materials/probe.json',material('probe',['single']))
            image,stats=capture(engine,probe,scene,('--post-processing','ultra','--msaa',samples))
            check('HDR energy through MSAA '+samples,image.getpixel((160,90)),
                  expected((2,.3,.1),.5,1.5,.1,1,2,(1,1,1)))

        # Each setting changes after initial frames. Compare against the final
        # independent equation; constructor-only reads fail this fixture.
        probe=root/'live-parameters'
        scene=prepare(probe,'void main() { gl_FragColor=vec4(0.65,0.35,0.2,1); }')
        scene['general'].update(hdr=True,bloom=True)
        for field,initial,updated in [('strength',0,'.35'),('threshold',2,'.3'),
                ('feather',0,'.25'),('scatter',0,'1.619'),('iterations',1,'5'),
                ('tint','0 0 0','new Vec3(.25,.5,1)')]:
            scene['general']['bloomtint' if field=='tint' else 'bloomhdr'+field]={'value':initial,'script':
                'let frames=0; export function update(value) { return ++frames > 3 ? '+updated+' : value; }'}
        write_json(probe/'materials/probe.json',material('probe',['single']))
        image,_=capture(engine,probe,scene,('--post-processing','ultra'))
        check('live HDR parameters',image.getpixel((160,90)),
              expected((.65,.35,.2),.35,.3,.25,1.619,5,(.25,.5,1)))

        probe=root/'live-bloom-off'
        scene=prepare(probe,"""
void main() {
#if HDR
    gl_FragColor=vec4(0.5,0.25,0.04,1);
#else
    gl_FragColor=vec4(1,0,0,1);
#endif
}
""")
        scene['general'].update(hdr=True,bloom={'value':True,'script':
            'let frames=0; export function update(value) { return ++frames <= 3; }'})
        write_json(probe/'materials/probe.json',material('probe',['single']))
        image,_=capture(engine,probe,scene,('--post-processing','ultra'))
        check('live bloom off keeps HDR mode and unprocessed SDR color',image.getpixel((160,90)),(128,64,10))

        # Brightness should multiply image RGB once in HDR mode, including the
        # ping-pong buffers used by image effects. LDR retains its previous color.
        for quality,effect in [('enabled',False),('ultra',False),('ultra',True)]:
            probe=root/f'image-brightness-{quality}-{effect}'
            scene=prepare(probe,'uniform vec4 g_Color4; void main() { gl_FragColor=vec4(0.25,0.125,0.05,1)*g_Color4; }')
            scene['general'].update(hdr=True,bloom=True,bloomhdrstrength=0,bloomstrength=0)
            scene['objects'][0]['brightness']=2
            if effect:
                scene['objects'][0]['effects']=[{'id':3,'file':'effects/identity.json'}]
                write_json(probe/'effects/identity.json',{'name':'identity','passes':[{'material':'materials/identity.json'}]})
                write_json(probe/'materials/identity.json',material('passthrough'))
            write_json(probe/'materials/probe.json',material('probe',['single']))
            image,_=capture(engine,probe,scene,('--post-processing',quality))
            check(f'image brightness once {quality} effect={effect}',image.getpixel((160,90)),
                  (128,64,26) if quality=='ultra' else (64,32,13))

        # Automatic image intermediates remain float; explicit effect formats
        # stay authored, including an intentional RGBA8 clipping boundary.
        for target_format in (None, 'rgba8888', 'rgba16161616f'):
            probe=root/f'effect-energy-{target_format}'
            scene=prepare(probe,'void main() { gl_FragColor=vec4(2,0.3,0.1,1); }')
            scene['general'].update(hdr=True,bloom=True,bloomhdrstrength=.5,
                bloomhdrthreshold=1.5,bloomhdrscatter=1,bloomhdriterations=2)
            scene['objects'][0]['effects']=[{'id':3,'file':'effects/identity.json'}]
            effect={'name':'identity','passes':[{'material':'materials/identity.json'}]}
            if target_format:
                effect['fbos']=[{'name':'explicit','format':target_format,'scale':1}]
                effect['passes']=[{'material':'materials/identity.json','target':'explicit'},
                    {'material':'materials/identity.json','bind':[{'index':0,'name':'explicit'}]}]
            write_json(probe/'effects/identity.json',effect)
            write_json(probe/'materials/identity.json',material('passthrough'))
            write_json(probe/'materials/probe.json',material('probe',['single']))
            image,_=capture(engine,probe,scene,('--post-processing','ultra'))
            want=(255,77,26) if target_format=='rgba8888' else expected((2,.3,.1),.5,1.5,.1,1,2,(1,1,1))
            check(f'effect HDR energy with {target_format}',image.getpixel((160,90)),want)

        probe=root/'reflection-energy'
        scene=prepare(probe,'void main() { gl_FragColor=vec4(2,0.3,0.1,1); }')
        scene['general'].update(hdr=True,bloom=True,bloomhdrstrength=0)
        write_json(probe/'materials/probe.json',material('probe',['single']))
        (probe/'shaders/reader.vert').write_text((probe/'shaders/probe.vert').read_text())
        (probe/'shaders/reader.frag').write_text("""
uniform sampler2D g_Texture3; // {"hidden":true,"default":"_rt_MipMappedFrameBuffer"}
void main() {
    bool hdr=texSample2DLod(g_Texture3,vec2(0.8,0.5),0.0).r>1.5;
    gl_FragColor=vec4(hdr ? vec3(0,1,0) : vec3(1,0,0),1);
}
""")
        write_json(probe/'materials/reader.json',material('reader',['single']))
        write_json(probe/'models/reader.json',{'material':'materials/reader.json','width':8,'height':8})
        scene['objects'].append({'id':2,'name':'reflection consumer','image':'models/reader.json',
            'origin':'16 20 0','size':'32 40'})
        image,_=capture(engine,probe,scene,('--post-processing','ultra'))
        check('modern reflection preserves HDR energy',image.getpixel((80,90)),(0,255,0))

        # Ordinary text draws reapply brightness directly to the scene. Its
        # published texture is a different path and must also preserve values >1.
        probe=root/'text-published'
        scene=prepare(probe,"""
uniform sampler2D g_Texture0;
void main() {
    float peak=0.0;
    for (int i=0;i<64;i++) peak=max(peak,texSample2D(g_Texture0,vec2((float(i)+0.5)/64.0,0.5)).r);
    gl_FragColor=vec4(peak>1.3 ? vec3(0,1,0) : vec3(1,0,0),1);
}
""")
        scene['general'].update(hdr=True,bloom=True,bloomhdrstrength=0)
        obj={'id':2,'name':'HDR text','text':'MMMM','pointsize':12,'origin':'32 20 0',
             'color':'.4 .1 .05','brightness':4,'visible':False,
             'effects':[{'id':3,'file':'effects/identity.json'}]}
        scene['objects'].insert(0,obj)
        write_json(probe/'effects/identity.json',{'name':'identity','passes':[{'material':'materials/identity.json'}]})
        write_json(probe/'materials/identity.json',material('passthrough'))
        write_json(probe/'materials/probe.json',material('probe',['_rt_imageLayerComposite_2_a']))
        image,_=capture(engine,probe,scene,('--post-processing','ultra'))
        check('published text preserves HDR brightness',image.getpixel((160,90)),(0,255,0))

        for logpath in root.glob('*/engine.log'):
            log=logpath.read_text()
            assert not re.search(r'SyntaxError|ReferenceError|TypeError|Failed to setup object',log),log
    (root/'checks.json').write_text(json.dumps(checks,indent=2))
    assert all(c['passed'] for c in checks), 'HDR scene regression'


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('engine',type=Path)
    parser.add_argument('--artifacts',type=Path)
    parser.add_argument('--baseline',action='store_true')
    args=parser.parse_args()
    if args.artifacts:run(args.engine.resolve(),args.artifacts.resolve(),args.baseline)
    else:
        with tempfile.TemporaryDirectory(prefix='lwe-scene-hdr-') as directory:
            run(args.engine.resolve(),Path(directory),args.baseline)
