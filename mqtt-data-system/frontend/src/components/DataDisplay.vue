<template>
  <div class="data-display-container">
    <el-card shadow="hover">
      <template #header>
        <span>ADXL355实时数值展示</span>
      </template>
      <el-row :gutter="20" style="margin-top: 20px;">
        <!-- 基础信息 -->
        <el-col :span="6">
          <div class="data-item">
            <p>数据包序号</p>
            <h3>{{ pack_no }}</h3>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="data-item">
            <p>帧头</p>
            <h3>{{ head }}</h3>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="data-item">
            <p>命令字</p>
            <h3>{{ cmd }}</h3>
          </div>
        </el-col>
        <el-col :span="6">
          <div class="data-item">
            <p>帧尾</p>
            <h3>{{ tail }}</h3>
          </div>
        </el-col>
        
        <!-- 传感器数据 -->
        <el-col :span="8" style="margin-top: 30px;">
          <div class="data-item sensor-item">
            <p>X轴g值</p>
            <h3>{{ x_g }} <small>g</small></h3>
          </div>
        </el-col>
        <el-col :span="8" style="margin-top: 30px;">
          <div class="data-item sensor-item">
            <p>Y轴g值</p>
            <h3>{{ y_g }} <small>g</small></h3>
          </div>
        </el-col>
        <el-col :span="8" style="margin-top: 30px;">
          <div class="data-item sensor-item">
            <p>Z轴g值</p>
            <h3>{{ z_g }} <small>g</small></h3>
          </div>
        </el-col>
        
        <el-col :span="12" style="margin-top: 30px;">
          <div class="data-item sensor-item">
            <p>俯仰角 (Pitch)</p>
            <h3>{{ pitch }} <small>°</small></h3>
          </div>
        </el-col>
        <el-col :span="12" style="margin-top: 30px;">
          <div class="data-item sensor-item">
            <p>横滚角 (Roll)</p>
            <h3>{{ roll }} <small>°</small></h3>
          </div>
        </el-col>
      </el-row>
    </el-card>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import wsManager from '../utils/wsManager.js'

// 初始化数值
const head = ref('')
const pack_no = ref(0)
const cmd = ref('')
const tail = ref('')
const x_g = ref(0)
const y_g = ref(0)
const z_g = ref(0)
const pitch = ref(0)
const roll = ref(0)

// 消息处理函数（适配后端下划线字段）
const handleMessage = (data) => {
  const parsed = data.parsed_data
  // 更新基础字段
  head.value = parsed.head || ''
  pack_no.value = parsed.pack_no || 0
  cmd.value = parsed.cmd || ''
  tail.value = parsed.tail || ''
  // 更新传感器数据（保留6位小数）
  x_g.value = (parsed.result?.x_g || 0).toFixed(6)
  y_g.value = (parsed.result?.y_g || 0).toFixed(6)
  z_g.value = (parsed.result?.z_g || 0).toFixed(6)
  pitch.value = (parsed.result?.pitch || 0).toFixed(2)
  roll.value = (parsed.result?.roll || 0).toFixed(2)
}

// 组件挂载
onMounted(() => {
  wsManager.addMessageListener(handleMessage)
})

// 组件销毁
onUnmounted(() => {
  wsManager.removeMessageListener(handleMessage)
})
</script>

<style scoped>
.data-display-container {
  padding: 10px;
}
.data-item {
  text-align: center;
  padding: 15px 0;
  border-radius: 8px;
  background-color: #f8f9fa;
}
.sensor-item {
  background-color: #e8f4f8;
  padding: 20px 0;
}
.data-item p {
  font-size: 16px;
  color: #666;
  margin: 0 0 10px 0;
}
.data-item h3 {
  font-size: 28px;
  color: #1989fa;
  margin: 0;
  font-weight: 600;
}
.data-item small {
  font-size: 16px;
  color: #888;
  margin-left: 5px;
}
</style>