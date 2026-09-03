<template>
  <div class="app-container">
    <el-container style="height: 100vh;">
      <el-aside width="200px" style="background-color: #2e3b4e;">
        <el-menu
          default-active="1"
          class="el-menu-vertical-demo"
          background-color="#2e3b4e"
          text-color="#fff"
          active-text-color="#ffd04b"
          @select="handleMenuSelect"
        >
          <el-menu-item index="1">
            <el-icon><Menu /></el-icon>
            <template #title>原始报文展示</template>
          </el-menu-item>
          <el-menu-item index="2">
            <el-icon><DataAnalysis /></el-icon>
            <template #title>数据可视化</template>
          </el-menu-item>
          <el-menu-item index="3">
            <el-icon><Monitor /></el-icon>
            <template #title>实时数值展示</template>
          </el-menu-item>

          <!-- 新增：固件升级菜单 -->
          <el-menu-item index="4">
            <el-icon><Upload /></el-icon>
            <template #title>固件升级(OTA)</template>
          </el-menu-item>

        </el-menu>
      </el-aside>

      <el-main>
        <!-- 全部改成 v-show，不会销毁、不会丢失数据 -->
        <div v-show="activePage === '1'">
          <RawData />
        </div>
        <div v-show="activePage === '2'">
          <ChartView />
        </div>
        <div v-show="activePage === '3'">
          <DataDisplay />
        </div>
        <div v-show="activePage === '4'">
          <OtaUpload />
        </div>
      </el-main>
    </el-container>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import RawData from './components/RawData.vue'
import ChartView from './components/ChartView.vue'
import DataDisplay from './components/DataDisplay.vue'
import OtaUpload from './components/OtaUpload.vue'

import { Menu, DataAnalysis, Monitor, Upload } from '@element-plus/icons-vue'
import wsManager from './utils/wsManager.js'

const activePage = ref('1')

const handleMenuSelect = (index) => {
  activePage.value = index
}

// 全局初始化WebSocket
onMounted(() => {
  wsManager.init();
})
</script>

<style scoped>
.app-container {
  width: 100%;
  height: 100vh;
}
.el-aside {
  color: #333;
}
</style>