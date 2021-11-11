<template>
  <div class="app">
    <div class="frame" data-tauri-drag-region>
      <div class="menu">
        <span @click="openSettings" v-show="!show" title="设置">
          <svg width="20" height="20" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg"><rect width="48" height="48" fill="white" fill-opacity="0.01"/><path d="M7.94977 11.9498H39.9498" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><path d="M7.94977 23.9498H39.9498" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><path d="M7.94977 35.9498H39.9498" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></svg>
        </span>
        <span @click="backMain" v-show="show" title="返回">
          <svg width="20" height="20" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M12.9998 8L6 14L12.9998 21" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><path d="M6 14H28.9938C35.8768 14 41.7221 19.6204 41.9904 26.5C42.2739 33.7696 36.2671 40 28.9938 40H11.9984" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/></svg>
        </span>
        <span @click="openSite('home')" v-show="show" title="官网">
          <svg width="20" height="20" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg"><rect width="48" height="48" fill="white" fill-opacity="0.01"/><path d="M29.3444 30.4767C31.7481 29.9771 33.9292 29.1109 35.6247 27.8393C38.5202 25.6677 40 22.3137 40 19C40 16.6754 39.1187 14.5051 37.5929 12.6669C36.7427 11.6426 39.2295 4.00001 37.02 5.02931C34.8105 6.05861 31.5708 8.33691 29.8726 7.8341C28.0545 7.29577 26.0733 7.00001 24 7.00001C22.1992 7.00001 20.4679 7.22313 18.8526 7.63452C16.5046 8.23249 14.2591 6.00001 12 5.02931C9.74086 4.05861 10.9736 11.9633 10.3026 12.7946C8.84119 14.6052 8 16.7289 8 19C8 22.3137 9.79086 25.6677 12.6863 27.8393C14.6151 29.2858 17.034 30.2077 19.7401 30.6621" stroke="#333" stroke-width="4" stroke-linecap="round"/><path d="M19.7402 30.662C18.5817 31.9372 18.0024 33.148 18.0024 34.2946C18.0024 35.4411 18.0024 38.3465 18.0024 43.0108" stroke="#333" stroke-width="4" stroke-linecap="round"/><path d="M29.3443 30.4767C30.4421 31.9175 30.991 33.2112 30.991 34.3577C30.991 35.5043 30.991 38.3886 30.991 43.0108" stroke="#333" stroke-width="4" stroke-linecap="round"/><path d="M6 31.2156C6.89887 31.3255 7.56554 31.7388 8 32.4555C8.65169 33.5304 11.0742 37.5181 13.8251 37.5181C15.6591 37.5181 17.0515 37.5181 18.0024 37.5181" stroke="#333" stroke-width="4" stroke-linecap="round"/></svg>
        </span>
        <span @click="openSite('issues')" v-show="show" title="反馈">
          <svg width="20" height="20" viewBox="0 0 48 48" xmlns="http://www.w3.org/2000/svg"><g><rect width="48" height="48" fill="white" fill-opacity="0.01" stroke-linejoin="round" stroke-width="4" stroke="none" fill-rule="evenodd"/><g transform="translate(4.000000, 4.000000)"><path d="M20,38 C32,38 34,27.5323633 34,24 C34,20.837862 34,16.1711953 34,10 L6,10 C6,13.442296 6,18.1089627 6,24 C6,27.450596 8,38 20,38 Z" stroke="#333" fill="none" fill-rule="nonzero" stroke-linejoin="round" stroke-width="4"/><path d="M3.55271368e-15,4 L6,10" stroke="#333" stroke-linecap="round" stroke-linejoin="round" stroke-width="4" fill="none" fill-rule="evenodd"/><path d="M40,4 L34,10" stroke="#333" stroke-linecap="round" stroke-linejoin="round" stroke-width="4" fill="none" fill-rule="evenodd"/><path d="M0,23 L6,23" stroke="#333" stroke-linecap="round" stroke-linejoin="round" stroke-width="4" fill="none" fill-rule="evenodd"/><path d="M40,23 L34,23" stroke="#333" stroke-linecap="round" stroke-linejoin="round" stroke-width="4" fill="none" fill-rule="evenodd"/><path d="M3,40 L9,34" stroke="#333" stroke-linecap="round" stroke-linejoin="round" stroke-width="4" fill="none" fill-rule="evenodd"/><path d="M37,40 L31,34" stroke="#333" stroke-linecap="round" stroke-linejoin="round" stroke-width="4" fill="none" fill-rule="evenodd"/><path d="M20,38 L20,10" stroke="#333" stroke-linecap="round" stroke-linejoin="round" stroke-width="4" fill="none" fill-rule="evenodd"/><path d="M10.920475,35.0408965 C13.002464,36.7831828 15.9245088,38 20,38 L20,38 C24.1112534,38 27.0487287,36.7713317 29.1341392,35.0138386" stroke="#333" stroke-linecap="round" stroke-linejoin="round" stroke-width="4" fill="none" fill-rule="evenodd"/><path d="M28,8.33333333 C28,3.73096042 24.418278,0 20,0 C15.581722,0 12,3.73096042 12,8.33333333 L12,10 L28,10 L28,8.33333333 Z" stroke="#333" fill="none" fill-rule="nonzero" stroke-linejoin="round" stroke-width="4"/></g></g></svg>
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
          <svg width="20" height="20" viewBox="0 0 24 24" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M20.3847 2.87868C19.2132 1.70711 17.3137 1.70711 16.1421 2.87868L14.0202 5.00052L13.313 4.29332C12.9225 3.9028 12.2894 3.9028 11.8988 4.29332C11.5083 4.68385 11.5083 5.31701 11.8988 5.70754L17.5557 11.3644C17.9462 11.7549 18.5794 11.7549 18.9699 11.3644C19.3604 10.9739 19.3604 10.3407 18.9699 9.95018L18.2629 9.24316L20.3847 7.12132C21.5563 5.94975 21.5563 4.05025 20.3847 2.87868Z" fill="currentColor" /><path fill-rule="evenodd" clip-rule="evenodd" d="M11.9297 7.09116L4.1515 14.8693C3.22786 15.793 3.03239 17.169 3.5651 18.2842L1.99994 19.8493L3.41415 21.2635L4.97931 19.6984C6.09444 20.2311 7.4705 20.0356 8.39414 19.112L16.1723 11.3338L11.9297 7.09116ZM13.3439 11.3338L11.9297 9.91959L5.56571 16.2835C5.17518 16.6741 5.17518 17.3072 5.56571 17.6978C5.95623 18.0883 6.5894 18.0883 6.97992 17.6978L13.3439 11.3338Z" fill="currentColor" /></svg>
        </span>
      </div>
      <div class="input"><input type="text" v-model="colorValue"></div>
      <div class="copy">
        <span @click="handleCopy">
          <svg width="16" height="16" viewBox="0 0 48 48" fill="none" xmlns="http://www.w3.org/2000/svg"><path d="M13 12.4316V7.8125C13 6.2592 14.2592 5 15.8125 5H40.1875C41.7408 5 43 6.2592 43 7.8125V32.1875C43 33.7408 41.7408 35 40.1875 35H35.5163" stroke="#333" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/><path d="M32.1875 13H7.8125C6.2592 13 5 14.2592 5 15.8125V40.1875C5 41.7408 6.2592 43 7.8125 43H32.1875C33.7408 43 35 41.7408 35 40.1875V15.8125C35 14.2592 33.7408 13 32.1875 13Z" :fill="hexColor ? hexColor : '#000000'" stroke="#333" stroke-width="4" stroke-linejoin="round"/></svg>
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
      <div class="item"
        v-for="(i, j) in history" :key="j"
        :style="`background-color: ${i};`"
        title="1. 左键单击显示颜色值; &#10;2. 左键双击复制颜色值; &#10;3. 右键单击删除颜色值"
        @click="handleHistory(i)"
        @dblclick="handleCopy"
        @contextmenu.prevent="deleteItem(j)"></div>
    </div>
    <div class="settings" v-show="show">
      <div class="item">
        <div class="label" title="修改快捷键会重启软件">截屏快捷键：</div>
        <div class="value" title="修改快捷键会重启软件">
          <input type="text" v-model.trim="shortcut" @change="changeShortcut">
        </div>
        <div class="status" :class="[isRegister ? 'yes' : 'no']" :title="isRegister ? '快捷键正常' : '快捷键被占用，请注册新的快捷键！'"></div>
      </div>
      <div class="item">
        <div class="label">颜色值类型：</div>
        <div class="value">
          <select v-model="dbColorType" @change="changeDbColorType">
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
  </div>
  <notifications position="bottom right" />
</template>
<script lang="ts" setup>
import { ref, onMounted } from 'vue'
import { appWindow } from '@tauri-apps/api/window'
import { writeText } from '@tauri-apps/api/clipboard'
import { isRegistered, unregister, register } from '@tauri-apps/api/globalShortcut'
import { open } from '@tauri-apps/api/shell'
import Color from 'color'
import db from 'localforage'

const colorValue = ref<string>('')
const colorType = ref<string>('HEX')
const hexColor = ref<string>('')
const history = ref<string[]>([])
const show = ref<boolean>(false)
const shortcut = ref<string>('F2')
const isRegister = ref<boolean>(false)
const dbColorType = ref<string>('')

// 打开设置界面
async function openSettings () {
  show.value = true
  checkShortcut()
}
// 返回主界面
async function backMain () {
  show.value = false
}
// 访问网站
function openSite (site: string) {
  if (site === 'home') {
    open('https://github.com/Hunlongyu/color-picker')
  }
  if (site === 'issues') {
    open('https://github.com/Hunlongyu/color-picker/issues')
  }
}
// 软件最小化
function handleMinimize () {
  appWindow.hide()
}
// 软件关闭
function handleClose () {
  appWindow.close()
}
// 开始取色
async function handleColorPicker () {
  // @ts-ignore
  const eyeDropper = new EyeDropper()
  const result = await eyeDropper.open()
  hexColor.value = result.sRGBHex
  await checkRepeatColor()
  await changeColorType()
  await putHistory()
  await saveDbHistory()
  handleCopy()
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
// 右键删除颜色
function deleteItem (n: number) {
  const arr = [...history.value]
  arr.splice(n, 1)
  history.value = arr
  saveDbHistory()
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
function handleHistory (color: string) {
  hexColor.value = color
  changeColorType()
}
// 从本地数据库获取历史数据
async function getDbHistory () {
  const historyList: string[] | null = await db.getItem('history')
  if (historyList) {
    history.value = historyList
    hexColor.value = history.value[0]
    changeColorType()
  }
}
// 保存历史数据到本地数据库
async function saveDbHistory () {
  db.setItem('history', [...history.value])
}
// 注册全局快捷键
async function handleShortcut (s: string) {
  try {
    await register(s, handleColorPicker)
    await checkShortcut()
    db.setItem('shortcut', s)
  } catch (ignore) {}
}
// 切换快捷键
async function changeShortcut () {
  const ds: string | null = await db.getItem('shortcut')
  if (ds) await unregister(ds)
  handleShortcut(shortcut.value)
}
// 获取数据库 设置信息
async function getDBSettings () {
  const s: string | null = await db.getItem('shortcut')
  shortcut.value = s ? s : 'F2'
  handleShortcut(shortcut.value)
  const c: string | null = await db.getItem('colorType')
  if (c) {
    colorType.value = c
    dbColorType.value = c
  } else {
    colorType.value = 'HEX'
    dbColorType.value = 'HEX'
  }
}
// 检查快捷键是否正常
async function checkShortcut () {
  const f = await isRegistered(shortcut.value)
  console.log(f, 'fff')
  isRegister.value = f
}
// 保存color type 到数据库
async function changeDbColorType () {
  await db.setItem('colorType', dbColorType.value)
  colorType.value = dbColorType.value
  changeColorType()
}
onMounted(async () => {
  await getDBSettings()
  await getDbHistory()
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
  border-radius: 8px;
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
.win, .menu{
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
.settings{
  position: absolute;
  top: 30px;
  left: 0;
  background-color: #fff;
  width: calc(100% - 12px);
  height: calc(100% - 30px);
  border-radius: 0 0 8px 8px;
  padding: 0 6px;
  display: flex;
  flex-direction: column;
  justify-content: center;
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
</style>
