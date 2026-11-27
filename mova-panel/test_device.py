"""ARM CLI tests via ADB; no cloud requests or framebuffer access."""
from pathlib import Path
import json
import hashlib
import base64
import zlib
import struct
import subprocess
import tempfile
from PIL import Image

ROOT = Path(__file__).resolve().parent
ADB = ROOT.parent / 'tools/platform-tools/adb.exe'
if not ADB.exists():
    ADB = Path('adb')


def adb(*args, ok=True):
    remote = args[0] == 'shell'
    if remote:
        args = ('shell', args[1] + '; result=$?; echo MOVA_TEST_EXIT:$result')
    r = subprocess.run([str(ADB), *map(str, args)], capture_output=True, text=True, encoding='utf-8')
    if remote and 'MOVA_TEST_EXIT:' in r.stdout:
        body, marker = r.stdout.rsplit('MOVA_TEST_EXIT:', 1)
        r.returncode = int(marker.strip())
        r.stdout = body
    if ok and r.returncode:
        raise RuntimeError(r.stderr + r.stdout)
    return r


def main():
    adb('push', ROOT / 'build/sd-card/mova-panel/mova-panel', '/tmp/mova-panel-prototype')
    adb('shell', 'chmod 755 /tmp/mova-panel-prototype')
    assert 'LAYOUT_TEST_OK' in adb('shell', '/tmp/mova-panel-prototype --layout-test').stdout
    assert adb('shell', 'MOVA_ORIENTATION=invalid /tmp/mova-panel-prototype --layout-test', ok=False).returncode != 0
    settings = json.loads((ROOT / 'secrets.example.json').read_text())
    settings.update(device_id='test-device')
    settings['rooms'] = [{'id': 8, 'name': 'Kueche'}, {'id': 3, 'name': 'Flur'},
                         {'id': 19, 'name': 'Bad'}]
    with tempfile.TemporaryDirectory() as temp:
        fixture = Path(temp) / 'fixture.json'

        def upload(value):
            fixture.write_text(json.dumps(value), encoding='utf-8')
            adb('push', fixture, '/tmp/mova-panel-test.json')

        upload(settings)
        result = adb('shell', '/tmp/mova-panel-prototype --self-test /tmp/mova-panel-test.json').stdout
        lines = [x.strip() for x in result.splitlines() if x.strip()]
        room, whole = map(json.loads, lines[:2])
        assert room['siid'] == 4 and room['aiid'] == 1
        assert room['in'][0] == {'piid': 1, 'value': 18}
        assert json.loads(room['in'][1]['value'])['selects'] == [[8, 1, 1, 1, 1], [19, 1, 1, 1, 1]]
        assert whole == {'did': 'test-device', 'siid': 2, 'aiid': 1, 'in': []}
        signed = json.loads(lines[2])
        canonical = 'data=[did="test-device"&id=7&method="action"&params=[aiid=1&did="test-device"&siid=4]]&did=test-device&id=7'
        expected = hashlib.md5((canonical + '1700000000000gigxlmqwZ]7oWZUF').encode()).hexdigest()
        assert signed['timestamp'] == 1700000000000 and signed['sign'] == expected
        disable = json.loads(lines[3])
        assert len(disable) == 1 and disable[0]['siid'] == 4 and disable[0]['piid'] == 50
        assert json.loads(disable[0]['value']) == {'k': 'SmartHost', 'v': 0}
        # Preserve exact live manual mode/suction/water; reject missing/error data.
        props = [{'siid': 4, 'piid': 50, 'code': 0,
                  'value': json.dumps([{'k': 'SmartHost', 'v': 0}])},
                 {'siid': 4, 'piid': 4, 'code': 0, 'value': 3},
                 {'siid': 4, 'piid': 5, 'code': 0, 'value': 2},
                 {'siid': 4, 'piid': 23, 'code': 0, 'value': 131584}]
        def settings_check(value, ok=True):
            fixture.write_text(json.dumps(value), encoding='utf-8')
            adb('push', fixture, '/tmp/mova-settings-fixture.json')
            return adb('shell', '/tmp/mova-panel-prototype --settings-check /tmp/mova-settings-fixture.json', ok=ok)
        for genius in [0, 1, 2]:
            props[0]['value'] = json.dumps([{'k': 'SmartHost', 'v': genius}])
            response = {'code': 0, 'data': {'result': props}}
            assert json.loads(settings_check(response).stdout) == {'genius': genius, 'mode': 131584, 'suction': 3, 'water': 2}
        for invalid in [{'code': 0, 'data': {'result': props[:-1]}},
                        {'code': 0, 'data': {'result': props[1:]}}, {'code': -1}]:
            assert settings_check(invalid, ok=False).returncode != 0
        assert lines[-1] == 'SELF_TEST_OK' 
        for change in ({'region': 'invalid'}, {'rooms': [{'id': 8, 'name': 'A'}, {'id': 8, 'name': 'B'}]},
                       {'rooms': [{'id': 0, 'name': 'A'}]}, {'demo': 2}):
            upload(settings | change)
            assert adb('shell', '/tmp/mova-panel-prototype --self-test /tmp/mova-panel-test.json', ok=False).returncode != 0
        upload(settings)
        assert 'DEMO' in adb('shell', '/tmp/mova-panel-prototype --probe /tmp/mova-panel-test.json').stdout
        # Synthetic map fixture: custom UTF-8 name, type/index fallback, metadata-only
        # removed room, two map headers and a curr_id differing from entry.id.
        header = bytearray(27)
        struct.pack_into('<H', header, 0, 7)
        struct.pack_into('<HH', header, 19, 2, 2)
        metadata = {'seg_inf': {
            '8': {'name': base64.b64encode('KÃ¼che'.encode()).decode()},
            '19': {'type': 1, 'index': 1}, '3': {'type': 6}}}
        payload = bytes(header) + bytes([8, 19, 0, 0]) + json.dumps(metadata).encode()
        encoded = base64.b64encode(zlib.compress(payload)).decode()
        other = bytearray(payload)
        struct.pack_into('<H', other, 0, 4)
        maps = {'curr_id': 7, 'mapstr': [
            {'id': 0, 'map': base64.b64encode(zlib.compress(other)).decode()},
            {'id': 0, 'map': encoded}]}
        def map_upload(value):
            fixture.write_text(json.dumps(value), encoding='utf-8')
            adb('push', fixture, '/tmp/mova-panel-map-test.json')
        map_upload(maps)
        decoded = adb('shell', '/tmp/mova-panel-prototype --decode-rooms /tmp/mova-panel-map-test.json').stdout
        assert [line for line in decoded.splitlines() if line] == ['MAP 7', '8\tKÃ¼che', '19\tWohnzimmer 2']
        for invalid in (maps | {'curr_id': 9}, {'curr_id': 7, 'mapstr': [{'map': 'invalid'}]},
                        {'curr_id': 7, 'mapstr': [{'map': encoded + ',encrypted-key'}]}):
            map_upload(invalid)
            assert adb('shell', '/tmp/mova-panel-prototype --decode-rooms /tmp/mova-panel-map-test.json', ok=False).returncode != 0
        adb('shell', 'cd /tmp && /tmp/mova-panel-prototype --preview /tmp/mova-panel-test.json')
        adb('pull', '/tmp/preview.ppm', ROOT / 'build/test-preview.ppm')
        with Image.open(ROOT / 'build/test-preview.ppm') as im:
            assert im.size == (1024, 600)
            im.save(ROOT / 'build/test-preview.png')
        adb('shell', 'cd /tmp && MOVA_ORIENTATION=portrait /tmp/mova-panel-prototype --preview /tmp/mova-panel-test.json')
        adb('pull', '/tmp/preview.ppm', ROOT / 'build/test-portrait-physical.ppm')
        with Image.open(ROOT / 'build/test-portrait-physical.ppm') as im:
            assert im.size == (1024, 600)
            upright = im.transpose(Image.Transpose.ROTATE_90)
            assert upright.size == (600, 1024)
            upright.save(ROOT / 'build/test-portrait.png')
    (ROOT / 'build/device-tests.txt').write_text(
        'PASS: ARM selected room IDs/order, whole-area command, empty selection, invalid '
        'region/duplicate IDs/zero ID/demo, demo probe, selected map/header, Unicode room names, '
        'type/index fallback, removed room, invalid/encrypted map rejection, landscape/portrait preview sizes, '
        'all-pixel rotation and touch inverse, button hit targets in both orientations, invalid orientation rejection. No robot actions.\n')
    print('PASS: ARM command/config tests and offscreen preview; no real robot actions')


if __name__ == '__main__':
    main()
