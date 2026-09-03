// frontend/src/utils/wsManager.js
class WSManager {
  constructor() {
    this.ws = null; // WebSocket实例
    this.isConnected = false; // 连接状态
    this.messageListeners = []; // 消息监听回调列表
    this.reconnectTimer = null; // 重连定时器
    this.reconnectInterval = 3000; // 重连间隔（3秒）
  }

  // 初始化连接
  init() {
    if (this.ws && this.isConnected) return; // 已连接则直接返回

    // 创建WebSocket连接
    this.ws = new WebSocket('ws://localhost:8000/ws/data');

    // 连接成功
    this.ws.onopen = () => {
      console.log('全局WebSocket连接成功');
      this.isConnected = true;
      clearTimeout(this.reconnectTimer); // 清除重连定时器
    };

    // 接收消息：分发给所有监听者
    this.ws.onmessage = (event) => {
      const data = JSON.parse(event.data);
      this.messageListeners.forEach(listener => {
        listener(data); // 把消息传给每个监听的页面
      });
    };

    // 连接关闭：自动重连
    this.ws.onclose = () => {
      console.log('WebSocket连接断开，准备重连');
      this.isConnected = false;
      this.reconnectTimer = setTimeout(() => {
        this.init(); // 重新初始化连接
      }, this.reconnectInterval);
    };

    // 连接错误：自动重连
    this.ws.onerror = (error) => {
      console.error('WebSocket错误:', error);
      this.isConnected = false;
      this.ws.close();
    };
  }

  // 添加消息监听（页面组件调用）
  addMessageListener(callback) {
    this.messageListeners.push(callback);
  }

  // 移除消息监听（组件销毁时调用）
  removeMessageListener(callback) {
    this.messageListeners = this.messageListeners.filter(
      listener => listener !== callback
    );
  }

  // 手动关闭连接（全局退出时用）
  close() {
    clearTimeout(this.reconnectTimer);
    if (this.ws) {
      this.ws.close();
    }
    this.isConnected = false;
    this.messageListeners = [];
  }
}

// 创建单例实例（全局唯一）
const wsManager = new WSManager();
export default wsManager;