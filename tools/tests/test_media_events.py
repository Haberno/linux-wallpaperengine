"""Opt-in private-MPRIS regression for independent SceneScript media events."""
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import time
import unittest

PLAYER = r'''
import dbus, dbus.service, dbus.mainloop.glib, json, sys
from gi.repository import GLib
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
class Player(dbus.service.Object):
    def __init__(self):
        self.bus = dbus.SessionBus()
        self.name = dbus.service.BusName('org.mpris.MediaPlayer2.lwe_event_test', self.bus)
        super().__init__(self.name, '/org/mpris/MediaPlayer2')
        self.values = dict(state='Playing', title='Track One', artist='Artist', album='Album',
                           duration=180000000, position=30000000, url='')
    @dbus.service.method('org.freedesktop.DBus.Properties', in_signature='ss', out_signature='v')
    def Get(self, iface, prop):
        if prop == 'PlaybackStatus': return self.values['state']
        if prop == 'Position': return dbus.Int64(self.values['position'])
        if prop == 'Metadata': return dbus.Dictionary({
            'xesam:title': self.values['title'],
            'xesam:artist': dbus.Array([self.values['artist']], signature='s'),
            'xesam:album': self.values['album'],
            'mpris:length': dbus.Int64(self.values['duration']),
            'mpris:artUrl': self.values['url']}, signature='sv')
        raise dbus.exceptions.DBusException('Unknown property')
    @dbus.service.signal('org.freedesktop.DBus.Properties', signature='sa{sv}as')
    def PropertiesChanged(self, iface, props, invalidated): pass
    def command(self, source, condition):
        line = sys.stdin.readline()
        if not line: return False
        self.values.update(json.loads(line))
        self.PropertiesChanged('org.mpris.MediaPlayer2.Player', {
            'PlaybackStatus': self.values['state'], 'Metadata': self.Get('', 'Metadata')}, [])
        print('READY', flush=True)
        return True
player = Player()
GLib.io_add_watch(sys.stdin, GLib.IO_IN, player.command)
print('READY', flush=True)
GLib.MainLoop().run()
'''

SCRIPT = '''
const counts={properties:0,playback:0,timeline:0,thumbnail:0};
let position=-1, title='', state=-1, thumbnail=false;
function report(){ console.log('MEDIA_EVENTS '+JSON.stringify({counts,position,title,state,thumbnail})); }
export function mediaPropertiesChanged(event){counts.properties++;title=event.title;report();}
export function mediaPlaybackChanged(event){counts.playback++;state=event.state;report();}
export function mediaTimelineChanged(event){counts.timeline++;position=event.position;report();}
export function mediaThumbnailChanged(event){counts.thumbnail++;thumbnail=event.hasThumbnail;report();}
export function update(value){return value;}
'''

@unittest.skipUnless(os.environ.get('LWE_TEST_BINARY'), 'Set LWE_TEST_BINARY for graphics integration tests')
class MediaEvents(unittest.TestCase):
    def stop(self, process):
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        for stream in (process.stdin, process.stdout, process.stderr):
            if stream:
                stream.close()

    def ready(self, process):
        self.assertTrue(select.select([process.stdout], [], [], 8)[0], 'Fake media player did not respond')
        self.assertEqual(process.stdout.readline().strip(), 'READY')

    def wait_event(self, path, predicate):
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:
            lines=path.read_text().splitlines()
            events=[json.loads(line.split('MEDIA_EVENTS ',1)[1]) for line in lines if 'MEDIA_EVENTS ' in line]
            if events and predicate(events[-1]):
                return events[-1]
            time.sleep(.025)
        self.fail(path.read_text()[-6000:])

    def test_unchanged_fields_do_not_receive_other_event_types(self):
        with tempfile.TemporaryDirectory(prefix='lwe-media-events-') as directory:
            root=Path(directory)
            from PIL import Image
            Image.new('RGB',(16,16),'blue').save(root/'cover.png')
            daemon=subprocess.Popen(['dbus-daemon','--session','--nofork','--print-address=1'],
                                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            self.addCleanup(self.stop,daemon)
            address=daemon.stdout.readline().strip()
            self.assertTrue(address.startswith('unix:'))
            env=dict(os.environ,DBUS_SESSION_BUS_ADDRESS=address,WPE_LOG_FILE='off',
                     WPE_CONTROL_SOCKET=str(root/'control.sock'),XDG_STATE_HOME=str(root/'state'))
            player=subprocess.Popen([sys.executable,'-u','-c',PLAYER],env=env,stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
            self.addCleanup(self.stop,player)
            self.ready(player)
            (root/'project.json').write_text(json.dumps({'type':'scene','file':'scene.json','title':'Media event routing'}))
            (root/'scene.json').write_text(json.dumps({
                'camera':{'eye':'0 0 1','center':'0 0 0','up':'0 1 0'},
                'general':{'orthogonalprojection':{'width':320,'height':180},
                           'bloom':{'value':False,'script':SCRIPT}},'objects':[]}))
            log_path=root/'engine.log'
            with log_path.open('w') as log:
                engine=subprocess.Popen([str(Path(os.environ['LWE_TEST_BINARY']).resolve()),
                    '--window','0x0x320x180','--volume','0','--fps','20','--no-full-screen-pause',str(root)],
                    env=env,stdout=log,stderr=subprocess.STDOUT)
                try:
                    event=self.wait_event(log_path,lambda e:e['position']==30 and e['title']=='Track One'
                        and e['state']==1 and all(n>0 for n in e['counts'].values()))
                    # Absorb intentional initial delivery before measuring changes.
                    time.sleep(.3)
                    event=self.wait_event(log_path,lambda e:True)
                    for update,kind,predicate in [
                        ({'position':31000000},'timeline',lambda e:e['position']==31),
                        ({'state':'Paused'},'playback',lambda e:e['state']==2),
                        ({'title':'Track Two'},'properties',lambda e:e['title']=='Track Two'),
                        ({'url':(root/'cover.png').as_uri()},'thumbnail',lambda e:e['thumbnail']),
                    ]:
                        previous=event['counts']
                        player.stdin.write(json.dumps(update)+'\n');player.stdin.flush();self.ready(player)
                        event=self.wait_event(log_path,predicate)
                        time.sleep(.15)
                        event=self.wait_event(log_path,predicate)
                        with self.subTest(event=kind):
                            self.assertEqual(event['counts'],{key:value+(key==kind) for key,value in previous.items()})
                finally:
                    self.stop(engine)
            self.assertEqual(engine.returncode,0,log_path.read_text())
            self.assertNotIn('ScriptEngine [',log_path.read_text())

if __name__=='__main__':
    unittest.main()
