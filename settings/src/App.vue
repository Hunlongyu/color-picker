<template>
  <div class="app">
    <div class="frame" data-tauri-drag-region>
      <div class="win">
        <span @click="handleClose">
          <svg width="16" height="16" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg"><rect width="48" height="48" fill="white" fill-opacity="0.01"/><path d="M8 8L40 40" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><path d="M8 40L40 8" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></svg>
        </span>
      </div>
    </div>
    <div class="content">
      <div class="settings">
        <div class="item">
          <div class="label">截屏快捷键：</div>
          <div class="value">
            <input type="text" v-model.trim="shortcut" @change="handleShortcut">
          </div>
          <div class="status" :class="[isRegister ? 'yes' : 'no']" :title="isRegister ? '快捷键正常' : '快捷键被占用，请注册新的快捷键！'"></div>
        </div>
        <div class="item">
          <div class="label">颜色值类型：</div>
          <div class="value">
            <select v-model="colorType" @change="changeColorType">
              <option value="HEX" label="HEX"></option>
              <option value="HSL" label="HSL"></option>
              <option value="RGB" label="RGB"></option>
              <option value="CMYK" label="CMYK"></option>
              <option value="HWB" label="HWB"></option>
            </select>
          </div>
          <div class="status"></div>
        </div>
      </div>
      <div class="about">
        <div class="name">Color Picker</div>
        <div class="version">v{{version}}</div>
        <div class="link">
          <a target="_blank" href="https://github.com/Hunlongyu">Hunlongyu</a>
          <a target="_blank" href="https://github.com/Hunlongyu/color-picker">Github</a>
          <a target="_blank" href="https://github.com/Hunlongyu/color-picker/issues">反馈</a>
        </div>
      </div>
    </div>
  </div>
</template>
<script setup>
import { ref, onMounted } from 'vue'
import { appWindow } from '@tauri-apps/api/window'
import { emit } from '@tauri-apps/api/event'
import { isRegistered, unregister } from '@tauri-apps/api/globalShortcut'
import db from 'localforage'
import pkg from '../../package.json'
const shortcut = ref('F2')
const isRegister = ref(false)
const colorType = ref('HEX')
const version = ref(pkg.version)

// 软件关闭
function handleClose () {
  appWindow.close()
}
// 获取数据库信息
async function getDBSettings () {
  const s = await db.getItem('shortcut')
  if (s) shortcut.value = s
  const c = await db.getItem('colorType')
  if (c) colorType.value = c
  console.log(s, c)
  checkShortcut()
}
// 检查快捷键是否正常
async function checkShortcut () {
  const f = await isRegistered(shortcut.value)
  isRegister.value = f
}

async function handleShortcut () {
  if (shortcut.value === '') return false 
  const s = await db.getItem('shortcut')
  await unregister(s)
  await emit('changeShortcut', 'main', shortcut.value)
}
onMounted(() => {
  getDBSettings()
})
</script>
<style>
html,body, #app{
  margin: 0;
  padding: 0;
  height: 100%;
}
.app{
  height: 100%;
  box-sizing: border-box;
  background-color: #fff;
  border-radius: 6px;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  border: 1px solid #f0f0f0;
}
.frame{
  display: flex;
  justify-content: flex-end;
  height: 30px;
}
.win{
  display: flex;
}
.win span{
  display: flex;
  width: 30px;
  height: 30px;
  margin-left: 0px;
  justify-content: center;
  align-items: center;
}
.win span:hover{
  background-color: #f6f3f1;
  cursor: pointer;
}
.content{
  padding: 10px 20px;
}
.settings .item{
  display: flex;
  justify-content: space-between;
  margin-bottom: 10px;
  height: 30px;
  align-items: center;
}
.settings .item .label{
  color: #1e293c;
  font-size: 16px;
}
.settings .item .value input{
  height: 28px;
  width: 180px;
  border: none;
  outline: none;
  font-size: 18px;
  color: #252525;
  background-color: #f6f3f1;
  text-align: center;
  letter-spacing: 1px;
}
.settings .item .value select{
  height: 30px;
  width: 184px;
  border: none;
  background-color: #f6f3f1;
  outline: none;
  text-align: center;
  cursor: pointer;
  font-size: 18px;
  color: #252525;
}
.settings .item .status{
  width: 16px;
  height: 16px;
  border-radius: 50%;
  margin-right: 10px;
}
.settings .item .yes{
  background-color: #17a05d;
}
.settings .item .no{
  background-color: #dd4f43;
}
.about{
  display: flex;
  align-items: center;
  flex-direction: column;
  margin-top: 30px;
  border-top: 1px solid #f6f3f1;
  padding-top: 10px;
}
.about .name{
  font-size: 20px;
  font-weight: bold;
}
.about .version{
  margin-top: 10px;
  font-size: 14px;
}
.about .link{
  margin-top: 10px;
  width: 100%;
  display: flex;
  justify-content: space-around;
}
.about a{
  text-decoration: none;
  color: #4e6ef2;
}
</style>
