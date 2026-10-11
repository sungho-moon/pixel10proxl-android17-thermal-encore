import { defineStore } from 'pinia'
import { ref, computed } from 'vue'
import { exec } from 'kernelsu'
import * as KernelSU from '@/helpers/KernelSU'
const path='/data/adb/.config/encore_pixel_cp41/config.json'
const flag='/data/adb/.config/encore_pixel_cp41/disable-high-fps'
export const useEncoreConfigStore=defineStore('encoreConfig',()=>{
  const config=ref(null), highFPS=ref(false)
  let originalText='', pending={}
  const isLoaded=computed(()=>config.value!==null)
  const isLiteModeEnabled=computed(()=>config.value?.preferences?.enforce_lite_mode??false)
  const isDisableTweaksEnabled=computed(()=>config.value?.preferences?.disable_tweaks??false)
  const logLevel=computed(()=>config.value?.preferences?.log_level??3)
  async function loadConfig(){
    const text=await KernelSU.readFile(path)
    config.value=JSON.parse(text); originalText=text; pending={}
    highFPS.value=!(await KernelSU.fileExists(flag))
    return config.value
  }
  function set(field,value){
    if(!config.value) throw new Error('Configuration not loaded')
    config.value.preferences ??= {}
    pending[field]=value; config.value.preferences[field]=value
  }
  async function saveConfig(){
    if(!config.value) throw new Error('Configuration not loaded')
    const desired={...pending}; if(!Object.keys(desired).length)return
    const latest=await KernelSU.readFile(path), next=JSON.parse(latest)
    next.preferences={...next.preferences,...desired}
    const content=JSON.stringify(next,null,2)
    try{
      await KernelSU.writeFile(path,content,latest)
      config.value=next; originalText=content; pending={}
    }catch(e){config.value=JSON.parse(originalText);pending={};throw e}
  }
  async function setHighFPS(enabled){
    const result=await exec(enabled?`rm -f '${flag}'`:`touch '${flag}'`)
    if(result.errno!==0||(await KernelSU.fileExists(flag))===enabled) throw new Error(result.stderr||'Frame-rate preference write failed')
    highFPS.value=enabled
  }
  function setLogLevel(value){
    if(!Number.isInteger(value)||value<0||value>5)throw new Error('Invalid log level')
    set('log_level',value)
  }
  return {config,isLoaded,isLiteModeEnabled,isDisableTweaksEnabled,logLevel,highFPS,loadConfig,saveConfig,
    setLiteMode:value=>set('enforce_lite_mode',!!value),setDisableTweaks:value=>set('disable_tweaks',!!value),setLogLevel,setHighFPS}
})
