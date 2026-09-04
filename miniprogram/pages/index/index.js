// pages/index/index.js - 首页：设备实时概览
const { formatTime } = require('../../utils/util')

Page({
  data: {
    loading: true,
    noData: false,
    latest: null,       // 最新一条数据
    summary: {},        // 概览卡片字段
    alarm: false,       // 心率/血氧异常预警（alarm=1 弹红色预警条）
    db: null
  },

  onShow() {
    // 首次进入页面：调一次云函数采集最新数据（之后自动刷新只查库，不再调 ThingsCloud API 烧配额）
    this.loadLatest(true)
    this.startAutoRefresh()
  },

  onHide() { this.stopAutoRefresh() },
  onUnload() { this.stopAutoRefresh() },

  // 每 30 秒自动刷新（数据链路：手表40s上报 → 打开小程序时云函数手动采集）
  startAutoRefresh() {
    this.stopAutoRefresh()
    // fetchRemote=false：30s 轮询只查本地数据库 health_records，不再调 collectSensorData 云函数（避免每 30s 烧一次 ThingsCloud API）
    this.timer = setInterval(() => this.loadLatest(false), 30000)
  },
  stopAutoRefresh() {
    if (this.timer) { clearInterval(this.timer); this.timer = null }
  },

  async loadLatest(fetchRemote = false) {
    this.setData({ loading: true, noData: false })
    try {
      // 仅 fetchRemote=true（首次进入/手动刷新）时才调云函数采集；30s 自动轮询传 false 只查库，避免白白消耗 ThingsCloud 每日消息配额
      if (fetchRemote) {
        try { await wx.cloud.callFunction({ name: 'collectSensorData' }) } catch (e) { /* 设备离线/网络异常：忽略，继续展示历史数据 */ }
      }
      const db = wx.cloud.database()
      const res = await db.collection('health_records').orderBy('ts', 'desc').limit(1).get()
      if (res.data && res.data.length > 0) {
        const r = res.data[0]
        this.setData({
          loading: false,
          latest: r,
          summary: {
            time: formatTime(r.ts),
            temp: r.temp != null ? r.temp.toFixed(1) : '--',
            hum: r.hum != null ? Math.round(r.hum) : '--',
            press: r.press != null ? r.press.toFixed(0) : '--',
            bpm: r.bpm != null ? Math.round(r.bpm) : '--',
            spo2: r.spo2 != null ? Math.round(r.spo2) : '--',
            uv: r.uv != null ? r.uv.toFixed(1) : '--',
            rssi: r.rssi != null ? r.rssi : '--'
          },
          // 心率>150 或 <40、血氧<90 视为异常 → 弹预警（与手表端阈值保持一致）
          alarm: (r.bpm != null && (r.bpm > 150 || (r.bpm < 40 && r.bpm > 0))) ||
                 (r.spo2 != null && r.spo2 < 90 && r.spo2 > 0) ||
                 (r.alarm != null && r.alarm === 1)
        })
      } else {
        this.setData({ loading: false, noData: true })
      }
    } catch (e) {
      console.error('加载最新数据失败', e)
      this.setData({ loading: false, noData: true })
    }
  },

  goHealth() { wx.navigateTo({ url: '/pages/health/health' }) },
  goSport() { wx.navigateTo({ url: '/pages/sport/sport' }) },
  goWatchface() { wx.navigateTo({ url: '/pages/watchface/watchface' }) }
})
