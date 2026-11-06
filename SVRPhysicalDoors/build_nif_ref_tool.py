from pathlib import Path
import os,subprocess
R=Path(__file__).resolve().parent.parent
setup=(R/'physical-containers/build_plugin.py').read_text(encoding='utf-8');exec(setup[setup.index('env={'):setup.index('line=next')])
base=R/'work/nifly-source/nifly-main';out=R/'work/nif-ref-build';out.mkdir(exist_ok=True)
sources=list((base/'src').glob('*.cpp'))+[R/'work/nif_ref_offsets.cpp']
changed=[p for p in sources if not (out/(p.stem+'.obj')).exists() or p.stat().st_mtime>(out/(p.stem+'.obj')).stat().st_mtime]
flags=['/nologo','/EHsc','/std:c++17','/O2','/MD','/bigobj','/MP8','/I'+str(base/'include'),'/I'+str(base/'external'),'/Fo'+str(out)+'\\']
if changed:
 r=subprocess.run([vc+r'\bin\Hostx64\x64\cl.exe',*flags,'/c',*map(str,changed)],env=env,capture_output=True,text=True);print(r.stdout+r.stderr);assert r.returncode==0
cmd=[vc+r'\bin\Hostx64\x64\link.exe','/nologo','/OUT:'+str(out/'nif_ref_offsets.exe')]+[str(out/(p.stem+'.obj')) for p in sources]
r=subprocess.run(cmd,env=env,capture_output=True,text=True);(out/'build.log').write_text(r.stdout+r.stderr);print(r.stdout[-3500:]+r.stderr[-1000:]);assert r.returncode==0
