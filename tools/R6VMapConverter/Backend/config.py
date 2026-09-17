"""Explicit per-run paths; backend modules never reference the development machine."""
import json, os
from pathlib import Path

CONFIG = json.loads(Path(os.environ['R6V_CONVERTER_CONFIG']).read_text(encoding='utf-8-sig'))
WORK = Path(CONFIG['work']).resolve()
BATCH = WORK / 'assets'
COOKED = Path(CONFIG['cooked']).resolve()
MAP = Path(CONFIG['map']).resolve()
PACKAGE = CONFIG['package']
CACHE = []
if CONFIG.get('textureCache'):
    manifest = Path(CONFIG['textureCache'])
    CACHE = json.loads(manifest.read_text(encoding='utf-8-sig'))
    for row in CACHE:
        if row.get('file') and not Path(row['file']).is_absolute():
            row['file'] = str((manifest.parent / row['file']).resolve())
