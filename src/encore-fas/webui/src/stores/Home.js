import { defineStore } from 'pinia'
import { ref } from 'vue'
import { exec } from 'kernelsu'
import * as KernelSU from '@/helpers/KernelSU'
const mod='/data/adb/modules/encore_pixel_cp41'
export const useHomeStore=defineStore('home',()=>{
  const daemonPidRaw=ref(''),moduleVersion=ref(''),currentProfileRaw=ref('unknown')
  const kernelVersion=ref(''),chipsetName=ref('unknown'),androidSDK=ref('')
  const daemonStatusRaw=ref('loading'),daemonError=ref(''),logoImage=ref('./encore_sleeping.avif')
  const packageName=ref(''),foregroundPackage=ref(''),gameActive=ref(false),batteryC=ref(null)
  const defaultDisabled=ref(false),paused=ref(false),fasDisabled=ref(false),fasAdvisorDisabled=ref(false),fasLevel=ref(0),nodes=ref([])
  let timer=null,busy=false,initialized=false
  async function snapshot(){
    if(busy||document.hidden)return;busy=true
    try{
      if(!KernelSU.isKSUWebUI())throw new Error('Please open this page from the module WebUI')
      const r=await exec(`sh ${mod}/ui-state.sh`)
      if(r.errno!==0)throw new Error(r.stderr||'Status read failed')
      const fields=Object.fromEntries(r.stdout.split('\n').filter(l=>l.includes('=')).map(l=>{
        const i=l.indexOf('=');return[l.slice(0,i),l.slice(i+1).trim()]
      }))
      const status=r.stdout.match(/STATUS_BEGIN\s*([^\n]+)/)?.[1]||'UNKNOWN'
      const values=Object.fromEntries([...status.matchAll(/(\w+)=([^\s]+)/g)].map(m=>[m[1],m[2]]))
      daemonStatusRaw.value=fields.process_ok==='1'?'running':'stopped'
      daemonPidRaw.value=`${fields['controller.pid']||'—'} / ${fields['brain.pid']||'—'}`
      currentProfileRaw.value=status.startsWith('GAME_LITE')?'game_lite':status.startsWith('GAME ')?'game':status.startsWith('RESTORED')?'restored':'stopped'
      packageName.value=values.package||''
      foregroundPackage.value=fields.foreground_package||packageName.value
      gameActive.value=fields.game_active==='1'
      batteryC.value=values.temp?(Number(values.temp)/10).toFixed(1):null
      defaultDisabled.value=['true','1'].includes(fields.default_fps_disabled);paused.value=fields.pause==='1'
      fasDisabled.value=fields.fas_disabled==='1'
      fasAdvisorDisabled.value=fields.fas_advisor_disabled==='1'
      fasLevel.value=Number(fields.fas_level||fields.FAS_LEVEL||0)
      nodes.value=[['CPU_0','CPU 0–1',1000],['CPU_2','CPU 2–4',1000],['CPU_5','CPU 5–6',1000],['CPU_7','CPU 7',1000],
        ['34f00000.gpu0','GPU',1e6],['200c0780.dsufreq','DSU',1e6],['irm_gmc_freq','IRM GMC',1e6]]
        .map(([key,name,unit])=>({name,values:(fields[key]||'').split(',').map(v=>Number.isFinite(Number(v))?Math.round(Number(v)/unit):'—')}))
      daemonError.value='';logoImage.value=daemonStatusRaw.value==='running'?'./encore_happy.avif':'./encore_sleeping.avif'
    }catch(e){daemonStatusRaw.value='error';daemonError.value=e.message;currentProfileRaw.value='error';nodes.value=[]}
    finally{busy=false}
  }
  function stopProfileMonitoring(){if(timer)clearInterval(timer);timer=null}
  async function initializeData(){
    if(!initialized){
      await Promise.allSettled([
        KernelSU.readFile(`${mod}/module.prop`).then(t=>{moduleVersion.value=t.match(/^version=(.*)$/m)?.[1]||'unknown'}),
        exec('getprop ro.soc.model').then(r=>{chipsetName.value=r.stdout.trim()||'unknown'}),
        exec('uname -r -m').then(r=>{kernelVersion.value=r.stdout.trim()}),
        exec('getprop ro.build.version.sdk').then(r=>{androidSDK.value=r.stdout.trim()}),
      ]);initialized=true
    }
    await snapshot();stopProfileMonitoring();timer=setInterval(snapshot,3000)
  }
  return{daemonPidRaw,moduleVersion,currentProfileRaw,kernelVersion,chipsetName,androidSDK,daemonStatusRaw,daemonError,logoImage,
    packageName,foregroundPackage,gameActive,batteryC,defaultDisabled,paused,fasDisabled,fasAdvisorDisabled,fasLevel,nodes,initializeData,getServiceState:snapshot,getCurrentProfile:snapshot,
    stopProfileMonitoring,stopDaemonMonitoring:stopProfileMonitoring}
})
