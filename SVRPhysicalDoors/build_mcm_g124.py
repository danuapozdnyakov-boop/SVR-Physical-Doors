from pathlib import Path
import json,struct,configparser
R=Path(__file__).resolve().parent;P=R/'g124/package';P.mkdir(parents=True,exist_ok=True)
def sub(sig,data):return sig.encode()+struct.pack('<H',len(data))+data
def z(s):return s.encode()+b'\0'
def rec(sig,form,body):return sig.encode()+struct.pack('<IIIIHH',len(body),0,form,0,44,0)+body
def string(s):b=s.encode();return struct.pack('<H',len(b))+b
def script(name,props=b'',count=0):return string(name)+b'\0'+struct.pack('<H',count)+props
quest=0x01000800
props=string('ModName')+b'\x02\x01'+string('SVRPhysicalDoors')
vmad=struct.pack('<HHH',5,2,1)+script('MCM_ConfigBase',props,1)
# QUST VMAD fragment header, followed by alias 0 and its load-game maintenance script.
vmad+=struct.pack('<BHHH',2,0,0,1)+struct.pack('<HhIHHH',0,0,quest,5,2,1)+script('SKI_PlayerLoadGameAlias')
body=sub('EDID',z('SVD_MCMQuest'))+sub('VMAD',vmad)+sub('FULL',z('SVR Physical Doors'))
body+=sub('DNAM',struct.pack('<HBBII',1,40,255,0,0))+sub('NEXT',b'')+sub('ALST',struct.pack('<I',0))+sub('ALID',z('PlayerAlias'))+sub('FNAM',struct.pack('<I',0))+sub('ALFR',struct.pack('<I',0x14))+sub('ALED',b'')
q=rec('QUST',quest,body);header=rec('TES4',0,sub('HEDR',struct.pack('<fII',1.7,2,0x801))+sub('CNAM',z('SVR Physical Doors'))+sub('MAST',z('Skyrim.esm'))+sub('DATA',b'\0'*8))
(P/'SVRPhysicalDoors.esp').write_bytes(header+b'GRUP'+struct.pack('<I',len(q)+24)+b'QUST'+struct.pack('<iHHHH',0,0,0,44,0)+q)
translations={}
def label(key,en,ru):translations['$SVD_'+key]=(en,ru);return '$SVD_'+key
controls=[]
def slider(key,en,ru,help_en,help_ru,default,low,high,step):
 controls.append({'id':key+':Physics','text':label(key,en,ru),'help':label(key+'Help',help_en,help_ru),'type':'slider','valueOptions':{'sourceType':'ModSettingFloat','min':low,'max':high,'step':step,'formatString':'{2}'}})
 return key,default
floats=[
slider('fInertia','Door inertia','Инерция двери','Higher values make doors harder to accelerate. Door size and material still matter.','Чем выше значение, тем труднее разогнать дверь. Размер и материал сохраняют своё влияние.',3,.5,4,.05),
slider('fGrabAcceleration','Acceleration while held','Разгон при захвате','Higher values let a grabbed door follow your hand more readily.','Чем выше значение, тем легче разогнать дверь рукой при захвате.',8,.25,24,.1),
slider('fDamping','Motion damping','Затухание движения','Higher values reduce coasting after release. Lower values preserve momentum longer.','Чем выше значение, тем быстрее дверь останавливается после отпускания.',.08,0,1,.01),
slider('fFriction','Hinge friction','Трение петель','Higher values add resistance at the hinge and slow a released door.','Чем выше значение, тем сильнее сопротивление петель и тем быстрее затихает движение.',.15,0,4,.05),
slider('fSpeedLimit','Maximum opening speed','Предельная скорость','Limits how fast the door can swing. This is a speed cap, not its weight.','Ограничивает скорость створки. Не меняет её вес.',6,.25,16,.05),
slider('fPalmStrength','Bare-hand push strength','Сила толчка ладонью','Strength of a real palm contact. Does not slow a released door or affect grabbing.','Сила толчка при контакте ладони. Не влияет на захват или движение после отпускания.',1,.1,2,.05)]
toggles=[]
for key,en,ru,he,hr,default in [
 ('bDisableHighlight','Disable door highlight','Отключить подсветку дверей','Affects supported doors only; other HIGGS objects keep their highlight.','Применяется только к дверям мода. Подсветка остальных предметов сохраняется.',0),
 ('bShowDoorDuringLockpicking','Show door while lockpicking','Показывать дверь при взломе','Keep the door visible. Turn off if its model obscures your lockpicks.','Сохраняет видимую дверь. Отключите, если она заслоняет отмычки.',1),
 ('bLockpickOnGrab','Lockpick on grab','Взлом при захвате','Grabbing a locked door opens the lockpicking menu. Standard activation remains available.','Захват запертой двери открывает меню взлома. Обычная активация также доступна.',1)]:
 toggles.append({'id':key+':Interaction','text':label(key,en,ru),'help':label(key+'Help',he,hr),'type':'toggle','valueOptions':{'sourceType':'ModSettingBool'}})
config={'modName':'SVRPhysicalDoors','displayName':'SVR Physical Doors','minMcmVersion':13,'pluginRequirements':['SkyUI_SE.esp','MCMHelper.esp'],'cursorFillMode':'topToBottom','pages':[{'pageDisplayName':label('Physics','Door handling','Движение дверей'),'content':controls},{'pageDisplayName':label('Interaction','Interaction','Взаимодействие'),'content':toggles}]}
folder=P/'MCM/Config/SVRPhysicalDoors';folder.mkdir(parents=True,exist_ok=True)
(folder/'config.json').write_text(json.dumps(config,ensure_ascii=False,indent=2),encoding='utf-8')
defaults='[Physics]\n'+''.join(f'{k}={v}\n' for k,v in floats)+'\n[Interaction]\nbDisableHighlight=0\nbShowDoorDuringLockpicking=1\nbLockpickOnGrab=1\n'
(folder/'settings.ini').write_text(defaults)
T=P/'Interface/Translations';T.mkdir(parents=True,exist_ok=True)
for index,language in [(0,'ENGLISH'),(1,'RUSSIAN')]:
 (T/f'SVRPhysicalDoors_{language}.txt').write_text(''.join(f'{key}\t{v[index]}\r\n' for key,v in translations.items()),encoding='utf-16',newline='')
print('Generated one-quest MCM ESP, 8 controls and English/Russian translations; no new Papyrus bytecode required.')
