// app.js - 小程序入口（云开发初始化）
App({
  onLaunch() {
    if (!wx.cloud) {
      console.error('请使用 2.2.3 或以上的基础库以使用云能力')
    } else {
      wx.cloud.init({
        env: 'YOUR_CLOUD_ENV_ID',
        traceUser: true
      })
    }
  },
  globalData: {
    envId: 'YOUR_CLOUD_ENV_ID'
  }
})