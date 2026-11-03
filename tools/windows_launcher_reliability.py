"""Exercise actual Windows launcher recovery and headless support actions in isolated folders."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from runner_platform import BACKENDS, actual_backend, process_options


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bin',required=True,type=Path)
    parser.add_argument('--rom',required=True,type=Path)
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    args.bin=args.bin.resolve(); args.rom=args.rom.resolve(); args.out=args.out.resolve()
    args.out.mkdir(parents=True,exist_ok=True)
    fixture=ROOT/'tools/save_editor/fixtures/first_save.flash'
    digest=lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    originals={p:digest(p) for p in (args.bin,args.rom,fixture)}
    results=[]
    base={k:v for k,v in os.environ.items() if k!='SHIP_HOME' and not k.startswith(('PORT_','ARES_REF_','XCHECK_'))}

    def run_case(name,backend,arguments,marker=None,tools=False,handheld=False):
        folder=args.out/name; home=folder/'Pokémon 런처'; home.mkdir(parents=True)
        exe=home/'pokestadium.exe'; shutil.copy2(args.bin,exe)
        cwd=folder/'unrelated'; cwd.mkdir()
        (home/'saves').mkdir(); save=home/'saves/stadium.flash'; save.write_bytes(fixture.read_bytes())
        config={'Window':{'Backend':{'Id':BACKENDS[backend][0],'Name':BACKENDS[backend][1]}},
                'CVars':{'gLauncher':{'Rom':str(args.rom),'Save':'saves/stadium.flash','AutoPlay':1,
                                      'Renderer':BACKENDS[backend][0]},'gKeepRecovery':'controller-state'}}
        configfile=home/'pokestadium.cfg.json'; configfile.write_text(json.dumps(config),encoding='utf-8')
        original=configfile.read_bytes()
        if marker: (home/'launcher-session.txt').write_text(marker+'\n')
        env=base|{'PORT_LAUNCHER_SHOT':str(folder/'launcher.ppm'),'PORT_HIDDEN_WINDOW':'1'}
        if tools: env['PORT_LAUNCHER_OPEN_TOOLS']='1'
        if handheld: env['PORT_LAUNCHER_HANDHELD']='1'
        with (folder/'run.log').open('w',encoding='utf-8') as log:
            process=subprocess.run([str(exe)]+arguments,cwd=cwd,env=env,stdout=log,stderr=subprocess.STDOUT,
                                   timeout=120,**process_options())
        assert process.returncode==0, (name,(folder/'run.log').read_text(errors='replace'))
        assert (folder/'launcher.ppm').is_file(), 'Auto-play was not bypassed'
        from PIL import Image,ImageStat
        image=Image.open(folder/'launcher.ppm'); assert max(ImageStat.Stat(image).stddev)>5
        if tools:
            from PIL import ImageChops
            baseline_case = 'handheld-launcher' if handheld else backend + '-force-launcher'
            baseline=Image.open(args.out/baseline_case/'launcher.ppm')
            assert image.size==baseline.size
            assert max(ImageStat.Stat(ImageChops.difference(image,baseline)).mean)>8, 'Files/help panel did not become visible'
            image.save(folder/'tools.png')
        state=json.loads(configfile.read_text(encoding='utf-8'))
        assert state['CVars']['gKeepRecovery']=='controller-state'
        assert state['CVars']['gLauncher']['Save']=='saves/stadium.flash'
        assert save.read_bytes()==fixture.read_bytes()
        assert not (home/'launcher-session.txt').exists()
        safe='--safe-mode' in arguments or marker=='graphics'
        expected='opengl' if safe else backend
        assert actual_backend(str(home))==BACKENDS[expected][0]
        if safe:
            assert state['CVars']['gLauncher']['AutoPlay']==0
            copies=list(home.glob('pokestadium.cfg.json.before-recovery-*'))
            assert len(copies)==1 and copies[0].read_bytes()==original
        results.append({'case':name,'status':'PASS','active_renderer':expected}); print(results[-1],flush=True)

    for backend in ('opengl','vulkan','directx'):
        run_case(backend+'-force-launcher',backend,['--launcher'])
    for backend in ('opengl','vulkan','directx'):
        run_case(backend+'-tools-panel',backend,['--launcher'],tools=True)
    run_case('handheld-launcher','opengl',['--launcher'],handheld=True)
    run_case('handheld-tools-panel','opengl',['--launcher'],tools=True,handheld=True)
    run_case('explicit-safe-mode','directx',['--safe-mode'])
    run_case('interrupted-graphics','vulkan',[],marker='graphics')
    run_case('interrupted-game','directx',[],marker='game')

    home=args.out/'headless'; home.mkdir(); exe=home/'pokestadium.exe'; shutil.copy2(args.bin,exe)
    env=base|{'SHIP_HOME':str(home)}
    def utility(arguments):
        return subprocess.run([str(exe)]+arguments,cwd=home,env=env,capture_output=True,text=True,encoding='utf-8',
                              timeout=45,**process_options())
    save=home/'active.flash'; save.write_bytes(fixture.read_bytes())
    result=utility(['--check-launcher',str(args.rom),str(save)]+['','']*4)
    assert result.returncode==0,result.stdout+result.stderr
    wrong=home/'bad.flash'; wrong.write_bytes(b'wrong size')
    result=utility(['--check-launcher',str(args.rom),str(wrong)]+['','']*4)
    assert result.returncode!=0 and 'wrong size' in result.stdout
    result=utility(['--export-stadium-save',str(save)])
    assert result.returncode==0,result.stderr
    assert Path(result.stdout.strip()).read_bytes()==fixture.read_bytes()
    assert save.read_bytes()==fixture.read_bytes()
    (home/'logs').mkdir(exist_ok=True)
    (home/'logs/pokestadium.log').write_text('Cannot open C:\\Users\\PrivatePerson\\Downloads\\personal.z64\nRenderer startup failed\n')
    result=utility(['--launcher-diagnostics'])
    assert result.returncode==0,result.stderr
    text=Path(result.stdout.strip()).read_text(encoding='utf-8')
    assert 'PrivatePerson' not in text and 'personal.z64' not in text
    assert 'Renderer startup failed' in text and 'Windows' in text
    results.append({'case':'headless-preflight-export-diagnostics','status':'PASS'});print(results[-1],flush=True)
    unchanged=all(digest(p)==sha for p,sha in originals.items())
    assert unchanged
    (args.out/'results.json').write_text(json.dumps({'results':results,'inputs_unchanged':unchanged},indent=2))
    return 0

if __name__=='__main__': raise SystemExit(main())
