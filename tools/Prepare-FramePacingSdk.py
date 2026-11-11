"""Apply and verify the reviewed OutRun SDK patch layers in this project only."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]
subprocess.run([sys.executable,str(root/'tools/Apply-SdkPatches.py'),str(root/'out/dependencies/rexglue-sdk')],check=True)
