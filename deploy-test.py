"""Update the isolated VLMNET001 test title only. Full first install uses VitaShell."""
import argparse,hashlib,ipaddress,subprocess,sys,zipfile,struct
from pathlib import Path
root=Path(__file__).resolve().parent
def title_id(sfo):
    if sfo[:4]!=b'\0PSF':raise ValueError('Invalid SFO')
    keys,data,count=struct.unpack_from('<III',sfo,8)
    for index in range(count):
        key,fmt,length,maximum,offset=struct.unpack_from('<HHIII',sfo,20+16*index)
        if sfo[keys+key:].split(b'\0',1)[0]==b'TITLE_ID':return sfo[data+offset:data+offset+length].rstrip(b'\0').decode('ascii')
    raise ValueError('Missing title')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',required=True);parser.add_argument('--cli',required=True);args=parser.parse_args()
    if not ipaddress.ip_address(args.host).is_private:raise SystemExit('Trusted LAN only')
    vpk=root/'build-vita/TyphoonLLM-Online.vpk'
    with zipfile.ZipFile(vpk) as package:
        if title_id(package.read('sce_sys/param.sfo'))!='VLMNET001':raise SystemExit('Refusing production or unrelated title')
        eboot=package.read('eboot.bin')
    if eboot!=(root/'build-vita/eboot.bin').read_bytes():raise SystemExit('VPK and built executable differ')
    def command(*values):
        result=subprocess.run([sys.executable,args.cli,'--host',args.host,*map(str,values)],capture_output=True,text=True,timeout=60)
        if result.returncode or result.stdout.startswith('ERR '):raise RuntimeError(result.stdout.strip() or result.stderr.strip())
        print(result.stdout.strip())
    command('awake',1);command('stat','ux0:app/VLMNET001/eboot.bin');command('close','VLMNET001')
    local=root/'build-vita/deploy-eboot.bin';local.write_bytes(eboot)
    command('put',vpk,'ux0:data/TyphoonOnline/TyphoonOnline-Dev.vpk')
    command('put',local,'ux0:data/TyphoonOnline/eboot.bin')
    command('cp','ux0:data/TyphoonOnline/eboot.bin','ux0:app/VLMNET001/eboot.bin')
    command('launch','VLMNET001')
    print('deployed isolated eboot SHA256='+hashlib.sha256(eboot).hexdigest())
if __name__=='__main__':main()
