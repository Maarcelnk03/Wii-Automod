import re,sys,zlib,struct
from pathlib import Path
N=Path(sys.argv[1]);what=sys.argv[2]
def sub(f,pat,rep):
    s=f.read_text();n,c=re.subn(pat,rep,s);assert c==1,(f,pat,c);f.write_text(n)
def crc(b):return zlib.crc32(b)&0xffffffff
if what=='priiloader':
    d=(N/'priiloader-auto.elf').read_bytes();sz,c=len(d),crc(d)
    (N/'runner/priiloader-package.inc').write_text(f'#define PRIILOADER_SIZE {sz}u\n#define PRIILOADER_CRC 0x{c:08X}u\n')
    sub(N/'runner/core-manifest.inc',r'\{ "apps/priiloader/auto\.elf", \d+u, 0x[0-9A-Fa-f]+u \}',f'{{ "apps/priiloader/auto.elf", {sz}u, 0x{c:08X}u }}')
    print('priiloader auto.elf',sz,f'{c:08X}')
elif what=='package':
    base=(N/'AUTO45.base46.WPK').read_bytes()
    def swap(pkg,path,payload):
        pb=path.encode();marker=struct.pack('>H',len(pb))+pb
        assert pkg.count(marker)==1
        i=pkg.index(marker)+len(marker);size,c=struct.unpack_from('>II',pkg,i)
        assert zlib.crc32(pkg[i+8:i+8+size])==c
        return pkg[:i]+struct.pack('>II',len(payload),crc(payload))+payload+pkg[i+8+size:]
    post=(N/'post-dumper/NANDAUTO-POST.ELF').read_bytes();pri=(N/'priiloader-auto.elf').read_bytes()
    pkg=swap(swap(base,'apps/nanddumper_ios/auto.elf',post),'apps/priiloader/auto.elf',pri)
    (N/'AUTO45.WPK').write_bytes(pkg)
    sub(N/'runner/main.c',r'boot_sd_app\("0:/apps/nanddumper_ios/auto\.elf", \d+, 0x[0-9A-Fa-f]+u, false\)',f'boot_sd_app("0:/apps/nanddumper_ios/auto.elf", {len(post)}, 0x{crc(post):08X}u, false)')
    sub(N/'runner/core-manifest.inc',r'\{ "apps/nanddumper_ios/auto\.elf", \d+u, 0x[0-9A-Fa-f]+u \}',f'{{ "apps/nanddumper_ios/auto.elf", {len(post)}u, 0x{crc(post):08X}u }}')
    sub(N/'runner/main.c',r'#define CORE_SIZE \d+u',f'#define CORE_SIZE {len(pkg)}u')
    sub(N/'runner/main.c',r'#define CORE_CRC 0x[0-9A-Fa-f]+u',f'#define CORE_CRC 0x{crc(pkg):08X}u')
    print('AUTO45.WPK',len(pkg),f'{crc(pkg):08X}','| nanddumper',len(post),f'{crc(post):08X}')
