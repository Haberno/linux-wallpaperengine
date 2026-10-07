#!/usr/bin/env python3
"""Generate and decode-check the calibrated source clips; never a Windows oracle."""
import argparse
import array
import json
from pathlib import Path
import subprocess
import sys

from build_manifest import digest

ROOT = Path(__file__).resolve().parent
CASES = {'sdr10': ('bt709', 'bt709', 'bt709', 1, 1, 1),
         'pq10': ('smpte2084', 'bt2020', 'bt2020nc', 16, 9, 9),
         'hlg10': ('arib-std-b67', 'bt2020', 'bt2020nc', 18, 9, 9)}


def raw_frame(frame, codes):
    data = [codes[x // 32] for y in range(180) for x in range(320)]
    for y in range(16):
        for x in range(320):
            data[y*320+x] = 940 if (x//8+y//8) % 2 else 64
    for y in range(160, 180):
        for x in range(320):
            data[y*320+x] = 940 if x//80 == (frame//5) % 4 else 64
    data += [512] * (320*180//2)
    values = array.array('H', data)
    if sys.byteorder != 'little':
        values.byteswap()
    return values.tobytes()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='new fixture directory')
    args = parser.parse_args()
    reference_path = ROOT / 'reference.json'
    reference_hash = digest(reference_path)
    reference = json.loads(reference_path.read_text())
    codes = [band['y10'] for band in reference['bands']]
    raw = b''.join(raw_frame(frame, codes) for frame in range(40))
    args.output.mkdir()  # No existing fixture files are overwritten.
    manifest = {'schema': 1, 'reference_sha256': reference_hash,
                'generator_sha256': digest(Path(__file__)), 'windows_acceptance': False,
                'ffmpeg_version': subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0],
                'clips': {}}
    for kind, (trc, prim, matrix, trc_vui, prim_vui, matrix_vui) in CASES.items():
        directory = args.output / kind
        directory.mkdir()
        clip = directory / 'source.mp4'
        command = ['ffmpeg', '-n', '-hide_banner', '-loglevel', 'error',
                   '-f', 'rawvideo', '-pixel_format', 'yuv420p10le', '-video_size', '320x180',
                   '-framerate', '10', '-color_range', 'tv', '-colorspace', matrix,
                   '-i', 'pipe:0', '-frames:v', '40', '-an', '-c:v', 'libx265',
                   '-preset', 'ultrafast', '-profile:v', 'main10', '-x265-params',
                   'lossless=1:log-level=error:pools=none:frame-threads=1:range=limited:'
                   f'colorprim={prim_vui}:transfer={trc_vui}:colormatrix={matrix_vui}',
                   '-color_range', 'tv', '-colorspace', matrix, '-color_trc', trc,
                   '-color_primaries', prim, '-movflags', '+faststart', '-tag:v', 'hvc1', str(clip)]
        subprocess.run(command, input=raw, check=True, timeout=60)
        decoded = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(clip),
                                           '-pix_fmt', 'yuv420p10le', '-f', 'rawvideo', '-'], timeout=60)
        if decoded != raw:
            raise ValueError(f'{kind}: decoded samples differ from calibrated source')
        stream = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                            '-count_frames', '-show_streams', '-of', 'json', str(clip)], timeout=30))['streams'][0]
        expected = {'codec_name': 'hevc', 'profile': 'Main 10', 'pix_fmt': 'yuv420p10le',
                    'width': 320, 'height': 180, 'color_range': 'tv', 'color_transfer': trc,
                    'color_primaries': prim, 'color_space': matrix, 'nb_read_frames': '40',
                    'r_frame_rate': '10/1'}
        for key, value in expected.items():
            if stream.get(key) != value:
                raise ValueError(f'{kind}: unexpected {key}: {stream.get(key)}')
        frame = json.loads(subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0',
                           '-read_intervals', '%+#1', '-show_frames', '-of', 'json', str(clip)],
                           timeout=30))['frames'][0]
        for key in ('pix_fmt', 'width', 'height', 'color_range', 'color_transfer', 'color_primaries', 'color_space'):
            if frame.get(key) != expected[key]:
                raise ValueError(f'{kind}: decoded frame metadata differs: {key}')
        manifest['clips'][kind] = {'sha256': digest(clip), 'size': clip.stat().st_size,
                                   'exact_decode': True, 'stream': stream,
                                   'decoded_frame_metadata': frame}
    if digest(reference_path) != reference_hash:
        raise ValueError('Reference changed during generation')
    with (args.output / 'fixture-manifest.json').open('x') as output:
        json.dump(manifest, output, indent=2)
        output.write('\n')


if __name__ == '__main__':
    main()
