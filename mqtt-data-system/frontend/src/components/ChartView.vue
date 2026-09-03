<template>
  <div class="chart-container">
    <el-card shadow="hover">
      <template #header>
        <div class="card-header">
          <span>ADXL355传感器数据实时监控</span>
          <el-select v-model="activeMetric" placeholder="选择指标" style="width: 150px;">
            <el-option label="X轴g值" value="x_g" />
            <el-option label="Y轴g值" value="y_g" />
            <el-option label="Z轴g值" value="z_g" />
            <el-option label="俯仰角 (°)" value="pitch" />
            <el-option label="横滚角 (°)" value="roll" />
            <el-option label="数据包序号" value="pack_no" />
          </el-select>
        </div>
      </template>
      <div id="realtime-chart" style="width: 100%; height: 500px;"></div>
    </el-card>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, watch } from 'vue'
import { ElMessage } from 'element-plus'
import * as echarts from 'echarts'
import wsManager from '../utils/wsManager.js'

const activeMetric = ref('x_g')
let chart = null

// 图表数据（适配后端新字段）
const chartData = {
  timestamp: [],
  x_g: [],        // X轴g值
  y_g: [],        // Y轴g值
  z_g: [],        // Z轴g值
  pitch: [],      // 俯仰角
  roll: [],       // 横滚角
  pack_no: []     // 数据包序号
}
const MAX_DATA_LENGTH = 50  // 最多显示50个数据点

// 初始化图表
const initChart = () => {
  const chartDom = document.getElementById('realtime-chart')
  chart = echarts.init(chartDom)
  // 图表基础配置
  const option = {
    title: { text: 'ADXL355实时数据折线图', left: 'center' },
    tooltip: { 
      trigger: 'axis',
      formatter: '{b}<br/>{a}: {c}'
    },
    grid: { left: '3%', right: '4%', bottom: '3%', containLabel: true },
    xAxis: { 
      type: 'category', 
      data: chartData.timestamp,
      axisLabel: { rotate: 30 }  // X轴标签旋转，避免重叠
    },
    yAxis: { 
      type: 'value',
      name: '',
      axisLine: { lineStyle: { color: '#1989fa' } }
    },
    series: [{ 
      name: 'X轴g值', 
      type: 'line', 
      data: chartData.x_g, 
      smooth: true,
      itemStyle: { color: '#1989fa' },
      lineStyle: { width: 2 }
    }]
  }
  chart.setOption(option)
  // 窗口大小变化时自适应
  window.addEventListener('resize', () => chart.resize())
}

// 更新图表数据
const updateChart = () => {
  let seriesData = []
  let seriesName = ''
  let yAxisName = ''
  
  // 根据选中的指标切换数据
  switch (activeMetric.value) {
    case 'x_g':
      seriesData = chartData.x_g
      seriesName = 'X轴g值'
      yAxisName = 'g'
      break
    case 'y_g':
      seriesData = chartData.y_g
      seriesName = 'Y轴g值'
      yAxisName = 'g'
      break
    case 'z_g':
      seriesData = chartData.z_g
      seriesName = 'Z轴g值'
      yAxisName = 'g'
      break
    case 'pitch':
      seriesData = chartData.pitch
      seriesName = '俯仰角'
      yAxisName = '°'
      break
    case 'roll':
      seriesData = chartData.roll
      seriesName = '横滚角'
      yAxisName = '°'
      break
    case 'pack_no':
      seriesData = chartData.pack_no
      seriesName = '数据包序号'
      yAxisName = ''
      break
  }
  
  // 更新图表配置
  chart.setOption({
    xAxis: { data: chartData.timestamp },
    yAxis: { name: yAxisName },
    series: [{ 
      name: seriesName, 
      data: seriesData 
    }]
  })
}

// 处理WebSocket消息
const handleMessage = (data) => {
  try {
    const parsed = data.parsed_data
    const time = data.timestamp.split(' ')[1]  // 只取时分秒，简化X轴
    
    // 提取数据（适配后端下划线字段）
    chartData.timestamp.push(time)
    chartData.x_g.push(parsed.result?.x_g || 0)
    chartData.y_g.push(parsed.result?.y_g || 0)
    chartData.z_g.push(parsed.result?.z_g || 0)
    chartData.pitch.push(parsed.result?.pitch || 0)
    chartData.roll.push(parsed.result?.roll || 0)
    chartData.pack_no.push(parsed.pack_no || 0)
    
    // 限制数据长度，保持图表简洁
    if (chartData.timestamp.length > MAX_DATA_LENGTH) {
      chartData.timestamp.shift()
      chartData.x_g.shift()
      chartData.y_g.shift()
      chartData.z_g.shift()
      chartData.pitch.shift()
      chartData.roll.shift()
      chartData.pack_no.shift()
    }
    
    // 更新图表
    updateChart()
  } catch (e) {
    console.error('更新图表失败:', e)
  }
}

// 监听指标切换
watch(activeMetric, updateChart)

// 组件挂载
onMounted(() => {
  initChart()
  wsManager.addMessageListener(handleMessage)
  if (wsManager.isConnected) {
    ElMessage.success('已连接到数据服务');
  } else {
    ElMessage.warning('正在连接数据服务...');
  }
})

// 组件销毁
onUnmounted(() => {
  wsManager.removeMessageListener(handleMessage);
  if (chart) {
    chart.dispose()  // 销毁图表，释放资源
  }
  window.removeEventListener('resize', () => chart.resize())
})
</script>

<style scoped>
.chart-container { padding: 10px; }
.card-header { display: flex; justify-content: space-between; align-items: center; }
</style>