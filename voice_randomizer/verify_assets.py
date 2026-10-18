"""Verify installed-scope assets against game PAC data, manifests and prior assets."""
from pathlib import Path
from collections import Counter
import hashlib
import io
import json
import re
import sys
import wave

HERE = Path(__file__).resolve().parent
from pac_index import pac_index


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inspect(data):
    with wave.open(io.BytesIO(data), 'rb') as wav:
        assert (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(), wav.getcomptype()) == (1, 2, 48000, 'NONE')
        frames = wav.getnframes()
        assert frames > 0 and len(wav.readframes(frames)) == frames * 2
        return frames


def main():
    manifest = json.loads((HERE/'assets/manifest.json').read_text(encoding='utf-8'))
    placeholders = json.loads((HERE/'assets/placeholder_manifest.json').read_text(encoding='utf-8'))
    names = {x['name'] for x in manifest}
    assert len(names) == len(manifest) == len(placeholders)
    assert names == {x['name'] for x in placeholders}
    cpp = dict((name, int(ident)) for name, ident in re.findall(
        r'\{"([^"]+)", (\d+)\}', (HERE/'src/assets.generated.h').read_text()))
    assert cpp == {x['name']: x['voice_id'] for x in manifest}
    placeholder_map = {x['name']: x for x in placeholders}
    game_pac = HERE.parent/'pac/steam/voice.pac'
    index = pac_index(game_pac)
    with game_pac.open('rb') as game:
        for item in manifest:
            name = item['name']
            legacy = (HERE/'assets/old'/f'{name}.wav').read_bytes()
            test = (HERE/'assets/placeholder'/f'{name}.wav').read_bytes()
            assert digest(legacy) == item['old_sha256'], name
            inspect(legacy)
            entry = index[f'voice/wav/{name}.wav']
            game.seek(entry['offset'])
            original = game.read(entry['size'])
            assert digest(original) == item['new_sha256'], name
            assert inspect(test) == inspect(original) == item['new_frames'] == placeholder_map[name]['frames'], name
            assert digest(test) == placeholder_map[name]['sha256'], name
            assert digest(test) not in (digest(original), digest(legacy)), name
            assert placeholder_map[name]['phrase'] == item['placeholder_phrase'], name
    print(f'PASS: {len(names)} asset triples match manifests, exact game sample counts, PCM format and SHA256; compiled allowlist matches all IDs.')
    backup = HERE/'backups/0.3-before-full-scope'
    preserved = 0
    if backup.exists():
        for file, folder, key in [('manifest.json','old','old_sha256'), ('placeholder_manifest.json','placeholder','sha256')]:
            for item in json.loads((backup/file).read_text(encoding='utf-8-sig')):
                assert digest((HERE/'assets'/folder/(item['name']+'.wav')).read_bytes()) == item[key], item['name']
                preserved += 1
        print(f'PASS: {preserved} pre-existing battle legacy/placeholder files remain byte-identical.')
    report = dict(voices=len(names), categories=dict(Counter(x['category'] for x in manifest)),
                  exact_game_frames=True, hashes_verified=True, preserved_battle_files=preserved,
                  scope='All matching assets in the supplied 1st pack; no 2nd assets.')
    (HERE/'asset_validation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
