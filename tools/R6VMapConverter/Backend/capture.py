import os, runpy, sys
from pathlib import Path
os.environ['R6V_CONVERTER_CONFIG']=str(Path(sys.argv[1]).resolve())
os.environ['R6V_CAPTURE_PID']=sys.argv[2]
from config import WORK, MAP
if not MAP.with_suffix('.usdx').is_file():raise RuntimeError('This map has no adjacent .usdx texture stream.')
runpy.run_module('decode_stream',run_name='__main__')
import live_reader
try:
    live_reader.capture()
    runpy.run_module('map_stream',run_name='__main__')
finally:
    live_reader.k.CloseHandle(live_reader.h)
runpy.run_module('recover_stream',run_name='__main__')
print('Texture cache: '+str(WORK/'streamed/recovered-all.json'),flush=True)
