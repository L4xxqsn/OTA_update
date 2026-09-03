<template>
  <div class="raw-data-container">
    <el-card shadow="hover">
      <template #header>
        <div class="card-header">
          <span>ADXL355原始报文列表</span>
          <el-button type="primary" size="small" @click="clearData">清空列表</el-button>
        </div>
      </template>
      <el-table :data="messageList" border style="width: 100%; margin-top: 10px;" max-height="600">
        <el-table-column prop="timestamp" label="时间戳" width="200" />
        <el-table-column prop="topic" label="MQTT主题" width="150" />
        <el-table-column prop="raw_data_hex" label="原始数据(16进制)" min-width="400">
          <template #default="scope">
            <el-input 
              v-model="scope.row.raw_data_hex" 
              size="small" 
              readonly 
              type="textarea" 
              :rows="2"
              style="font-family: monospace;"
            />
          </template>
        </el-table-column>
        <el-table-column label="解析后数据" min-width="500">
          <template #default="scope">
            <pre style="font-size: 12px; margin: 0;">{{ JSON.stringify(scope.row.parsed_data, null, 2) }}</pre>
          </template>
        </el-table-column>
      </el-table>
    </el-card>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { ElMessage } from 'element-plus'
import wsManager from '../utils/wsManager.js'

const messageList = ref([])

// 消息处理函数
const handleMessage = (data) => {
  // 新增数据到列表头部
  messageList.value.unshift(data);
  // 限制列表长度，避免性能问题
  if (messageList.value.length > 100) {
    messageList.value.pop();
  }
};

// 清空列表
const clearData = () => {
  messageList.value = [];
  ElMessage.info('已清空报文列表');
};

// 组件挂载：添加消息监听
onMounted(() => {
  wsManager.addMessageListener(handleMessage);
  if (wsManager.isConnected) {
    ElMessage.success('已连接到ADXL355数据服务');
  } else {
    ElMessage.warning('正在连接数据服务...');
  }
});

// 组件销毁：移除监听（不关闭连接）
onUnmounted(() => {
  wsManager.removeMessageListener(handleMessage);
});
</script>

<style scoped>
.raw-data-container { padding: 10px; }
.card-header { display: flex; justify-content: space-between; align-items: center; }
pre { white-space: pre-wrap; word-wrap: break-word; }
</style>