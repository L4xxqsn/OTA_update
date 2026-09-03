<template>
  <div class="ota-container">
    <div class="card">
      <h2>STM32 OTA 远程升级</h2>

      <div class="status-bar">
        <span :class="statusColor">{{ statusText }}</span>
      </div>

      <!-- 上传文件 -->
      <div class="upload-section">
        <input
          type="file"
          accept=".bin"
          @change="handleFileChange"
          ref="fileInput"
        />
        <button
          @click="uploadBin"
          :disabled="uploading || !file || sending"
          class="btn btn-upload"
        >
          {{ uploading ? "上传中..." : "1. 上传 BIN 文件" }}
        </button>
      </div>

      <!-- 开始升级 -->
      <div class="control-section">
        <button
          @click="startUpgrade"
          :disabled="!ready || sending || completed"
          class="btn btn-start"
        >
          2. 开始自动升级（一问一答）
        </button>
        <button @click="clearAll" class="btn btn-clear">清空</button>
      </div>

      <!-- 进度 -->
      <div class="progress-section" v-if="totalPackets > 0">
        <div class="progress-bar">
          <div
            class="progress-fill"
            :style="{ width: progress + '%' }"
          ></div>
        </div>
        <p>
          已发送：{{ currentDigit }} / {{ totalPackets }} 包
          ({{ progress.toFixed(1) }}%)
        </p>
        <p class="progress-detail" v-if="fileSize > 0">
          文件大小：{{ (fileSize / 1024).toFixed(1) }} KB |
          已传输：{{ (currentOffset / 1024).toFixed(1) }} KB |
          偏移量：{{ currentOffset }} / {{ fileSize }} 字节
        </p>
        <p class="progress-detail" v-if="sending && statusText.includes('重传')">
          🔄 {{ statusText }}
        </p>
      </div>

      <!-- 16 进制包日志 -->
      <div class="hex-log">
        <h3>发送包日志（16进制）</h3>
        <div class="hex-content" ref="hexContent">
          <p v-for="(item, idx) in hexList" :key="idx" class="hex-item">
            {{ item }}
          </p>
          <p v-if="hexList.length === 0">暂无数据</p>
        </div>
      </div>
    </div>

    <!-- 升级完成弹窗 -->
    <div class="modal-overlay" v-if="showCompleteModal" @click.self="confirmComplete">
      <div class="modal-card">
        <div class="modal-icon">✅</div>
        <h3 class="modal-title">升级完成！</h3>
        <p class="modal-body">设备即将重启，界面数据将被清除。</p>
        <button class="btn btn-modal" @click="confirmComplete">确定</button>
      </div>
    </div>
  </div>
</template>

<script>
export default {
  name: "OtaUpload",
  data() {
    return {
      file: null,
      uploading: false,
      ready: false,
      sending: false,
      completed: false,
      showCompleteModal: false,   // 升级完成弹窗

      statusText: "等待上传",
      statusColor: "status-wait",

      currentDigit: 0,
      totalPackets: 0,
      progress: 0,
      fileSize: 0,            // 文件总大小（字节）
      currentOffset: 0,       // 当前发送偏移量（字节）
      chunkSize: 256,         // 将从后端接口获取，默认256 (Flash页大小)
      hexList: [],

      ws: null,
      baseURL: "http://127.0.0.1:8000",
    };
  },

  mounted() {
    this.connectWebSocket();
  },

  methods: {
    // WebSocket 连接
    connectWebSocket() {
      this.ws = new WebSocket("ws://127.0.0.1:8000/ws/ota");
      this.ws.onmessage = (evt) => {
        const msg = JSON.parse(evt.data);

        if (msg.status === "sending") {
          // 后端发送: offset, file_size, total_packets, progress
          this.currentOffset = msg.offset || 0;
          this.fileSize = msg.file_size || this.fileSize;
          this.totalPackets = msg.total_packets || this.totalPackets;
          this.progress = msg.progress || this.progress;
          // currentOffset = 下位机已确认的字节数
          // 已确认的包数 = 已确认字节 / 每包大小（取整）
          if (this.chunkSize > 0) {
            this.currentDigit = Math.floor(this.currentOffset / this.chunkSize);
          }
        } else if (msg.status === "complete") {
          this.sending = false;
          this.progress = 100;
          this.currentDigit = this.totalPackets;
          this.currentOffset = this.fileSize;
          this.showCompleteModal = true;
        } else if (msg.status === "resend_first") {
          // 下位机请求重新发送首包
          this.statusText = "🔄 下位机请求重传，重新发送首包...";
          this.statusColor = "status-sending";
          this.currentDigit = 0;
          this.currentOffset = 0;
          this.progress = 0;
        } else if (msg.status === "error") {
          this.statusText = "❌ " + (msg.msg || "升级出错");
          this.statusColor = "status-error";
          this.sending = false;
        }
        this.getHexList();
      };
    },

    // 选择文件
    handleFileChange(e) {
      this.file = e.target.files[0];
    },

    // 上传 BIN
    async uploadBin() {
      if (!this.file) return;
      this.uploading = true;
      this.statusText = "上传中...";
      this.statusColor = "status-sending";

      const formData = new FormData();
      formData.append("file", this.file);

      try {
        const res = await fetch(`${this.baseURL}/ota/upload_bin`, {
          method: "POST",
          body: formData,
        });
        const data = await res.json();
        if (data.status === "ok") {
          this.ready = true;
          this.statusText = "✅ 上传成功，可开始升级";
          this.statusColor = "status-ready";
          // 使用后端返回的分包信息
          this.fileSize = data.file_size || 0;
          this.totalPackets = data.total_packets;
          this.chunkSize = data.chunk_size || 212; // 兜底
        } else {
          throw new Error(data.msg);
        }
      } catch (err) {
        this.statusText = "❌ 上传失败";
        this.statusColor = "status-error";
      }
      this.uploading = false;
    },

    // 开始升级
    async startUpgrade() {
      this.sending = true;
      this.completed = false;
      this.currentDigit = 0;
      this.currentOffset = 0;
      this.progress = 0;
      this.hexList = [];
      this.statusText = "⏳ 发送首包，等待下位机应答...";
      this.statusColor = "status-sending";

      await fetch(`${this.baseURL}/ota/start_auto`);
      this.getHexList();
    },

    // 获取16进制包日志
    async getHexList() {
      const res = await fetch(`${this.baseURL}/ota/hex`);
      const data = await res.json();
      this.hexList = data.hex_list;
      this.$nextTick(() => {
        if (this.$refs.hexContent) {
          this.$refs.hexContent.scrollTop = this.$refs.hexContent.scrollHeight;
        }
      });
    },

    // 升级完成弹窗确认 — 全部清除，回到上传 BIN 之前的初始状态
    async confirmComplete() {
      this.showCompleteModal = false;

      // 通知后端清理
      try {
        await fetch(`${this.baseURL}/ota/clear`);
      } catch (err) { }

      // 全部状态归零
      this.file = null;
      this.uploading = false;
      this.ready = false;
      this.sending = false;
      this.completed = false;
      this.statusText = "等待上传";
      this.statusColor = "status-wait";
      this.currentDigit = 0;
      this.totalPackets = 0;
      this.progress = 0;
      this.fileSize = 0;
      this.currentOffset = 0;
      this.chunkSize = 256;
      this.hexList = [];

      // 清空文件选择器
      if (this.$refs.fileInput) {
        this.$refs.fileInput.value = "";
      }
    },

    // 清空（停止传输 + 重置进度，保留已上传的 BIN 文件）
    async clearAll() {
      // 通知后端停止发送并重置
      try {
        await fetch(`${this.baseURL}/ota/clear`);
      } catch (err) {
        // 即使后端调用失败也继续清空前端状态
      }

      this.sending = false;
      this.completed = false;
      this.currentDigit = 0;
      this.currentOffset = 0;
      this.progress = 0;
      this.hexList = [];
      // 保留 this.file / this.ready / this.fileSize / this.totalPackets / this.chunkSize
      // 用户可直接再次点击"开始升级"，无需重新上传
      if (this.ready) {
        this.statusText = "✅ 已上传，可开始升级";
        this.statusColor = "status-ready";
      } else {
        this.statusText = "等待上传";
        this.statusColor = "status-wait";
      }
    },
  },
};
</script>

<style scoped>
.ota-container {
  max-width: 900px;
  margin: 30px auto;
  padding: 20px;
  font-family: Arial, sans-serif;
}

.card {
  background: #fff;
  padding: 25px;
  border-radius: 12px;
  box-shadow: 0 0 10px rgba(0, 0, 0, 0.1);
}

h2 {
  text-align: center;
  margin-bottom: 20px;
  color: #333;
}

.status-bar {
  text-align: center;
  font-size: 16px;
  margin-bottom: 20px;
}

.status-wait {
  color: #666;
}
.status-ready {
  color: #009955;
}
.status-sending {
  color: #0066cc;
}
.status-success {
  color: #00aa00;
  font-weight: bold;
}
.status-error {
  color: #cc0000;
}

.upload-section,
.control-section {
  display: flex;
  justify-content: center;
  gap: 12px;
  margin: 16px 0;
}

.btn {
  padding: 10px 20px;
  border: none;
  border-radius: 6px;
  cursor: pointer;
  font-size: 14px;
}

.btn:disabled {
  background: #ccc !important;
  cursor: not-allowed;
}

.btn-upload {
  background: #007bff;
  color: white;
}

.btn-start {
  background: #28a745;
  color: white;
}

.btn-clear {
  background: #6c757d;
  color: white;
}

.progress-section {
  margin: 20px 0;
  text-align: center;
}

.progress-bar {
  width: 100%;
  height: 12px;
  background: #eee;
  border-radius: 6px;
  overflow: hidden;
}

.progress-fill {
  height: 100%;
  background: #28a745;
  transition: width 0.3s;
}

.progress-detail {
  font-size: 13px;
  color: #666;
  margin-top: 6px;
}

.hex-log {
  margin-top: 20px;
}

.hex-log h3 {
  font-size: 16px;
  margin-bottom: 10px;
}

.hex-content {
  height: 260px;
  overflow-y: auto;
  background: #f9f9f9;
  padding: 12px;
  border-radius: 6px;
  font-family: monospace;
  font-size: 12px;
  line-height: 1.5;
}

.hex-item {
  margin: 2px 0;
}

/* 升级完成弹窗 */
.modal-overlay {
  position: fixed;
  top: 0;
  left: 0;
  width: 100%;
  height: 100%;
  background: rgba(0, 0, 0, 0.5);
  display: flex;
  align-items: center;
  justify-content: center;
  z-index: 1000;
}

.modal-card {
  background: #fff;
  border-radius: 12px;
  padding: 40px 50px;
  text-align: center;
  box-shadow: 0 8px 30px rgba(0, 0, 0, 0.2);
  max-width: 400px;
  width: 90%;
}

.modal-icon {
  font-size: 48px;
  margin-bottom: 12px;
}

.modal-title {
  font-size: 20px;
  color: #333;
  margin-bottom: 10px;
}

.modal-body {
  font-size: 14px;
  color: #888;
  margin-bottom: 24px;
}

.btn-modal {
  background: #28a745;
  color: white;
  padding: 10px 50px;
  font-size: 15px;
}
</style>