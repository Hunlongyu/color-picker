<template>
  <div class="app">
    <div class="frame" data-tauri-drag-region>
      <div class="menu">
        <span @click="openSettings">
          <svg width="20" height="20" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg"><rect width="48" height="48" fill="white" fill-opacity="0.01"/><path d="M7.94977 11.9498H39.9498" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><path d="M7.94977 23.9498H39.9498" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><path d="M7.94977 35.9498H39.9498" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></svg>
        </span>
      </div>
      <div class="win">
        <span @click="handleMinimize">
          <svg width="20" height="20" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg"><rect width="48" height="48" fill="white" fill-opacity="0.01"/><path d="M10.5 24L38.5 24" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></svg>
        </span>
        <span @click="handleClose">
          <svg width="16" height="16" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg"><rect width="48" height="48" fill="white" fill-opacity="0.01"/><path d="M8 8L40 40" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><path d="M8 40L40 8" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></svg>
        </span>
      </div>
    </div>
    <div class="body">
      <div class="picker" @click="handleColorPicker">
        <span>
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M20.3847 2.87868C19.2132 1.70711 17.3137 1.70711 16.1421 2.87868L14.0202 5.00052L13.313 4.29332C12.9225 3.9028 12.2894 3.9028 11.8988 4.29332C11.5083 4.68385 11.5083 5.31701 11.8988 5.70754L17.5557 11.3644C17.9462 11.7549 18.5794 11.7549 18.9699 11.3644C19.3604 10.9739 19.3604 10.3407 18.9699 9.95018L18.2629 9.24316L20.3847 7.12132C21.5563 5.94975 21.5563 4.05025 20.3847 2.87868Z" :fill="hexColor ? hexColor : '#000000'" /><path fill-rule="evenodd" clip-rule="evenodd" d="M11.9297 7.09116L4.1515 14.8693C3.22786 15.793 3.03239 17.169 3.5651 18.2842L1.99994 19.8493L3.41415 21.2635L4.97931 19.6984C6.09444 20.2311 7.4705 20.0356 8.39414 19.112L16.1723 11.3338L11.9297 7.09116ZM13.3439 11.3338L11.9297 9.91959L5.56571 16.2835C5.17518 16.6741 5.17518 17.3072 5.56571 17.6978C5.95623 18.0883 6.5894 18.0883 6.97992 17.6978L13.3439 11.3338Z" fill="currentColor" /></svg>
        </span>
      </div>
      <div class="input"><input type="text" v-model="colorValue"></div>
      <div class="copy">
        <span @click="handleCopy">
          <svg width="16" height="16" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M13 12.4316V7.8125C13 6.2592 14.2592 5 15.8125 5H40.1875C41.7408 5 43 6.2592 43 7.8125V32.1875C43 33.7408 41.7408 35 40.1875 35H35.5163" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><path d="M32.1875 13H7.8125C6.2592 13 5 14.2592 5 15.8125V40.1875C5 41.7408 6.2592 43 7.8125 43H32.1875C33.7408 43 35 41.7408 35 40.1875V15.8125C35 14.2592 33.7408 13 32.1875 13Z" fill="none" stroke="#333" stroke-width="4" stroke-linejoin="round"/></svg>
        </span>
      </div>
      <div class="type">
        <select v-model="colorType" @change="changeColorType">
          <option value="HEX" label="HEX"></option>
          <option value="HSL" label="HSL"></option>
          <option value="RGB" label="RGB"></option>
          <option value="CMYK" label="CMYK"></option>
          <option value="HWB" label="HWB"></option>
        </select>
      </div>
    </div>
    <div class="history">
      <div class="item" v-for="(i, j) in history" :key="j" :style="`background-color: ${i};`" @click="handleHistory(i)"></div>
    </div>
  </div>
</template>
<script setup>
import { ref, onMounted } from 'vue'
import { appWindow, getAll, WebviewWindow } from '@tauri-apps/api/window'
import { writeText } from '@tauri-apps/api/clipboard'
import { register } from '@tauri-apps/api/globalShortcut'
import { isPermissionGranted, requestPermission, sendNotification } from '@tauri-apps/api/notification'
import Color from 'color'
import db from 'localforage'

const colorValue = ref('')
const colorType = ref('HEX')
const hexColor = ref('')
const history = ref([])

// 打开设置界面
function openSettings () {
  new WebviewWindow('settings', {
    url: 'settings/index.html',
    width: 356,
    height: 230,
    resizable: false,
    center: true,
    decorations: false,
    transparent: true
  })
}
// 软件最小化
function handleMinimize () {
  appWindow.minimize()
}
// 软件关闭
function handleClose () {
  appWindow.close()
}
// 开始取色
async function handleColorPicker () {
  sendNotification('start pick color!')
  const eyeDropper = new EyeDropper()
  const result = await eyeDropper.open()
  hexColor.value = result.sRGBHex
  await checkRepeatColor()
  await changeColorType()
  await putHistory()
  await saveDbHistory()
}
// 颜色去重
async function checkRepeatColor () {
  if (history.value.length < 1) return false
  const idx = history.value.indexOf(hexColor.value)
  if (idx >= 0) {
    const arr = [...history.value]
    arr.splice(idx, 1)
    history.value = arr
  }
}
// 复制到剪贴板
function handleCopy () {
  if (colorValue.value === '') return false
  writeText(colorValue.value)
}
// 改变颜色类型
async function changeColorType () {
  if (hexColor.value === '') return false
  const color = Color(hexColor.value)
  if (colorType.value === 'HEX') {
    colorValue.value = color.hex()
  }
  if (colorType.value === 'HSL') {
    colorValue.value = color.hsl().string()
  }
  if (colorType.value === 'RGB') {
    colorValue.value = color.rgb().string()
  }
  if (colorType.value === 'CMYK') {
    colorValue.value = color.cmyk().round().array().toString()
  }
  if (colorType.value === 'HWB') {
    colorValue.value = color.hwb().string()
  }
}
// 历史记录
async function putHistory () {
  const arr = [...history.value]
  if (history.value.length < 12) {
    arr.unshift(hexColor.value)
  } else {
    arr.pop()
    arr.unshift(hexColor.value)
  }
  history.value = arr
}
// 点击历史记录里的颜色事件
function handleHistory (color) {
  hexColor.value = color
  changeColorType()
}
// 从本地数据库获取历史数据
async function getDbHistory () {
  const historyList = await db.getItem('history')
  if (historyList) {
    history.value = historyList
  }
}
// 保存历史数据到本地数据库
async function saveDbHistory () {
  db.setItem('history', [...history.value])
}
// 注册全局快捷键
async function handleShortcut () {
  try {
    await register('f2', handleColorPicker)
  } catch (ignore) {
    sendNotification('F2 快捷键被占用，请注册新的快捷键！')
  }
}
// 申请通知权限
async function getNotificationGranted () {
  const flag = await requestPermission()
  console.log('=== flag ===', flag)
  // const res = await isPermissionGranted()
  // if (!res) {
  // }
}
onMounted(async () => {
  getDbHistory()
  handleShortcut()
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
.frame, .body, .history{
  display: flex;
  justify-content: space-between;
}
.frame{
  height: 30px;
}
.body{
  height: 50px;
  display: flex;
  align-items: center;
}
.history{
  flex: 1;
  padding: 0 6px;
  display: flex;
  justify-content: flex-start;
}
.menu span, .win span, .picker span, .copy span{
  display: flex;
  width: 30px;
  height: 30px;
  margin-left: 0px;
  justify-content: center;
  align-items: center;
}
.copy span{
  background-color: #f6f3f1;
}
.win{
  display: flex;
}
.menu span:hover, .win span:hover{
  background-color: #f6f3f1;
  cursor: pointer;
}
.picker span:hover, .copy span:hover{
  background-color: #dad8d8;
  cursor: pointer;
}
.body{
  padding: 0 6px;
  justify-content: flex-start;
}
.body .input{
  height: 30px;
}
.body .input input{
  height: 28px;
  width: 200px;
  border: none;
  outline: none;
  background-color: #f6f3f1;
  font-size: 18px;
  color: #252525;
  text-indent: 4px;
  letter-spacing: 1px;
}
.body .type select{
  height: 30px;
  width: 82px;
  margin-left: 10px;
  border: none;
  background-color: #f6f3f1;
  outline: none;
  text-indent: 4px;
  cursor: pointer;
  font-size: 18px;
  color: #252525;
}
.history .item{
  width: 30px;
  height: 30px;
  margin-top: 5px;
  box-sizing: border-box;
  cursor: pointer;
}
.history .item:last-child{
  border-radius: 0 4px 4px 0;
}
.history .item:first-child{
  border-radius: 4px 0 0 4px;
}
.history .item:hover{
  box-shadow: 0 0 6px #333;
}
</style>
