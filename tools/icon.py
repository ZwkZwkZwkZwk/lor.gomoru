from pathlib import Path
import hashlib
import subprocess
root=Path(__file__).resolve().parent.parent
source=root/'assets'/'icon-source.png'
folder=root/'build'/'AppIcon.iconset'
folder.mkdir(parents=True,exist_ok=True)
for size in (16,32,128,256,512):
    for scale in (1,2):
        suffix='@2x' if scale==2 else ''
        output=folder/f'icon_{size}x{size}{suffix}.png'
        subprocess.run(['sips','-z',str(size*scale),str(size*scale),str(source),'--out',str(output)],check=True,stdout=subprocess.DEVNULL)
subprocess.run(['iconutil','-c','icns',str(folder),'-o',str(root/'assets'/'AppIcon.icns')],check=True)
print('ICNS created from unchanged square source; no crop or aspect change')
print('Source SHA256 '+hashlib.sha256(source.read_bytes()).hexdigest())
